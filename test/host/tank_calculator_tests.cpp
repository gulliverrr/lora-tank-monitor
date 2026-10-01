#include "tank/tank_calculator.hpp"

#include <cmath>
#include <iostream>
#include <limits>

namespace {

using tank_monitor::config::OutOfRangePolicy;
using tank_monitor::config::TankConfig;
using tank_monitor::config::VolumeModel;
using tank_monitor::config::VolumeUnit;
using tank_monitor::tank::MeasurementStatus;
using tank_monitor::tank::ValidationError;

int failures = 0;

void expect(bool condition, const char* description)
{
    if (!condition) {
        std::cerr << "FAIL: " << description << '\n';
        ++failures;
    }
}

void expect_near(double actual, double expected, const char* description)
{
    constexpr double tolerance = 1.0e-6;
    expect(std::abs(actual - expected) <= tolerance, description);
}

TankConfig valid_configuration()
{
    TankConfig configuration{};
    configuration.capacity_litres = 1000.0;
    configuration.display_unit = VolumeUnit::Litres;
    configuration.volume_model = VolumeModel::CapacityFromPercent;
    configuration.volume_litres_per_centimetre = 5.0;
    configuration.sensor_reference_height_cm = 220.0;
    configuration.minimum_sensor_distance_cm = 20.0;
    configuration.maximum_sensor_distance_cm = 210.0;
    configuration.empty_level_cm = 20.0;
    configuration.full_level_cm = 180.0;
    configuration.sensor_offset_cm = 0.0;
    configuration.out_of_range_policy = OutOfRangePolicy::Reject;
    return configuration;
}

void test_boundaries_and_midpoint()
{
    const auto configuration = valid_configuration();

    const auto empty = tank_monitor::tank::calculate(configuration, 200.0);
    expect(empty.valid(), "empty measurement is valid");
    expect_near(empty.water_level_cm, 20.0, "empty level");
    expect_near(empty.percentage_full, 0.0, "empty percentage");
    expect_near(empty.volume_litres, 0.0, "empty volume");

    const auto full = tank_monitor::tank::calculate(configuration, 40.0);
    expect(full.valid(), "full measurement is valid");
    expect_near(full.water_level_cm, 180.0, "full level");
    expect_near(full.percentage_full, 100.0, "full percentage");
    expect_near(full.volume_litres, 1000.0, "full volume");

    const auto midpoint = tank_monitor::tank::calculate(configuration, 120.0);
    expect(midpoint.valid(), "midpoint measurement is valid");
    expect_near(midpoint.water_level_cm, 100.0, "midpoint level");
    expect_near(midpoint.percentage_full, 50.0, "midpoint percentage");
    expect_near(midpoint.volume_litres, 500.0, "midpoint volume");
}

void test_offset_and_range_policies()
{
    auto configuration = valid_configuration();
    configuration.sensor_offset_cm = 5.0;
    const auto offset = tank_monitor::tank::calculate(configuration, 115.0);
    expect_near(offset.corrected_distance_cm, 120.0, "sensor offset correction");
    expect_near(offset.percentage_full, 50.0, "offset percentage");

    const auto rejected = tank_monitor::tank::calculate(configuration, 25.0);
    expect(rejected.status == MeasurementStatus::TankOutOfRange,
           "tank details over-range is rejected");

    configuration.out_of_range_policy = OutOfRangePolicy::Clamp;
    const auto clamped = tank_monitor::tank::calculate(configuration, 25.0);
    expect(clamped.valid(), "tank details over-range can be clamped");
    expect_near(clamped.percentage_full, 100.0, "clamped percentage");

    const auto sensor_failure = tank_monitor::tank::calculate(configuration, 10.0);
    expect(sensor_failure.status == MeasurementStatus::SensorOutOfRange,
           "physical sensor range is never clamped");
}

void test_volume_models_and_units()
{
    auto configuration = valid_configuration();
    configuration.volume_model = VolumeModel::LinearPerCentimetre;
    configuration.volume_litres_per_centimetre = 4.0;
    configuration.display_unit = VolumeUnit::UsGallons;

    const auto measurement = tank_monitor::tank::calculate(configuration, 120.0);
    expect_near(measurement.volume_litres, 320.0, "linear volume model");
    expect_near(measurement.display_volume, 320.0 / 3.785411784, "US gallon conversion");
    expect_near(tank_monitor::tank::convert_litres(4.54609, VolumeUnit::ImperialGallons),
                1.0,
                "imperial gallon conversion");
}

void test_validation_and_bad_measurements()
{
    auto configuration = valid_configuration();
    configuration.full_level_cm = configuration.empty_level_cm;
    expect(tank_monitor::tank::validate(configuration).error == ValidationError::InvalidLevelRange,
           "invalid level span is rejected");

    configuration = valid_configuration();
    configuration.volume_model = VolumeModel::LinearPerCentimetre;
    configuration.volume_litres_per_centimetre = 0.0;
    expect(tank_monitor::tank::validate(configuration).error == ValidationError::InvalidLinearVolume,
           "invalid linear volume is rejected");

    configuration = valid_configuration();
    const auto nan = tank_monitor::tank::calculate(
        configuration,
        std::numeric_limits<double>::quiet_NaN());
    expect(nan.status == MeasurementStatus::NonFiniteDistance,
           "non-finite distance is rejected");
}

}  // namespace

int main()
{
    test_boundaries_and_midpoint();
    test_offset_and_range_policies();
    test_volume_models_and_units();
    test_validation_and_bad_measurements();

    if (failures == 0) {
        std::cout << "All tank calculator tests passed\n";
    }
    return failures == 0 ? 0 : 1;
}