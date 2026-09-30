#pragma once

#include <cstdint>

namespace tank_monitor::board {

struct PowerStatus {
    bool initialized{false};
    bool battery_connected{false};
    std::uint16_t battery_millivolts{0};
    bool radio_rail_enabled{false};
    std::uint16_t radio_rail_millivolts{0};
    bool gps_rail_enabled{false};
    std::uint16_t gps_rail_millivolts{0};
};

[[nodiscard]] bool initialize_power_manager();
[[nodiscard]] bool set_radio_rail_enabled(bool enabled);
[[nodiscard]] PowerStatus read_power_status();

}  // namespace tank_monitor::board