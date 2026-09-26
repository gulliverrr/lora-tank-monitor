#include "config_validation/config_validator.hpp"

#include "alarms/alarm_state_machine.hpp"
#include "tank/tank_calculator.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace tank_monitor::config_validation {
namespace {

template <std::size_t Capacity>
bool terminated(const config::FixedString<Capacity>& value)
{
    return std::find(value.begin(), value.end(), '\0') != value.end();
}

template <std::size_t Capacity>
bool empty(const config::FixedString<Capacity>& value)
{
    return value[0] == '\0';
}

bool valid_radio(const config::RadioConfig& radio)
{
    const bool chip_frequency_valid =
        (radio.chip == config::RadioChip::Sx1276 &&
         radio.frequency_hz >= 137000000U && radio.frequency_hz <= 1020000000U) ||
        (radio.chip == config::RadioChip::Sx1278 &&
         radio.frequency_hz >= 137000000U && radio.frequency_hz <= 525000000U);
    constexpr std::array<std::uint32_t, 9> bandwidths{
        7800, 10400, 15600, 20800, 31250, 41700, 62500, 125000, 250000};

    return chip_frequency_valid &&
        std::find(bandwidths.begin(), bandwidths.end(), radio.bandwidth_hz) != bandwidths.end() &&
        radio.spreading_factor >= 6 && radio.spreading_factor <= 12 &&
        radio.coding_rate_denominator >= 5 && radio.coding_rate_denominator <= 8 &&
        radio.preamble_symbols >= 6 &&
        radio.transmit_power_dbm >= 2 && radio.transmit_power_dbm <= 20 &&
        radio.explicit_header &&
        radio.receive_timeout_ms > 0 && radio.transmit_timeout_ms > 0;
}

bool valid_sensor(const config::SensorConfig& sensor)
{
    return sensor.trigger_timeout_us > 0 && sensor.power_warmup_ms > 0 &&
        sensor.sample_count > 0 && sensor.sample_count <= 15 &&
        sensor.inter_sample_delay_ms > 0 &&
        std::isfinite(sensor.maximum_sample_spread_cm) && sensor.maximum_sample_spread_cm >= 0.0 &&
        sensor.measurement_interval_seconds > 0 &&
        sensor.critical_recheck_interval_seconds > 0 &&
        sensor.critical_recheck_interval_seconds <= sensor.measurement_interval_seconds &&
        sensor.transmission_retries <= 10;
}

bool valid_battery(const config::BatteryConfig& battery)
{
    const bool percentage_model_valid =
        battery.percentage_model == config::BatteryPercentageModel::Hidden ||
        battery.percentage_model == config::BatteryPercentageModel::LinearVoltage;
    return std::isfinite(battery.low_voltage_threshold) &&
        std::isfinite(battery.critical_voltage_cutoff) &&
        std::isfinite(battery.percentage_empty_voltage) &&
        std::isfinite(battery.percentage_full_voltage) &&
        std::isfinite(battery.calibration_scale) &&
        std::isfinite(battery.calibration_offset_volts) &&
        percentage_model_valid && battery.critical_voltage_cutoff > 0.0 &&
        battery.low_voltage_threshold > battery.critical_voltage_cutoff &&
        battery.percentage_empty_voltage > 0.0 &&
        battery.percentage_full_voltage > battery.percentage_empty_voltage &&
        battery.calibration_scale > 0.0 && battery.nominal_capacity_mah > 0 &&
        battery.usable_capacity_percent > 0 && battery.usable_capacity_percent <= 100;
}

bool valid_alarms(const config::AlarmConfig& alarm_config)
{
    const std::array policies{
        alarm_config.low_level,
        alarm_config.critical_high,
        alarm_config.low_battery,
        alarm_config.stale_data,
    };
    return std::all_of(policies.begin(), policies.end(), [](const config::AlarmPolicy& policy) {
        return !policy.enabled || alarms::validate(policy) == alarms::PolicyError::None;
    });
}

bool valid_pairing(const config::PairingConfig& pairing)
{
    if (pairing.pairing_window_seconds == 0) {
        return false;
    }
    std::size_t enabled_count = 0;
    for (const auto& peer : pairing.peers) {
        if (!terminated(peer.label)) {
            return false;
        }
        if (peer.enabled) {
            ++enabled_count;
            if (peer.node_id == 0 || peer.tank_id == 0) {
                return false;
            }
        }
    }
    return enabled_count <= 1;
}

bool valid_blynk(const config::BlynkConfig& blynk)
{
    if (!terminated(blynk.host) || !terminated(blynk.template_id) ||
        !terminated(blynk.device_name) || !terminated(blynk.auth_token)) {
        return false;
    }
    if (!blynk.enabled) {
        return true;
    }
    if (empty(blynk.host) || empty(blynk.auth_token) || blynk.port == 0 ||
        blynk.publish_interval_seconds == 0) {
        return false;
    }
    return std::all_of(blynk.virtual_pins.begin(), blynk.virtual_pins.end(), [](std::int16_t pin) {
        return pin >= -1 && pin <= 255;
    });
}

}  // namespace

ConfigValidationResult validate(const config::AppConfig& configuration)
{
    if (configuration.schema_version != config::kCurrentConfigVersion) {
        return {ConfigError::UnsupportedVersion};
    }
    if (!configuration.configured) {
        return {ConfigError::NotConfigured};
    }
    if (configuration.device.role != config::DeviceRole::Transmitter &&
        configuration.device.role != config::DeviceRole::Receiver) {
        return {ConfigError::InvalidRole};
    }
    if (configuration.device.node_id == 0) {
        return {ConfigError::InvalidNodeId};
    }
    if (configuration.device.role == config::DeviceRole::Receiver &&
        (!terminated(configuration.wifi.ssid) || !terminated(configuration.wifi.password) ||
         empty(configuration.wifi.ssid) ||
         configuration.wifi.reconnect_minimum_ms == 0 ||
         configuration.wifi.reconnect_maximum_ms < configuration.wifi.reconnect_minimum_ms)) {
        return {ConfigError::InvalidWifi};
    }
    if (!valid_radio(configuration.radio)) {
        return {ConfigError::InvalidRadio};
    }
    if (!valid_pairing(configuration.pairing)) {
        return {ConfigError::InvalidPairing};
    }
    if (configuration.device.role == config::DeviceRole::Transmitter &&
        (!terminated(configuration.tank.tank_name) ||
         (configuration.tank.tank_id != 0 && empty(configuration.tank.tank_name)) ||
         !tank::validate(configuration.tank).valid())) {
        return {ConfigError::InvalidTank};
    }
    if (configuration.device.role == config::DeviceRole::Transmitter && !valid_sensor(configuration.sensor)) {
        return {ConfigError::InvalidSensor};
    }
    if (configuration.device.role == config::DeviceRole::Transmitter && !valid_alarms(configuration.alarms)) {
        return {ConfigError::InvalidAlarm};
    }
    if (configuration.device.role == config::DeviceRole::Transmitter && !valid_battery(configuration.battery)) {
        return {ConfigError::InvalidBattery};
    }
    if (configuration.device.role == config::DeviceRole::Receiver && !valid_blynk(configuration.blynk)) {
        return {ConfigError::InvalidBlynk};
    }
    return {};
}

}  // namespace tank_monitor::config_validation