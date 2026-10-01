#pragma once

#include "config/tank_config.hpp"

#include <cstdint>

namespace tank_monitor::tank {

enum class ValidationError : std::uint8_t {
    None,
    NonFiniteValue,
    InvalidCapacity,
    InvalidVolumeUnit,
    InvalidVolumeModel,
    InvalidOutOfRangePolicy,
    InvalidSensorRange,
    InvalidLevelRange,
    LevelAboveSensor,
    CalibrationOutsideSensorRange,
    InvalidLinearVolume,
};

struct ValidationResult {
    ValidationError error{ValidationError::None};

    [[nodiscard]] constexpr bool valid() const
    {
        return error == ValidationError::None;
    }
};

enum class MeasurementStatus : std::uint8_t {
    Valid,
    InvalidConfiguration,
    NonFiniteDistance,
    SensorOutOfRange,
    TankOutOfRange,
};

struct TankMeasurement {
    MeasurementStatus status{MeasurementStatus::InvalidConfiguration};
    double raw_distance_cm{0.0};
    double corrected_distance_cm{0.0};
    double surface_position_cm{0.0};
    double water_level_cm{0.0};
    double percentage_full{0.0};
    double volume_litres{0.0};
    double display_volume{0.0};

    [[nodiscard]] constexpr bool valid() const
    {
        return status == MeasurementStatus::Valid;
    }
};

[[nodiscard]] ValidationResult validate(const config::TankConfig& configuration);

[[nodiscard]] TankMeasurement calculate(
    const config::TankConfig& configuration,
    double raw_distance_cm);

[[nodiscard]] double convert_litres(double litres, config::VolumeUnit unit);

}  // namespace tank_monitor::tank