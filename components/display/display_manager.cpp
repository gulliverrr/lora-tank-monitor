#include "display/display_manager.hpp"

#include "board/tbeam_v1_2.hpp"

#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ssd1306.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>

namespace tank_monitor::display {
namespace {

constexpr char kLogTag[] = "display";
constexpr int kWidth = 128;
constexpr int kHeight = 64;
constexpr int kLineHeight = 9;
constexpr int kCharWidth = 6;
ssd1306_handle_t display = nullptr;

struct DiagnosticsContext {
    board::SelfTestResult board{};
    board::PowerStatus power{};
    battery::BatteryEstimate battery{};
    config::AppConfig configuration{};
};

DiagnosticsContext diagnostics{};
LiveTelemetry tx_telemetry{};
LiveTelemetry rx_telemetry{};
NetworkHealth network_health{};
std::int64_t tx_measurement_time_us{0};
std::int64_t rx_measurement_time_us{0};
std::array<std::int64_t, 10> rx_intervals_us{};
std::size_t rx_interval_count{0};
std::size_t rx_interval_next{0};
bool remeasure_requested{false};
std::atomic<bool> idle_timeout_elapsed{false};

bool draw_line(int line, const char* text)
{
    return ssd1306_draw_text(display, 0, line * kLineHeight, text, true) == ESP_OK;
}

void draw_title_with_version(const char* title)
{
    draw_line(0, title);
    const char* version = esp_app_get_description()->version;
    if (*version == 'v') ++version;
    std::array<char, 16> text{};
    std::snprintf(text.data(), text.size(), "v%.*s",
                  static_cast<int>(std::strcspn(version, "-+")), version);
    const char* shown = text.data();
    const int available = kWidth - (static_cast<int>(std::strlen(title)) + 1) * kCharWidth;
    int width = static_cast<int>(std::strlen(shown)) * kCharWidth - 1;
    if (width > available) {
        ++shown;
        width -= kCharWidth;
    }
    if (width > available) return;
    ssd1306_draw_text(display, kWidth - width, 0, shown, true);
}

void format_age(std::int64_t then_us, char* output, std::size_t capacity)
{
    const std::uint32_t seconds = then_us <= 0 ? 0 : static_cast<std::uint32_t>((esp_timer_get_time() - then_us) / 1000000LL);
    if (seconds >= 86400) std::snprintf(output, capacity, "%lud %luh", static_cast<unsigned long>(seconds / 86400), static_cast<unsigned long>((seconds / 3600) % 24));
    else if (seconds >= 3600) std::snprintf(output, capacity, "%luh %lum", static_cast<unsigned long>(seconds / 3600), static_cast<unsigned long>((seconds / 60) % 60));
    else if (seconds >= 60) std::snprintf(output, capacity, "%lum %lus", static_cast<unsigned long>(seconds / 60), static_cast<unsigned long>(seconds % 60));
    else std::snprintf(output, capacity, "%lus", static_cast<unsigned long>(seconds));
}

const char* rssi_word(std::int16_t rssi)
{
    if (rssi >= -70) return "excellent";
    if (rssi >= -85) return "good";
    if (rssi >= -100) return "fair";
    return "bad";
}

std::uint32_t predicted_rx_interval_seconds()
{
    if (rx_interval_count == 0) return 0;
    std::int64_t total = 0;
    for (std::size_t index = 0; index < rx_interval_count; ++index) total += rx_intervals_us[index];
    return static_cast<std::uint32_t>(total / static_cast<std::int64_t>(rx_interval_count) / 1000000LL);
}

void format_countdown(std::uint32_t seconds, char* output, std::size_t capacity)
{
    if (seconds >= 86400) std::snprintf(output, capacity, "%ud%02uh%02um%02us", static_cast<unsigned int>(std::min<std::uint32_t>(seconds / 86400, 999)), static_cast<unsigned int>((seconds / 3600) % 24), static_cast<unsigned int>((seconds / 60) % 60), static_cast<unsigned int>(seconds % 60));
    else if (seconds >= 3600) std::snprintf(output, capacity, "%uh%02um%02us", static_cast<unsigned int>(std::min<std::uint32_t>(seconds / 3600, 999)), static_cast<unsigned int>((seconds / 60) % 60), static_cast<unsigned int>(seconds % 60));
    else if (seconds >= 60) std::snprintf(output, capacity, "%lum%02lus", static_cast<unsigned long>(seconds / 60), static_cast<unsigned long>(seconds % 60));
    else std::snprintf(output, capacity, "%lus", static_cast<unsigned long>(seconds));
}

const char* bars(std::int16_t rssi)
{
    if (rssi >= -60) return "||||";
    if (rssi >= -75) return "|||.";
    if (rssi >= -90) return "||..";
    return "|...";
}

void draw_diagnostics_page(std::uint8_t page)
{
    if (ssd1306_clear(display) != ESP_OK) return;
    std::array<char, 22> line{};
    if (page == 0) {
        if (diagnostics.configuration.device.role == config::DeviceRole::Transmitter) {
            draw_title_with_version("TANK STATUS");
            std::snprintf(line.data(), line.size(), "Batt %u%% %.2fV", diagnostics.battery.percentage, diagnostics.battery.calibrated_voltage);
            draw_line(2, line.data());
            std::array<char, 12> age_text{};
            format_age(tx_measurement_time_us, age_text.data(), age_text.size());
            std::snprintf(line.data(), line.size(), "Air %lucm %s", static_cast<unsigned long>(tx_telemetry.distance_centimetres), age_text.data());
            draw_line(3, line.data());
            std::snprintf(line.data(), line.size(), "Water %u%%", tx_telemetry.water_percentage_basis_points / 100U);
            draw_line(4, line.data());
            const std::uint32_t interval = diagnostics.configuration.sensor.measurement_interval_seconds;
            const std::uint32_t age = tx_measurement_time_us <= 0 ? 0 : static_cast<std::uint32_t>((esp_timer_get_time() - tx_measurement_time_us) / 1000000LL);
            std::array<char, 15> countdown{};
            format_countdown(interval > age ? interval - age : 0, countdown.data(), countdown.size());
            std::snprintf(line.data(), line.size(), "Next %s", countdown.data());
            draw_line(6, line.data());
        } else {
            draw_title_with_version("GATEWAY STATUS");
            std::snprintf(line.data(), line.size(), "TX batt %u%% %.2fV", rx_telemetry.battery_percentage, static_cast<double>(rx_telemetry.battery_millivolts) / 1000.0);
            draw_line(1, line.data());
            std::snprintf(line.data(), line.size(), "LoRa %s %ddBm", rssi_word(rx_telemetry.rssi_dbm), rx_telemetry.rssi_dbm);
            draw_line(2, line.data());
            std::array<char, 12> age_text{};
            format_age(rx_measurement_time_us, age_text.data(), age_text.size());
            std::snprintf(line.data(), line.size(), "Air %lucm %s", static_cast<unsigned long>(rx_telemetry.distance_centimetres), age_text.data());
            draw_line(4, line.data());
            std::snprintf(line.data(), line.size(), "Water %u%%", rx_telemetry.water_percentage_basis_points / 100U);
            draw_line(5, line.data());
            const std::uint32_t interval = predicted_rx_interval_seconds();
            const std::uint32_t age_seconds = rx_measurement_time_us <= 0 ? 0 : static_cast<std::uint32_t>((esp_timer_get_time() - rx_measurement_time_us) / 1000000LL);
            std::array<char, 15> countdown{};
            format_countdown(interval > age_seconds ? interval - age_seconds : 0, countdown.data(), countdown.size());
            std::snprintf(line.data(), line.size(), "Next %s", countdown.data());
            draw_line(6, line.data());
        }
    } else if (page == 1 && diagnostics.configuration.device.role == config::DeviceRole::Receiver) {
        draw_line(0, "INTERNET");
        std::snprintf(line.data(), line.size(), "%s", network_health.blynk_online ? "Blynk online" : "Blynk offline");
        draw_line(2, line.data());
        std::snprintf(line.data(), line.size(), "WiFi %s %ddBm", bars(network_health.wifi_rssi_dbm), network_health.wifi_rssi_dbm);
        draw_line(3, line.data());
        draw_line(5, "Hold at boot to setup.");
    } else {
				draw_line(0, "RADIO");
        std::snprintf(line.data(), line.size(), "%lu MHz", static_cast<unsigned long>(diagnostics.configuration.radio.frequency_hz / 1000000U));
        draw_line(2, line.data());
        std::snprintf(line.data(), line.size(), "SF%u BW%luKHz", diagnostics.configuration.radio.spreading_factor,
                      static_cast<unsigned long>(diagnostics.configuration.radio.bandwidth_hz)/1000UL);
        draw_line(3, line.data());
        // draw_line(5, "LoRa diagnostic page");
    }
    static_cast<void>(ssd1306_display(display));
}

void diagnostics_task(void*)
{
    constexpr std::int64_t kDisplayTimeoutUs = 10LL * 1000LL * 1000LL;
    std::uint8_t page = 0;
    bool previous_pressed = false;
    bool long_press_triggered = false;
    bool woke_display_on_press = false;
    std::int64_t pressed_since_us = 0;
    std::int64_t deadline = esp_timer_get_time() + kDisplayTimeoutUs;
    std::int64_t last_render_us = 0;
    draw_diagnostics_page(page);
    while (true) {
        const bool pressed = gpio_get_level(board::tbeam_v1_2::kUserButton) == 0;
        if (pressed && !previous_pressed) {
            pressed_since_us = esp_timer_get_time();
            long_press_triggered = false;
            woke_display_on_press = deadline == 0;
            if (woke_display_on_press) {
                deadline = esp_timer_get_time() + kDisplayTimeoutUs;
                draw_diagnostics_page(page);
                last_render_us = esp_timer_get_time();
            }
        }
        if (pressed && !long_press_triggered && pressed_since_us != 0 &&
            esp_timer_get_time() - pressed_since_us >= 1500000LL &&
            diagnostics.configuration.device.role == config::DeviceRole::Transmitter) {
            remeasure_requested = true;
            long_press_triggered = true;
            deadline = esp_timer_get_time() + kDisplayTimeoutUs;
        }
        if (!pressed && previous_pressed) {
            if (!long_press_triggered && !woke_display_on_press) {
                const std::uint8_t page_count = diagnostics.configuration.device.role == config::DeviceRole::Receiver ? 3U : 2U;
                page = static_cast<std::uint8_t>((page + 1U) % page_count);
                deadline = esp_timer_get_time() + kDisplayTimeoutUs;
                draw_diagnostics_page(page);
            }
            woke_display_on_press = false;
            pressed_since_us = 0;
        }
        previous_pressed = pressed;
        if (deadline != 0 && esp_timer_get_time() >= deadline) {
            static_cast<void>(ssd1306_clear(display));
            static_cast<void>(ssd1306_display(display));
            deadline = 0;
            idle_timeout_elapsed.store(true);
        }
        if (deadline != 0 && esp_timer_get_time() - last_render_us >= 1000000LL) {
            draw_diagnostics_page(page);
            last_render_us = esp_timer_get_time();
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

}  // namespace

bool initialize()
{
    if (display != nullptr) {
        return true;
    }
    if (board::i2c_bus_handle() == nullptr) {
        ESP_LOGE(kLogTag, "I2C bus unavailable");
        return false;
    }

    ssd1306_config_t configuration{};
    configuration.bus = SSD1306_I2C;
    configuration.width = kWidth;
    configuration.height = kHeight;
    configuration.fb = nullptr;
    configuration.fb_len = 0;
    configuration.iface.i2c.port = I2C_NUM_0;
    configuration.iface.i2c.addr = board::tbeam_v1_2::kDisplayAddress;
    configuration.iface.i2c.rst_gpio = GPIO_NUM_NC;

    const esp_err_t error = ssd1306_new_i2c(&configuration, &display);
    if (error != ESP_OK) {
        ESP_LOGE(kLogTag, "SSD1306 initialization failed: %s", esp_err_to_name(error));
        display = nullptr;
        return false;
    }
    ESP_LOGI(kLogTag, "SSD1306 initialized");
    return true;
}

bool show_boot_status(
    const board::SelfTestResult& board_status,
    const board::PowerStatus& power_status,
    const battery::BatteryEstimate& battery_estimate,
    bool configuration_valid)
{
    if (!initialize() || ssd1306_clear(display) != ESP_OK) {
        return false;
    }

    std::array<char, 22> battery_line{};
    if (power_status.battery_connected) {
        if (battery_estimate.available) {
            std::snprintf(battery_line.data(),
                          battery_line.size(),
                          "Batt %.3fV ~%u%%",
                          battery_estimate.calibrated_voltage,
                          battery_estimate.percentage);
        } else {
            std::snprintf(battery_line.data(),
                          battery_line.size(),
                          "Batt %.3fV",
                          battery_estimate.calibrated_voltage);
        }
    } else {
        std::snprintf(battery_line.data(), battery_line.size(), "Battery: not found");
    }

    std::array<char, 22> rails_line{};
    std::snprintf(rails_line.data(), rails_line.size(), "LoRa:%s",
                  power_status.radio_rail_enabled ? "ON" : "OFF");

    const bool drawn =
        draw_line(0, "LoRa Tank Monitor") &&
        draw_line(1, "T-Beam V1.2 SX1278") &&
        draw_line(2, board_status.pmu == board::DevicePresence::Present ? "PMU:OK  OLED:OK" : "PMU ERROR") &&
        draw_line(3, battery_line.data()) &&
        draw_line(4, rails_line.data()) &&
        draw_line(5, configuration_valid ? "CONFIGURED" : "NEEDS SETUP") &&
        draw_line(6, "Hold button: setup");
    if (!drawn) {
        ESP_LOGE(kLogTag, "Failed to compose boot screen");
        return false;
    }

    const esp_err_t error = ssd1306_display(display);
    if (error != ESP_OK) {
        ESP_LOGE(kLogTag, "Display update failed: %s", esp_err_to_name(error));
        return false;
    }
    return true;
}

bool show_provisioning(const char* access_point_name, const char* ip_address)
{
    if (access_point_name == nullptr || ip_address == nullptr ||
        !initialize() || ssd1306_clear(display) != ESP_OK) {
        return false;
    }
    const bool drawn =
        draw_line(0, "PROVISIONING") &&
        draw_line(2, "Connect to:") &&
        draw_line(3, access_point_name) &&
        draw_line(5, "Open in browser:") &&
        draw_line(6, ip_address);
    return drawn && ssd1306_display(display) == ESP_OK;
}

bool start_diagnostics(
    const board::SelfTestResult& board_status,
    const board::PowerStatus& power_status,
    const battery::BatteryEstimate& battery_estimate,
    const config::AppConfig& configuration)
{
    if (!initialize()) return false;
    diagnostics = {board_status, power_status, battery_estimate, configuration};
    idle_timeout_elapsed.store(false);
    return xTaskCreate(diagnostics_task, "oled_diagnostics", 3072, nullptr, 1, nullptr) == pdPASS;
}

void update_tx_telemetry(const LiveTelemetry& telemetry)
{
    tx_telemetry = telemetry;
    tx_measurement_time_us = esp_timer_get_time();
}

void update_rx_telemetry(const LiveTelemetry& telemetry)
{
    const std::int64_t now = esp_timer_get_time();
    if (rx_measurement_time_us != 0) {
        const std::int64_t interval = now - rx_measurement_time_us;
        const std::uint32_t average = predicted_rx_interval_seconds();
        if (interval > 0 && (average == 0 || interval <= static_cast<std::int64_t>(average) * 2500000LL)) {
            rx_intervals_us[rx_interval_next] = interval;
            rx_interval_next = (rx_interval_next + 1U) % rx_intervals_us.size();
            if (rx_interval_count < rx_intervals_us.size()) ++rx_interval_count;
        }
    }
    rx_telemetry = telemetry;
    rx_measurement_time_us = now;
}

void update_network_health(const NetworkHealth& health)
{
    network_health = health;
}

bool take_remeasure_request()
{
    const bool requested = remeasure_requested;
    remeasure_requested = false;
    return requested;
}

bool diagnostics_idle_timeout_elapsed()
{
    return idle_timeout_elapsed.load();
}

void blank()
{
    if (display != nullptr) {
        static_cast<void>(ssd1306_clear(display));
        static_cast<void>(ssd1306_display(display));
    }
}

}  // namespace tank_monitor::display