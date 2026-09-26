#include "battery/battery_estimator.hpp"

#include <cmath>
#include <iostream>

namespace {

int failures = 0;

void expect(bool condition, const char* description)
{
    if (!condition) {
        std::cerr << "FAIL: " << description << '\n';
        ++failures;
    }
}

}  // namespace

int main()
{
    auto configuration = tank_monitor::config::default_config().battery;

    const auto empty = tank_monitor::battery::estimate(3200, configuration);
    expect(empty.available && empty.percentage == 0, "empty endpoint is zero percent");

    const auto midpoint = tank_monitor::battery::estimate(3700, configuration);
    expect(midpoint.available && midpoint.percentage == 50, "midpoint is fifty percent");

    const auto full = tank_monitor::battery::estimate(4203, configuration);
    expect(full.available && full.percentage == 100, "voltage above full endpoint clamps");

    const auto below = tank_monitor::battery::estimate(3000, configuration);
    expect(below.available && below.percentage == 0, "voltage below empty endpoint clamps");

    configuration.calibration_scale = 1.01;
    configuration.calibration_offset_volts = -0.02;
    const auto calibrated = tank_monitor::battery::estimate(4000, configuration);
    expect(std::abs(calibrated.calibrated_voltage - 4.02) < 1.0e-9,
           "scale and offset are applied before percentage");
    expect(calibrated.percentage == 82, "calibrated voltage drives percentage");

    configuration.percentage_model = tank_monitor::config::BatteryPercentageModel::Hidden;
    expect(!tank_monitor::battery::estimate(4000, configuration).available,
           "hidden model suppresses percentage");
    expect(!tank_monitor::battery::estimate(0, configuration).available,
           "missing voltage suppresses percentage");

    if (failures == 0) {
        std::cout << "All battery estimator tests passed\n";
    }
    return failures == 0 ? 0 : 1;
}