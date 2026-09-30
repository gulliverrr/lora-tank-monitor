#include "battery/battery_estimator.hpp"
#include "blynk/blynk_manager.hpp"
#include "board/board.hpp"
#include "board/power_manager.hpp"
#include "board/tbeam_v1_2.hpp"
#include "config/app_config.hpp"
#include "config_validation/config_validator.hpp"
#include "display/display_manager.hpp"
#include "driver/gpio.h"
#include "lora/lora_manager.hpp"
#include "protocol/protocol_codec.hpp"
#include "provisioning/provisioning_manager.hpp"
#include "sensor/aj_sr04m_sensor.hpp"
#include "storage/config_store.hpp"
#include "tank/tank_calculator.hpp"

#include "esp_chip_info.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace {

constexpr char kLogTag[] = "tank_monitor";
constexpr char kFirmwareName[] = "LoRa Tank Monitor";

std::uint32_t bounded_u32(double value)
{
    if (!std::isfinite(value) || value <= 0.0) return 0;
    constexpr double kMaximum = static_cast<double>(std::numeric_limits<std::uint32_t>::max());
    return static_cast<std::uint32_t>(std::min(std::round(value), kMaximum));
}

struct ReceiverTaskContext {
    tank_monitor::lora::LoRaManager* radio{nullptr};
    tank_monitor::blynk::BlynkManager* blynk{nullptr};
    tank_monitor::config::AppConfig* configuration{nullptr};
    tank_monitor::storage::ConfigStore* config_store{nullptr};
    std::uint16_t battery_millivolts{0};
};

tank_monitor::lora::LoRaManager* active_tx_radio{nullptr};
tank_monitor::sensor::AjSr04mSensor* active_tx_sensor{nullptr};
tank_monitor::config::AppConfig* active_tx_configuration{nullptr};
tank_monitor::storage::ConfigStore* active_tx_store{nullptr};
std::uint16_t active_tx_battery_millivolts{0};
std::uint8_t active_tx_battery_percentage{0};

bool paired_sender_matches_or_binds(
    tank_monitor::config::AppConfig& configuration,
    tank_monitor::storage::ConfigStore& config_store,
    const tank_monitor::protocol::Header& header)
{
    for (const auto& peer : configuration.pairing.peers) {
        if (peer.enabled) {
            return peer.node_id == header.sender_id && peer.tank_id == header.tank_id;
        }
    }

    auto& peer = configuration.pairing.peers.front();
    const auto previous_peer = peer;
    peer = {};
    peer.enabled = true;
    peer.node_id = header.sender_id;
    peer.tank_id = header.tank_id;
    std::snprintf(peer.label.data(), peer.label.size(), "Paired TX");
    if (config_store.save(configuration) != tank_monitor::storage::StoreStatus::Ok) {
        peer = previous_peer;
        ESP_LOGE(kLogTag, "Failed to persist LoRa sender pairing");
        return false;
    }
    ESP_LOGI(kLogTag, "Paired LoRa sender: %016llx tank=%lu",
             static_cast<unsigned long long>(header.sender_id),
             static_cast<unsigned long>(header.tank_id));
    return true;
}

void run_radio_link_checkpoint(
    tank_monitor::lora::LoRaManager& radio,
    tank_monitor::blynk::BlynkManager* blynk,
    tank_monitor::config::AppConfig& configuration,
    tank_monitor::storage::ConfigStore& config_store,
    std::uint16_t battery_millivolts,
    const tank_monitor::sensor::SensorReading& sensor_reading,
    const tank_monitor::tank::TankMeasurement& tank_measurement)
{
    using tank_monitor::config::DeviceRole;
    using tank_monitor::protocol::CodecError;
    using tank_monitor::protocol::FieldTag;
    using tank_monitor::protocol::MessageType;

    if (configuration.device.role == DeviceRole::Transmitter) {
        if (sensor_reading.valid() && tank_measurement.valid()) {
            ESP_LOGI(kLogTag,
                     "TX measurement: distance=%lu mm level=%.1f cm fill=%.2f%% volume=%.3f L",
                     static_cast<unsigned long>(sensor_reading.distance_millimetres),
                     tank_measurement.water_level_cm,
                     tank_measurement.percentage_full,
                     tank_measurement.volume_litres);
        } else {
            ESP_LOGW(kLogTag, "TX measurement unavailable: sensor_status=%u tank_status=%u",
                     static_cast<unsigned int>(sensor_reading.status),
                     static_cast<unsigned int>(tank_measurement.status));
        }
        tank_monitor::protocol::PayloadBuilder payload;
        if (payload.add_u16(FieldTag::BatteryMillivolts, battery_millivolts) != CodecError::None ||
            payload.add_u8(FieldTag::SensorStatus,
                           static_cast<std::uint8_t>(sensor_reading.status)) != CodecError::None) {
            ESP_LOGE(kLogTag, "LoRa checkpoint payload construction failed");
            return;
        }

        if (sensor_reading.valid()) {
            if (payload.add_u32(FieldTag::DistanceCentimetres,
                                bounded_u32(static_cast<double>(sensor_reading.distance_millimetres) / 10.0)) != CodecError::None) {
                ESP_LOGE(kLogTag, "LoRa distance payload construction failed");
                return;
            }
        }
        if (tank_measurement.valid()) {
            if (payload.add_u32(FieldTag::LevelCentimetres,
                                bounded_u32(tank_measurement.water_level_cm)) != CodecError::None ||
                payload.add_u32(FieldTag::PercentageBasisPoints,
                                bounded_u32(tank_measurement.percentage_full * 100.0)) != CodecError::None ||
                payload.add_u32(FieldTag::VolumeUnits,
                                bounded_u32(tank_measurement.display_volume)) != CodecError::None ||
                payload.add_u8(FieldTag::VolumeUnit,
                               static_cast<std::uint8_t>(configuration.tank.display_unit)) != CodecError::None) {
                ESP_LOGE(kLogTag, "LoRa tank payload construction failed");
                return;
            }
        }

        tank_monitor::protocol::Packet packet{};
        packet.header.message_type = MessageType::Measurement;
        packet.header.sender_id = configuration.device.node_id;
        packet.header.receiver_id = 0;
        packet.header.tank_id = configuration.tank.tank_id;
        packet.header.boot_nonce = esp_random();
        packet.header.sequence = 0;
        packet.payload = payload.payload();

        tank_monitor::protocol::EncodedFrame frame{};
        if (tank_monitor::protocol::encode(packet, frame) != CodecError::None) {
            ESP_LOGE(kLogTag, "LoRa checkpoint frame encoding failed");
            return;
        }

        const auto transmitted = radio.transmit(frame);
        if (!transmitted.sent()) {
            ESP_LOGE(kLogTag, "LoRa measurement transmit failed: error=%d", transmitted.driver_error);
        } else {
            ESP_LOGI(kLogTag, "LoRa measurement sent: bytes=%u payload_register=%u",
                     static_cast<unsigned int>(frame.size),
                     static_cast<unsigned int>(transmitted.programmed_payload_length));
        }
        return;
    }

    if (configuration.device.role != DeviceRole::Receiver) return;

    const auto received = radio.receive(configuration.radio.receive_timeout_ms);
    if (!received.received()) {
        if (received.driver_error != RADIOLIB_ERR_RX_TIMEOUT) {
            ESP_LOGE(kLogTag, "LoRa measurement receive failed: error=%d", received.driver_error);
        }
        return;
    }

    const auto decoded = tank_monitor::protocol::decode(
        received.frame.bytes.data(), received.frame.size);
    if (!decoded.valid()) {
        ESP_LOGW(kLogTag, "Rejected malformed LoRa frame: codec_error=%u",
                 static_cast<unsigned int>(decoded.error));
        return;
    }
    const auto& header = decoded.packet.header;
    if (header.message_type != MessageType::Measurement || header.sender_id == 0 ||
        (header.receiver_id != 0 && header.receiver_id != configuration.device.node_id) ||
        header.tank_id != configuration.tank.tank_id) {
        ESP_LOGW(kLogTag, "Rejected unrelated LoRa frame");
        return;
    }

    if (!paired_sender_matches_or_binds(configuration, config_store, header)) {
        ESP_LOGW(kLogTag, "Rejected LoRa frame from unpaired sender=%016llx",
                 static_cast<unsigned long long>(header.sender_id));
        return;
    }

    std::uint16_t sender_battery_millivolts = 0;
    std::uint8_t sensor_status = 0;
    std::uint32_t distance_centimetres = 0;
    std::uint32_t level_centimetres = 0;
    std::uint32_t percentage_basis_points = 0;
    std::uint32_t volume_units = 0;
    std::uint8_t volume_unit = 0;
    const auto battery_error = tank_monitor::protocol::find_u16(
        decoded.packet.payload, FieldTag::BatteryMillivolts, sender_battery_millivolts);
    const auto sensor_error = tank_monitor::protocol::find_u8(
        decoded.packet.payload, FieldTag::SensorStatus, sensor_status);
    const auto distance_error = tank_monitor::protocol::find_u32(
        decoded.packet.payload, FieldTag::DistanceCentimetres, distance_centimetres);
    const auto level_error = tank_monitor::protocol::find_u32(
        decoded.packet.payload, FieldTag::LevelCentimetres, level_centimetres);
    const auto percentage_error = tank_monitor::protocol::find_u32(
        decoded.packet.payload, FieldTag::PercentageBasisPoints, percentage_basis_points);
    const auto volume_error = tank_monitor::protocol::find_u32(
           decoded.packet.payload, FieldTag::VolumeUnits, volume_units);
    const auto volume_unit_error = tank_monitor::protocol::find_u8(
        decoded.packet.payload, FieldTag::VolumeUnit, volume_unit);
    ESP_LOGI(kLogTag,
             "LoRa measurement received: sender=%016llx bytes=%u radio_bytes=%u RSSI=%.1f dBm SNR=%.1f dB battery=%s%u mV sensor=%s%u distance=%s%lu cm level=%s%lu cm fill=%s%lu.%02lu%% volume=%s%lu unit=%s%u",
             static_cast<unsigned long long>(header.sender_id),
             static_cast<unsigned int>(received.frame.size),
             static_cast<unsigned int>(received.radio_reported_length),
             received.rssi_dbm,
             received.snr_db,
             battery_error == CodecError::None ? "" : "unavailable/",
             sender_battery_millivolts,
             sensor_error == CodecError::None ? "" : "unavailable/",
             sensor_status,
             distance_error == CodecError::None ? "" : "unavailable/",
             static_cast<unsigned long>(distance_centimetres),
             level_error == CodecError::None ? "" : "unavailable/",
             static_cast<unsigned long>(level_centimetres),
             percentage_error == CodecError::None ? "" : "unavailable/",
             static_cast<unsigned long>(percentage_basis_points / 100U),
             static_cast<unsigned long>(percentage_basis_points % 100U),
             volume_error == CodecError::None ? "" : "unavailable/",
             static_cast<unsigned long>(volume_units),
             volume_unit_error == CodecError::None ? "" : "unavailable/",
             volume_unit);

    if (blynk != nullptr) {
        blynk->publish({
            .distance_centimetres = distance_centimetres,
            .level_centimetres = level_centimetres,
            .percentage_basis_points = percentage_basis_points,
            .volume_litres = volume_units,
            .battery_millivolts = sender_battery_millivolts,
            .sensor_status = sensor_status,
            .rssi_dbm = static_cast<std::int16_t>(received.rssi_dbm),
            .snr_tenths_db = static_cast<std::int16_t>(received.snr_db * 10.0F),
        });
    }
    tank_monitor::display::update_rx_telemetry({
        .battery_millivolts = sender_battery_millivolts,
        .battery_percentage = static_cast<std::uint8_t>(std::clamp(
            100.0 * (static_cast<double>(sender_battery_millivolts) / 1000.0 - 3.2), 0.0, 100.0)),
        .distance_centimetres = distance_centimetres,
        .water_percentage_basis_points = static_cast<std::uint16_t>(percentage_basis_points),
        .rssi_dbm = static_cast<std::int16_t>(received.rssi_dbm),
        .snr_tenths_db = static_cast<std::int16_t>(received.snr_db * 10.0F),
        .valid = true,
    });

}

void receiver_task(void* argument)
{
    const auto* context = static_cast<ReceiverTaskContext*>(argument);
    if (context == nullptr || context->radio == nullptr || context->configuration == nullptr ||
        context->config_store == nullptr) {
        vTaskDelete(nullptr);
        return;
    }

    const tank_monitor::sensor::SensorReading no_sensor_reading{};
    const tank_monitor::tank::TankMeasurement no_tank_measurement{};
    while (true) {
        run_radio_link_checkpoint(
            *context->radio,
            context->blynk,
            *context->configuration,
            *context->config_store,
            context->battery_millivolts,
            no_sensor_reading,
            no_tank_measurement);
        if (context->blynk != nullptr) {
            const auto health = context->blynk->health();
            tank_monitor::display::update_network_health({
                .wifi_connected = health.wifi_connected,
                .blynk_online = health.blynk_online,
                .wifi_rssi_dbm = health.wifi_rssi_dbm,
            });
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

}  // namespace

extern "C" void app_main()
{
    static_cast<void>(gpio_deep_sleep_hold_dis());
    static_cast<void>(gpio_hold_dis(tank_monitor::board::tbeam_v1_2::kRadioReset));
    static_cast<void>(gpio_hold_dis(tank_monitor::board::tbeam_v1_2::kSensorPowerEnable));

    esp_chip_info_t chip_info{};
    esp_chip_info(&chip_info);

    ESP_LOGI(kLogTag, "%s starting", kFirmwareName);
    ESP_LOGI(kLogTag, "ESP32 cores=%d revision=%d", chip_info.cores, chip_info.revision);
    ESP_LOGI(kLogTag, "Reset reason=%d", static_cast<int>(esp_reset_reason()));

    static tank_monitor::storage::ConfigStore config_store;
    const auto store_status = config_store.initialize();
    const auto stored = store_status == tank_monitor::storage::StoreStatus::Ok
        ? config_store.load()
        : tank_monitor::storage::LoadResult{};
    static tank_monitor::config::AppConfig configuration;
    configuration = stored.loaded() ? stored.configuration : tank_monitor::config::default_config();
    const auto validation = tank_monitor::config_validation::validate(configuration);

    const auto board_result = tank_monitor::board::run_self_test();
    if (!board_result.initialized) {
        ESP_LOGE(kLogTag, "Board self-test initialization failed");
    }
    const bool woke_from_button = esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT1;
    const bool provisioning_requested = !validation.valid() ||
        (board_result.user_button_pressed && !woke_from_button);
    if (provisioning_requested) {
        ESP_LOGI(kLogTag, "Provisioning mode active; transmitter deep sleep is disabled");
    }
    static tank_monitor::sensor::AjSr04mSensor sensor;
    if (!provisioning_requested && configuration.device.role == tank_monitor::config::DeviceRole::Transmitter) {
        static_cast<void>(sensor.set_power_enabled(false));
    }
    if (!provisioning_requested && !tank_monitor::board::set_radio_rail_enabled(true)) {
        ESP_LOGE(kLogTag, "Failed to restore LoRa rail after wake");
    }
    const auto power_status = tank_monitor::board::read_power_status();
    if (!power_status.initialized) {
        ESP_LOGE(kLogTag, "Power telemetry unavailable");
    }
    const auto battery_estimate = tank_monitor::battery::estimate(
        power_status.battery_millivolts,
        configuration.battery);
    if (power_status.battery_connected && battery_estimate.available) {
        ESP_LOGI(kLogTag,
                 "Battery estimate: %.3f V, ~%u%% (voltage-derived)",
                 battery_estimate.calibrated_voltage,
                 battery_estimate.percentage);
    }
    if (!provisioning_requested) {
        static tank_monitor::lora::LoRaManager lora;
        const auto radio_status = lora.initialize(
            configuration.radio,
            power_status.radio_rail_enabled);
        if (!radio_status.ready()) {
            ESP_LOGE(kLogTag, "LoRa initialization unavailable: state=%u error=%d",
                     static_cast<unsigned int>(radio_status.state),
                     radio_status.driver_error);
        } else {
            if (configuration.device.role == tank_monitor::config::DeviceRole::Transmitter) {
                tank_monitor::sensor::SensorReading sensor_reading{};
                tank_monitor::tank::TankMeasurement tank_measurement{};
                if (sensor.set_power_enabled(true) && sensor.initialize()) {
                    vTaskDelay(pdMS_TO_TICKS(configuration.sensor.power_warmup_ms));
                    sensor_reading = sensor.measure(configuration.sensor.trigger_timeout_us);
                    if (sensor_reading.valid()) {
                        tank_measurement = tank_monitor::tank::calculate(
                            configuration.tank,
                            static_cast<double>(sensor_reading.distance_millimetres) / 10.0);
                    }
                }
                static_cast<void>(sensor.set_power_enabled(false));
                tank_monitor::display::update_tx_telemetry({
                    .battery_millivolts = power_status.battery_millivolts,
                    .battery_percentage = battery_estimate.percentage,
                    .distance_centimetres = bounded_u32(static_cast<double>(sensor_reading.distance_millimetres) / 10.0),
                    .water_percentage_basis_points = static_cast<std::uint16_t>(tank_measurement.percentage_full * 100.0),
                    .valid = sensor_reading.valid(),
                });
                active_tx_radio = &lora;
                active_tx_sensor = &sensor;
                active_tx_configuration = &configuration;
                active_tx_store = &config_store;
                active_tx_battery_millivolts = power_status.battery_millivolts;
                active_tx_battery_percentage = battery_estimate.percentage;
                run_radio_link_checkpoint(
                    lora,
                    nullptr,
                    configuration,
                    config_store,
                    power_status.battery_millivolts,
                    sensor_reading,
                    tank_measurement);
                static_cast<void>(sensor.set_power_enabled(false));
            } else {
                static tank_monitor::blynk::BlynkManager blynk;
                tank_monitor::blynk::BlynkManager* blynk_manager = nullptr;
                if (configuration.blynk.enabled) {
                    if (blynk.start(configuration.wifi, configuration.blynk)) {
                        blynk_manager = &blynk;
                    } else {
                        ESP_LOGE(kLogTag, "Blynk manager failed to start");
                    }
                }
                static ReceiverTaskContext receiver_context;
                receiver_context.radio = &lora;
                receiver_context.blynk = blynk_manager;
                receiver_context.configuration = &configuration;
                receiver_context.config_store = &config_store;
                receiver_context.battery_millivolts = power_status.battery_millivolts;
                ESP_LOGI(kLogTag, "LoRa receiver listening continuously");
                if (xTaskCreate(receiver_task, "lora_receiver", 6144, &receiver_context, 1, nullptr) != pdPASS) {
                    ESP_LOGE(kLogTag, "LoRa receiver task creation failed");
                }
            }
        }
    }
    if (!tank_monitor::display::show_boot_status(
            board_result, power_status, battery_estimate, validation.valid())) {
        ESP_LOGE(kLogTag, "OLED boot diagnostics unavailable");
    }
    if (!provisioning_requested &&
        !tank_monitor::display::start_diagnostics(
            board_result, power_status, battery_estimate, configuration)) {
        ESP_LOGE(kLogTag, "OLED diagnostics task unavailable");
    }
    ESP_LOGI(kLogTag, "Configuration schema=%lu", static_cast<unsigned long>(configuration.schema_version));
    if (!provisioning_requested) {
        ESP_LOGI(kLogTag, "Persistent configuration is valid: generation=%lu role=%u tank=%s",
                 static_cast<unsigned long>(configuration.generation),
                 static_cast<unsigned int>(configuration.device.role),
                 configuration.tank.tank_name.data());
    } else {
        if (validation.valid()) {
            ESP_LOGW(kLogTag, "Provisioning requested by local button");
        }
        ESP_LOGW(kLogTag,
                 "Configuration requires provisioning (reason=%u)",
                 static_cast<unsigned int>(validation.error));
        static tank_monitor::provisioning::ProvisioningManager provisioning;
        if (provisioning.start(board_result.hardware_node_id, configuration, config_store)) {
            static_cast<void>(tank_monitor::display::show_provisioning(
                provisioning.access_point_name(), provisioning.ip_address()));
        } else {
            ESP_LOGE(kLogTag, "Provisioning transport failed to start");
        }
    }
    if (!provisioning_requested &&
        configuration.device.role == tank_monitor::config::DeviceRole::Transmitter) {
        while (!tank_monitor::display::diagnostics_idle_timeout_elapsed()) {
            if (tank_monitor::display::take_remeasure_request() && active_tx_radio != nullptr &&
                active_tx_sensor != nullptr && active_tx_configuration != nullptr && active_tx_store != nullptr) {
                if (!active_tx_sensor->set_power_enabled(true)) {
                    ESP_LOGE(kLogTag, "Failed to enable ultrasonic sensor on GPIO14");
                    continue;
                }
                vTaskDelay(pdMS_TO_TICKS(active_tx_configuration->sensor.power_warmup_ms));
                const auto sensor_reading = active_tx_sensor->measure(active_tx_configuration->sensor.trigger_timeout_us);
                static_cast<void>(active_tx_sensor->set_power_enabled(false));
                tank_monitor::tank::TankMeasurement tank_measurement{};
                if (sensor_reading.valid()) {
                    tank_measurement = tank_monitor::tank::calculate(
                        active_tx_configuration->tank,
                        static_cast<double>(sensor_reading.distance_millimetres) / 10.0);
                }
                tank_monitor::display::update_tx_telemetry({
                    .battery_millivolts = active_tx_battery_millivolts,
                    .battery_percentage = active_tx_battery_percentage,
                    .distance_centimetres = bounded_u32(static_cast<double>(sensor_reading.distance_millimetres) / 10.0),
                    .water_percentage_basis_points = static_cast<std::uint16_t>(tank_measurement.percentage_full * 100.0),
                    .valid = sensor_reading.valid(),
                });
                run_radio_link_checkpoint(*active_tx_radio, nullptr, *active_tx_configuration,
                                          *active_tx_store, active_tx_battery_millivolts,
                                          sensor_reading, tank_measurement);
            }
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        if (active_tx_sensor != nullptr) static_cast<void>(active_tx_sensor->set_power_enabled(false));
        tank_monitor::display::blank();
        static_cast<void>(tank_monitor::board::set_radio_rail_enabled(false));
        static_cast<void>(gpio_set_level(tank_monitor::board::tbeam_v1_2::kSensorPowerEnable, 0));
        static_cast<void>(gpio_hold_en(tank_monitor::board::tbeam_v1_2::kSensorPowerEnable));
        static_cast<void>(gpio_deep_sleep_hold_en());
        const std::uint64_t sleep_microseconds =
            static_cast<std::uint64_t>(configuration.sensor.measurement_interval_seconds) * 1'000'000ULL;
        if (esp_sleep_enable_timer_wakeup(sleep_microseconds) == ESP_OK &&
            esp_sleep_enable_ext1_wakeup(1ULL << tank_monitor::board::tbeam_v1_2::kUserButton,
                                         ESP_EXT1_WAKEUP_ALL_LOW) == ESP_OK) {
            ESP_LOGI(kLogTag, "TX entering deep sleep for %lu seconds",
                     static_cast<unsigned long>(configuration.sensor.measurement_interval_seconds));
            esp_deep_sleep_start();
        }
        ESP_LOGE(kLogTag, "TX deep sleep scheduling failed");
    }
}