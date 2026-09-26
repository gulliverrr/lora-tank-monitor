#pragma once

#include "config/app_config.hpp"

#include <cstdint>

namespace tank_monitor::battery {

struct BatteryEstimate {
    bool available{false};
    double calibrated_voltage{0.0};
    std::uint8_t percentage{0};
};

[[nodiscard]] BatteryEstimate estimate(
    std::uint16_t measured_millivolts,
    const config::BatteryConfig& configuration);

}  // namespace tank_monitor::battery