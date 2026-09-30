#include "provisioning/config_form.hpp"

#include <cstring>
#include <iostream>
#include <string>

namespace {

int failures = 0;

void expect(bool condition, const char* description)
{
    if (!condition) {
        std::cerr << "FAIL: " << description << '\n';
        ++failures;
    }
}

std::string valid_form()
{
    return
        "role=tx&display_enabled=1&display_timeout_s=30&long_press_ms=5000"
        "&wifi_ssid=&wifi_password=&hostname=tank-tx&wifi_retry_min_ms=1000&wifi_retry_max_ms=60000"
        "&radio_chip=sx1278&frequency_hz=433000000&bandwidth_hz=125000&spreading_factor=9&coding_rate=5&sync_word=18&preamble=8&tx_power=14&rx_timeout_ms=2000&tx_timeout_ms=5000"
        "&sensor_bottom_cm=220&sensor_surface_cm=40&tank_capacity_litres=1000&volume_unit=litres&range_policy=report&measurement_interval_minutes=30"
        "&trigger_timeout_us=40000&power_warmup_ms=1000&sample_count=3&inter_sample_ms=100&max_spread_cm=10&measurement_interval_s=1800&critical_interval_s=60&transmission_retries=2"
        "&alarm_low_enabled=0&alarm_low_direction=below&alarm_low_threshold=20&alarm_low_hysteresis=3&alarm_low_confirmations=3&alarm_low_reminder_s=3600"
        "&alarm_high_enabled=1&alarm_high_direction=above&alarm_high_threshold=95&alarm_high_hysteresis=3&alarm_high_confirmations=3&alarm_high_reminder_s=300"
        "&alarm_battery_enabled=1&alarm_battery_direction=below&alarm_battery_threshold=3.4&alarm_battery_hysteresis=0.1&alarm_battery_confirmations=3&alarm_battery_reminder_s=3600"
        "&alarm_stale_enabled=1&alarm_stale_direction=above&alarm_stale_threshold=3600&alarm_stale_hysteresis=300&alarm_stale_confirmations=1&alarm_stale_reminder_s=3600"
        "&battery_low_v=3.4&battery_cutoff_v=3.2&battery_percent_model=linear&battery_empty_v=3.2&battery_full_v=4.2&battery_scale=1&battery_offset_v=0&battery_capacity_mah=3500&battery_usable_percent=80"
        "&blynk_enabled=0&blynk_host=&blynk_port=443&blynk_template=&blynk_device=&blynk_token=&blynk_interval_s=60"
        "&blynk_pin_0=-1&blynk_pin_1=-1&blynk_pin_2=-1&blynk_pin_3=-1&blynk_pin_4=-1&blynk_pin_5=-1&blynk_pin_6=-1&blynk_pin_7=-1&blynk_pin_8=-1&blynk_pin_9=-1&blynk_pin_10=-1&blynk_pin_11=-1";
}

}  // namespace

int main()
{
    const std::string form = valid_form();
    const auto parsed = tank_monitor::provisioning::parse_config_form(
        form.data(), form.size(), tank_monitor::config::default_config(), 0xaabbccdd);
    expect(parsed.valid(), "complete TX form validates");
    expect(parsed.configuration.device.node_id == 0xaabbccdd, "hardware node ID is not form-controlled");
        expect(parsed.configuration.tank.full_level_cm == 180.0,
            "two distances derive full water level");
            expect(parsed.configuration.sensor.measurement_interval_seconds == 1800,
                "measurement interval minutes persist as seconds");

            std::string custom_interval = form;
            const auto interval_field = custom_interval.find("measurement_interval_minutes=30");
            custom_interval.replace(interval_field, std::strlen("measurement_interval_minutes=30"),
                                    "measurement_interval_minutes=45");
            const auto custom_interval_result = tank_monitor::provisioning::parse_config_form(
                custom_interval.data(), custom_interval.size(), tank_monitor::config::default_config(), 0xaabbccdd);
            expect(custom_interval_result.valid() && custom_interval_result.configuration.sensor.measurement_interval_seconds == 2700,
                   "custom measurement period persists as seconds");
        expect(parsed.configuration.device.role == tank_monitor::config::DeviceRole::Transmitter,
            "transmitter form keeps TX-only configuration");

    auto paired_base = tank_monitor::config::default_config();
    paired_base.pairing.peers[0].enabled = true;
    paired_base.pairing.peers[0].node_id = 0x1122334455667788ULL;
    paired_base.pairing.peers[0].tank_id = 7;
    std::strcpy(paired_base.pairing.peers[0].label.data(), "Paired TX");
    const auto paired_result = tank_monitor::provisioning::parse_config_form(
        form.data(), form.size(), paired_base, 0xaabbccdd);
    expect(paired_result.valid() && paired_result.configuration.pairing.peers[0].enabled &&
               paired_result.configuration.pairing.peers[0].node_id == 0x1122334455667788ULL,
           "staging configuration preserves existing pairing");

    std::string malformed = form;
    const auto frequency = malformed.find("frequency_hz=433000000");
    malformed.replace(frequency, std::strlen("frequency_hz=433000000"), "frequency_hz=oops");
    expect(tank_monitor::provisioning::parse_config_form(
               malformed.data(), malformed.size(), tank_monitor::config::default_config(), 1).error ==
               tank_monitor::provisioning::FormError::InvalidValue,
           "malformed number is rejected");

    std::string invalid = form;
    const auto surface_distance = invalid.find("sensor_surface_cm=40");
    invalid.replace(surface_distance, std::strlen("sensor_surface_cm=40"), "sensor_surface_cm=220");
    const auto invalid_result = tank_monitor::provisioning::parse_config_form(
        invalid.data(), invalid.size(), tank_monitor::config::default_config(), 1);
    expect(invalid_result.error == tank_monitor::provisioning::FormError::ValidationFailed &&
               invalid_result.validation_error == tank_monitor::config_validation::ConfigError::InvalidTank,
           "cross-field tank error is reported by group");

    const char bad_escape[] = "role=%GG";
    expect(tank_monitor::provisioning::parse_config_form(
               bad_escape, sizeof(bad_escape) - 1, tank_monitor::config::default_config(), 1).error ==
               tank_monitor::provisioning::FormError::InvalidValue,
           "malformed URL encoding is rejected");

    if (failures == 0) {
        std::cout << "All configuration form tests passed\n";
    }
    return failures == 0 ? 0 : 1;
}