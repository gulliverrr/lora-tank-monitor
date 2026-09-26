#include "battery/battery_estimator.hpp"

#include <algorithm>
#include <cmath>

namespace tank_monitor::battery {

BatteryEstimate estimate(
    std::uint16_t measured_millivolts,
    const config::BatteryConfig& configuration)
{
    BatteryEstimate result{};
    result.calibrated_voltage =
        (static_cast<double>(measured_millivolts) / 1000.0) * configuration.calibration_scale +
        configuration.calibration_offset_volts;

    if (measured_millivolts == 0 ||
        configuration.percentage_model == config::BatteryPercentageModel::Hidden ||
        !std::isfinite(result.calibrated_voltage) ||
        !std::isfinite(configuration.percentage_empty_voltage) ||
        !std::isfinite(configuration.percentage_full_voltage) ||
        configuration.percentage_full_voltage <= configuration.percentage_empty_voltage) {
        return result;
    }

    const double fraction = std::clamp(
        (result.calibrated_voltage - configuration.percentage_empty_voltage) /
            (configuration.percentage_full_voltage - configuration.percentage_empty_voltage),
        0.0,
        1.0);
    result.percentage = static_cast<std::uint8_t>(std::lround(fraction * 100.0));
    result.available = true;
    return result;
}

}  // namespace tank_monitor::battery