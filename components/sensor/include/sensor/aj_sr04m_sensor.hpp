#pragma once

#include <cstdint>

namespace tank_monitor::sensor {

enum class SensorStatus : std::uint8_t {
    Valid,
    NotInitialized,
    Busy,
    Timeout,
    InvalidPulse,
};

struct SensorReading {
    SensorStatus status{SensorStatus::NotInitialized};
    std::uint32_t echo_microseconds{0};
    std::uint32_t distance_millimetres{0};

    [[nodiscard]] constexpr bool valid() const
    {
        return status == SensorStatus::Valid;
    }
};

class AjSr04mSensor {
public:
    [[nodiscard]] bool initialize();
    [[nodiscard]] SensorReading measure(std::uint32_t timeout_microseconds);

private:
    bool initialized_{false};
    bool measuring_{false};
};

}  // namespace tank_monitor::sensor