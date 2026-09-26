#pragma once

#include "config/app_config.hpp"

#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include <cstdint>

namespace tank_monitor::blynk {

struct Telemetry {
    std::uint32_t distance_centimetres{0};
    std::uint32_t level_centimetres{0};
    std::uint32_t percentage_basis_points{0};
    std::uint32_t volume_litres{0};
    std::uint16_t battery_millivolts{0};
    std::uint8_t sensor_status{0};
    std::int16_t rssi_dbm{0};
    std::int16_t snr_tenths_db{0};
};

struct Health {
    bool wifi_connected{false};
    bool blynk_online{false};
    std::int8_t wifi_rssi_dbm{0};
};

class BlynkManager {
public:
    [[nodiscard]] bool start(const config::WifiConfig& wifi, const config::BlynkConfig& blynk);
    void publish(const Telemetry& telemetry);
    [[nodiscard]] Health health() const;

private:
    static void wifi_event_handler(void* argument, esp_event_base_t base, std::int32_t id, void* data);
    static void task_entry(void* argument);
    void run();

    config::WifiConfig wifi_{};
    config::BlynkConfig configuration_{};
    Telemetry telemetry_{};
    portMUX_TYPE telemetry_lock_ = portMUX_INITIALIZER_UNLOCKED;
    EventGroupHandle_t events_{nullptr};
    std::uint32_t telemetry_generation_{0};
    Health health_{};
    bool started_{false};
};

}  // namespace tank_monitor::blynk