#pragma once

#include <array>
#include <cstdint>

namespace tank_monitor::config {

enum class VolumeUnit : std::uint8_t {
    Litres,
    UsGallons,
    ImperialGallons,
};

enum class VolumeModel : std::uint8_t {
    CapacityFromPercent,
    LinearPerCentimetre,
};

enum class OutOfRangePolicy : std::uint8_t {
    Reject,
    Clamp,
    Report,
};

struct TankConfig {
    std::array<char, 33> tank_name{};
    double capacity_litres{0.0};
    VolumeUnit display_unit{VolumeUnit::Litres};
    VolumeModel volume_model{VolumeModel::CapacityFromPercent};
    double volume_litres_per_centimetre{0.0};

    double sensor_reference_height_cm{0.0};
    double minimum_sensor_distance_cm{0.0};
    double maximum_sensor_distance_cm{0.0};
    double empty_level_cm{0.0};
    double full_level_cm{0.0};
    double sensor_offset_cm{0.0};
    OutOfRangePolicy out_of_range_policy{OutOfRangePolicy::Reject};
};

}  // namespace tank_monitor::config