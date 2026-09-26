#include "tank/tank_calculator.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace tank_monitor::tank {
namespace {

constexpr double kLitresPerUsGallon = 3.785411784;
constexpr double kLitresPerImperialGallon = 4.54609;

bool all_finite(const config::TankConfig& configuration)
{
    const std::array values{
        configuration.capacity_litres,
        configuration.volume_litres_per_centimetre,
        configuration.sensor_reference_height_cm,
        configuration.minimum_sensor_distance_cm,
        configuration.maximum_sensor_distance_cm,
        configuration.empty_level_cm,
        configuration.full_level_cm,
        configuration.sensor_offset_cm,
    };

    return std::all_of(values.begin(), values.end(), [](double value) {
        return std::isfinite(value);
    });
}

}  // namespace

ValidationResult validate(const config::TankConfig& configuration)
{
    if (configuration.tank_id == 0) {
        return {ValidationError::InvalidTankId};
    }
    if (!all_finite(configuration)) {
        return {ValidationError::NonFiniteValue};
    }
    if (configuration.capacity_litres <= 0.0) {
        return {ValidationError::InvalidCapacity};
    }
    if (configuration.display_unit != config::VolumeUnit::Litres &&
        configuration.display_unit != config::VolumeUnit::UsGallons &&
        configuration.display_unit != config::VolumeUnit::ImperialGallons) {
        return {ValidationError::InvalidVolumeUnit};
    }
    if (configuration.volume_model != config::VolumeModel::CapacityFromPercent &&
        configuration.volume_model != config::VolumeModel::LinearPerCentimetre) {
        return {ValidationError::InvalidVolumeModel};
    }
    if (configuration.out_of_range_policy != config::OutOfRangePolicy::Reject &&
        configuration.out_of_range_policy != config::OutOfRangePolicy::Clamp &&
        configuration.out_of_range_policy != config::OutOfRangePolicy::Report) {
        return {ValidationError::InvalidOutOfRangePolicy};
    }
    if (configuration.minimum_sensor_distance_cm < 0.0 ||
        configuration.maximum_sensor_distance_cm <= configuration.minimum_sensor_distance_cm) {
        return {ValidationError::InvalidSensorRange};
    }
    if (configuration.empty_level_cm < 0.0 ||
        configuration.full_level_cm <= configuration.empty_level_cm) {
        return {ValidationError::InvalidLevelRange};
    }
    if (configuration.full_level_cm > configuration.sensor_reference_height_cm) {
        return {ValidationError::LevelAboveSensor};
    }

    const double distance_at_empty =
        configuration.sensor_reference_height_cm - configuration.empty_level_cm;
    const double distance_at_full =
        configuration.sensor_reference_height_cm - configuration.full_level_cm;
    if (distance_at_full < configuration.minimum_sensor_distance_cm ||
        distance_at_empty > configuration.maximum_sensor_distance_cm) {
        return {ValidationError::CalibrationOutsideSensorRange};
    }
    if (configuration.volume_model == config::VolumeModel::LinearPerCentimetre &&
        configuration.volume_litres_per_centimetre <= 0.0) {
        return {ValidationError::InvalidLinearVolume};
    }

    return {};
}

TankMeasurement calculate(const config::TankConfig& configuration, double raw_distance_cm)
{
    TankMeasurement result{};
    result.raw_distance_cm = raw_distance_cm;

    if (!validate(configuration).valid()) {
        return result;
    }
    if (!std::isfinite(raw_distance_cm)) {
        result.status = MeasurementStatus::NonFiniteDistance;
        return result;
    }

    result.corrected_distance_cm = raw_distance_cm + configuration.sensor_offset_cm;
    if (result.corrected_distance_cm < configuration.minimum_sensor_distance_cm ||
        result.corrected_distance_cm > configuration.maximum_sensor_distance_cm) {
        result.status = MeasurementStatus::SensorOutOfRange;
        return result;
    }

    const double distance_at_empty =
        configuration.sensor_reference_height_cm - configuration.empty_level_cm;
    const double distance_at_full =
        configuration.sensor_reference_height_cm - configuration.full_level_cm;
    double effective_distance = result.corrected_distance_cm;
    if (effective_distance < distance_at_full || effective_distance > distance_at_empty) {
        if (configuration.out_of_range_policy == config::OutOfRangePolicy::Reject) {
            result.status = MeasurementStatus::TankOutOfRange;
            return result;
        }
        effective_distance = std::clamp(effective_distance, distance_at_full, distance_at_empty);
    }

    result.surface_position_cm =
        configuration.sensor_reference_height_cm - result.corrected_distance_cm;
    const double calibrated_surface_position =
        configuration.sensor_reference_height_cm - effective_distance;
    const double level_span = configuration.full_level_cm - configuration.empty_level_cm;
    result.water_level_cm = calibrated_surface_position;
    result.percentage_full =
        100.0 * (calibrated_surface_position - configuration.empty_level_cm) / level_span;

    if (configuration.volume_model == config::VolumeModel::CapacityFromPercent) {
        result.volume_litres =
            configuration.capacity_litres * result.percentage_full / 100.0;
    } else {
        result.volume_litres =
            configuration.volume_litres_per_centimetre *
            (calibrated_surface_position - configuration.empty_level_cm);
    }
    result.display_volume = convert_litres(result.volume_litres, configuration.display_unit);
    result.status = MeasurementStatus::Valid;
    return result;
}

double convert_litres(double litres, config::VolumeUnit unit)
{
    switch (unit) {
    case config::VolumeUnit::Litres:
        return litres;
    case config::VolumeUnit::UsGallons:
        return litres / kLitresPerUsGallon;
    case config::VolumeUnit::ImperialGallons:
        return litres / kLitresPerImperialGallon;
    }
    return litres;
}

}  // namespace tank_monitor::tank