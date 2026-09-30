#pragma once

#include "battery/battery_estimator.hpp"
#include "board/board.hpp"
#include "board/power_manager.hpp"
#include "config/app_config.hpp"

namespace tank_monitor::display {

struct LiveTelemetry {
    std::uint16_t battery_millivolts{0};
    std::uint8_t battery_percentage{0};
    std::uint32_t distance_centimetres{0};
    std::uint16_t water_percentage_basis_points{0};
    std::int16_t rssi_dbm{0};
    std::int16_t snr_tenths_db{0};
    bool valid{false};
};

struct NetworkHealth {
    bool wifi_connected{false};
    bool blynk_online{false};
    std::int8_t wifi_rssi_dbm{0};
};

[[nodiscard]] bool initialize();
[[nodiscard]] bool show_boot_status(
    const board::SelfTestResult& board_status,
    const board::PowerStatus& power_status,
    const battery::BatteryEstimate& battery_estimate,
    bool configuration_valid);
[[nodiscard]] bool show_provisioning(const char* access_point_name, const char* ip_address);
[[nodiscard]] bool start_diagnostics(
    const board::SelfTestResult& board_status,
    const board::PowerStatus& power_status,
    const battery::BatteryEstimate& battery_estimate,
    const config::AppConfig& configuration);
void update_tx_telemetry(const LiveTelemetry& telemetry);
void update_rx_telemetry(const LiveTelemetry& telemetry);
void update_network_health(const NetworkHealth& health);
[[nodiscard]] bool take_remeasure_request();
[[nodiscard]] bool diagnostics_idle_timeout_elapsed();
void blank();

}  // namespace tank_monitor::display