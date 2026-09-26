#pragma once

#include <cstdint>

namespace tank_monitor::sensor {

[[nodiscard]] std::uint32_t echo_microseconds_to_millimetres(
    std::uint32_t echo_microseconds);

}  // namespace tank_monitor::sensor