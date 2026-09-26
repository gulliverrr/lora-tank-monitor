#include "sensor/ultrasonic_math.hpp"

#include <limits>

namespace tank_monitor::sensor {

std::uint32_t echo_microseconds_to_millimetres(std::uint32_t echo_microseconds)
{
    constexpr std::uint64_t speed_of_sound_metres_per_second = 343;
    constexpr std::uint64_t round_trip_divisor = 2000;
    const std::uint64_t distance =
        (static_cast<std::uint64_t>(echo_microseconds) * speed_of_sound_metres_per_second +
         round_trip_divisor / 2U) /
        round_trip_divisor;
    if (distance > std::numeric_limits<std::uint32_t>::max()) {
        return std::numeric_limits<std::uint32_t>::max();
    }
    return static_cast<std::uint32_t>(distance);
}

}  // namespace tank_monitor::sensor