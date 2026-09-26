#include "blynk/blynk_manager.hpp"

#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_tls.h"
#include "esp_wifi.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "nvs_flash.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

namespace tank_monitor::blynk {
namespace {

constexpr char kLogTag[] = "blynk";
constexpr EventBits_t kWifiConnected = BIT0;
constexpr std::uint8_t kResponse = 0;
constexpr std::uint8_t kPing = 6;
constexpr std::uint8_t kHardware = 20;
constexpr std::uint8_t kLogin = 29;
constexpr std::uint16_t kSuccess = 200;
constexpr std::uint32_t kHeartbeatMilliseconds = 40'000;

struct Header {
    std::uint8_t type{0};
    std::uint16_t message_id{0};
    std::uint16_t length{0};
} __attribute__((packed));

bool write_all(esp_tls_t* tls, const std::uint8_t* data, std::size_t length)
{
    while (length > 0) {
        const int written = esp_tls_conn_write(tls, data, length);
        if (written <= 0) return false;
        data += written;
        length -= static_cast<std::size_t>(written);
    }
    return true;
}

bool read_all(esp_tls_t* tls, std::uint8_t* data, std::size_t length)
{
    while (length > 0) {
        const int received = esp_tls_conn_read(tls, data, length);
        if (received <= 0) return false;
        data += received;
        length -= static_cast<std::size_t>(received);
    }
    return true;
}

bool send_command(esp_tls_t* tls, std::uint8_t type, std::uint16_t message_id,
                  const char* body, std::size_t body_length)
{
    if (body_length > UINT16_MAX) return false;
    Header header{type, htons(message_id), htons(static_cast<std::uint16_t>(body_length))};
    return write_all(tls, reinterpret_cast<const std::uint8_t*>(&header), sizeof(header)) &&
        (body_length == 0 || write_all(tls, reinterpret_cast<const std::uint8_t*>(body), body_length));
}

bool read_response(esp_tls_t* tls, std::uint16_t expected_message_id)
{
    Header header{};
    if (!read_all(tls, reinterpret_cast<std::uint8_t*>(&header), sizeof(header))) return false;
    const std::uint16_t message_id = ntohs(header.message_id);
    const std::uint16_t length = ntohs(header.length);
    if (header.type == kResponse) {
        return message_id == expected_message_id && length == kSuccess;
    }
    if (length > 256) return false;
    std::array<std::uint8_t, 256> ignored{};
    return length == 0 || read_all(tls, ignored.data(), length);
}

std::int16_t pin_for(const config::BlynkConfig& configuration, std::size_t index)
{
    return configuration.virtual_pins[index];
}

bool publish_value(esp_tls_t* tls, std::uint16_t& message_id, std::int16_t pin, const char* value)
{
    if (pin < 0 || value == nullptr) return true;
    std::array<char, 96> body{};
    const int length = std::snprintf(body.data(), body.size(), "vw%c%d%c%s", '\0', pin, '\0', value);
    if (length <= 0 || static_cast<std::size_t>(length) >= body.size()) return false;
    const bool sent = send_command(tls, kHardware, ++message_id, body.data(), static_cast<std::size_t>(length));
    if (sent) ESP_LOGI(kLogTag, "Blynk write V%d=%s", pin, value);
    return sent;
}

std::size_t configured_pin_count(const config::BlynkConfig& configuration)
{
    return static_cast<std::size_t>(std::count_if(
        configuration.virtual_pins.begin(), configuration.virtual_pins.end(),
        [](std::int16_t pin) { return pin >= 0; }));
}

}  // namespace

bool BlynkManager::start(const config::WifiConfig& wifi, const config::BlynkConfig& blynk)
{
    if (started_ || !blynk.enabled) return started_;
    wifi_ = wifi;
    configuration_ = blynk;
    events_ = xEventGroupCreate();
    if (events_ == nullptr) return false;

    esp_err_t error = nvs_flash_init();
    if (error != ESP_OK && error != ESP_ERR_NVS_INVALID_STATE) return false;
    error = esp_netif_init();
    if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) return false;
    error = esp_event_loop_create_default();
    if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) return false;
    if (esp_netif_create_default_wifi_sta() == nullptr) return false;
    const wifi_init_config_t initialization = WIFI_INIT_CONFIG_DEFAULT();
    error = esp_wifi_init(&initialization);
    if (error != ESP_OK && error != ESP_ERR_WIFI_INIT_STATE) return false;
    if (esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, this) != ESP_OK ||
        esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, this) != ESP_OK ||
        esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK) return false;

    wifi_config_t station{};
    std::memcpy(station.sta.ssid, wifi_.ssid.data(), wifi_.ssid.size());
    std::memcpy(station.sta.password, wifi_.password.data(), wifi_.password.size());
    if (esp_wifi_set_config(WIFI_IF_STA, &station) != ESP_OK || esp_wifi_start() != ESP_OK) return false;
    started_ = xTaskCreate(task_entry, "blynk", 8192, this, 1, nullptr) == pdPASS;
    return started_;
}

void BlynkManager::publish(const Telemetry& telemetry)
{
    portENTER_CRITICAL(&telemetry_lock_);
    telemetry_ = telemetry;
    ++telemetry_generation_;
    portEXIT_CRITICAL(&telemetry_lock_);
}

Health BlynkManager::health() const
{
    return health_;
}

void BlynkManager::wifi_event_handler(void* argument, esp_event_base_t base, std::int32_t id, void* data)
{
    auto* manager = static_cast<BlynkManager*>(argument);
    if (manager == nullptr || manager->events_ == nullptr) return;
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const auto* event = static_cast<const ip_event_got_ip_t*>(data);
        ESP_LOGI(kLogTag, "Wi-Fi got IP " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(manager->events_, kWifiConnected);
        manager->health_.wifi_connected = true;
        wifi_ap_record_t access_point{};
        if (esp_wifi_sta_get_ap_info(&access_point) == ESP_OK) manager->health_.wifi_rssi_dbm = access_point.rssi;
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        const auto* event = static_cast<const wifi_event_sta_disconnected_t*>(data);
        ESP_LOGW(kLogTag, "Wi-Fi disconnected: reason=%u", event->reason);
        xEventGroupClearBits(manager->events_, kWifiConnected);
        manager->health_.wifi_connected = false;
        manager->health_.blynk_online = false;
        static_cast<void>(esp_wifi_connect());
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        static_cast<void>(esp_wifi_connect());
    }
}

void BlynkManager::task_entry(void* argument)
{
    static_cast<BlynkManager*>(argument)->run();
}

void BlynkManager::run()
{
    while (true) {
        xEventGroupWaitBits(events_, kWifiConnected, pdFALSE, pdTRUE, portMAX_DELAY);
        ESP_LOGI(kLogTag, "Wi-Fi connected; opening Blynk TLS session");
        esp_tls_cfg_t tls_configuration{};
        tls_configuration.crt_bundle_attach = esp_crt_bundle_attach;
        esp_tls_t* tls = esp_tls_init();
        if (tls == nullptr || esp_tls_conn_new_sync(
                configuration_.host.data(), std::strlen(configuration_.host.data()), configuration_.port,
                &tls_configuration, tls) != 1) {
            ESP_LOGW(kLogTag, "Blynk TLS connection failed");
            if (tls != nullptr) esp_tls_conn_destroy(tls);
            vTaskDelay(pdMS_TO_TICKS(wifi_.reconnect_minimum_ms));
            continue;
        }
        if (!send_command(tls, kLogin, 1, configuration_.auth_token.data(),
                          std::strlen(configuration_.auth_token.data())) || !read_response(tls, 1)) {
            ESP_LOGW(kLogTag, "Blynk login rejected");
            esp_tls_conn_destroy(tls);
            vTaskDelay(pdMS_TO_TICKS(wifi_.reconnect_minimum_ms));
            continue;
        }
        ESP_LOGI(kLogTag, "Blynk online");
        health_.blynk_online = true;
        const std::size_t mapped_pins = configured_pin_count(configuration_);
        if (mapped_pins == 0) {
            ESP_LOGW(kLogTag, "Blynk has no mapped virtual pins; configure V0-V11 in provisioning");
        } else {
            ESP_LOGI(kLogTag, "Blynk virtual-pin mappings active: %u", static_cast<unsigned int>(mapped_pins));
        }
        std::uint16_t message_id = 1;
        std::uint32_t published_generation = 0;
        TickType_t last_ping = xTaskGetTickCount();
        while ((xEventGroupGetBits(events_) & kWifiConnected) != 0) {
            Telemetry telemetry{};
            std::uint32_t generation = 0;
            portENTER_CRITICAL(&telemetry_lock_);
            telemetry = telemetry_;
            generation = telemetry_generation_;
            portEXIT_CRITICAL(&telemetry_lock_);
            if (generation != 0 && generation != published_generation) {
                std::array<char, 80> value{};
                const auto publish = [&](std::size_t index, const char* text) {
                    if (text == nullptr) return false;
                    return publish_value(tls, message_id, pin_for(configuration_, index), text);
                };
                const auto publish_number = [&](std::size_t index, long number) {
                    std::snprintf(value.data(), value.size(), "%ld", number);
                    return publish_value(tls, message_id, pin_for(configuration_, index), value.data());
                };
                const double battery_volts = static_cast<double>(telemetry.battery_millivolts) / 1000.0;
                const double battery_percentage = std::clamp(
                    100.0 * (battery_volts - 3.2) / (4.2 - 3.2), 0.0, 100.0);
                if (!publish_number(0, telemetry.distance_centimetres) ||
                    !publish_number(1, telemetry.level_centimetres)) {
                    ESP_LOGW(kLogTag, "Blynk telemetry publish failed");
                    break;
                }
                std::snprintf(value.data(), value.size(), "%.2f",
                              static_cast<double>(telemetry.percentage_basis_points) / 100.0);
                if (!publish(2, value.data()) ||
                    !publish_number(3, telemetry.volume_litres)) {
                    ESP_LOGW(kLogTag, "Blynk telemetry publish failed");
                    break;
                }
                std::snprintf(value.data(), value.size(), "%.2f", battery_volts);
                if (!publish(4, value.data())) break;
                std::snprintf(value.data(), value.size(), "%.2f", battery_percentage);
                if (!publish(5, value.data())) break;
                std::snprintf(value.data(), value.size(), "rssi=%ddBm snr=%.1fdB sensor=%u",
                              telemetry.rssi_dbm, static_cast<double>(telemetry.snr_tenths_db) / 10.0,
                              telemetry.sensor_status);
                if (!publish(6, value.data())) break;
                std::snprintf(value.data(), value.size(), "sensor=%u battery=%.2fV",
                              telemetry.sensor_status, battery_volts);
                if (!publish(7, value.data())) break;
                if (!publish(8, "wifi=online blynk=online")) break;
                published_generation = generation;
                if (mapped_pins > 0) ESP_LOGI(kLogTag, "Blynk telemetry batch sent");
            }
            if (xTaskGetTickCount() - last_ping >= pdMS_TO_TICKS(kHeartbeatMilliseconds)) {
                if (!send_command(tls, kPing, ++message_id, nullptr, 0)) break;
                last_ping = xTaskGetTickCount();
            }
            vTaskDelay(pdMS_TO_TICKS(250));
        }
        esp_tls_conn_destroy(tls);
        health_.blynk_online = false;
        ESP_LOGW(kLogTag, "Blynk disconnected");
    }
}

}  // namespace tank_monitor::blynk