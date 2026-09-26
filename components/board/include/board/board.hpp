#pragma once

#include "driver/i2c_master.h"

#include <cstdint>

namespace tank_monitor::board {

enum class DevicePresence : std::uint8_t {
    Unknown,
    Absent,
    Present,
};

struct SelfTestResult {
    bool initialized{false};
    DevicePresence pmu{DevicePresence::Unknown};
    DevicePresence display{DevicePresence::Unknown};
    bool user_button_pressed{false};
    std::uint64_t hardware_node_id{0};
    std::uint32_t flash_size_bytes{0};
    std::uint32_t psram_size_bytes{0};
};

[[nodiscard]] const char* profile_name();
[[nodiscard]] i2c_master_bus_handle_t i2c_bus_handle();
[[nodiscard]] SelfTestResult run_self_test();

}  // namespace tank_monitor::board