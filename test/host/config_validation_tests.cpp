#include "config_validation/config_validator.hpp"

#include <algorithm>
#include <iostream>
#include <string_view>

namespace {

using tank_monitor::config::AppConfig;
using tank_monitor::config::DeviceRole;
using tank_monitor::config::RadioChip;
using tank_monitor::config_validation::ConfigError;

int failures = 0;

void expect(bool condition, const char* description)
{
    if (!condition) {
        std::cerr << "FAIL: " << description << '\n';
        ++failures;
    }
}

template <std::size_t Capacity>
void set_string(tank_monitor::config::FixedString<Capacity>& destination, std::string_view source)
{
    destination.fill('\0');
    const std::size_t length = std::min(source.size(), Capacity - 1);
    std::copy_n(source.begin(), length, destination.begin());
}

AppConfig operational_config(DeviceRole role)
{
    auto configuration = tank_monitor::config::default_config();
    configuration.configured = true;
    configuration.device.role = role;
    configuration.device.node_id = 0x1234;
    set_string(configuration.wifi.ssid, role == DeviceRole::Receiver ? "Example Network" : "");
    set_string(configuration.wifi.hostname, "tank-monitor");
    configuration.radio.chip = RadioChip::Sx1278;
    configuration.radio.frequency_hz = 433000000;
    set_string(configuration.tank.tank_name, "Example Tank");
    configuration.tank.capacity_litres = 1000.0;
    configuration.tank.sensor_reference_height_cm = 220.0;
    configuration.tank.minimum_sensor_distance_cm = 20.0;
    configuration.tank.maximum_sensor_distance_cm = 210.0;
    configuration.tank.empty_level_cm = 20.0;
    configuration.tank.full_level_cm = 180.0;
    return configuration;
}

void test_defaults_require_provisioning()
{
    const auto defaults = tank_monitor::config::default_config();
    expect(tank_monitor::config_validation::validate(defaults).error == ConfigError::NotConfigured,
           "default configuration cannot enter operational mode");
}

void test_role_complete_configurations()
{
    expect(tank_monitor::config_validation::validate(
               operational_config(DeviceRole::Transmitter)).valid(),
           "complete TX configuration is valid without Wi-Fi");
    expect(tank_monitor::config_validation::validate(
               operational_config(DeviceRole::Receiver)).valid(),
           "complete RX configuration is valid with Wi-Fi");
}

void test_cross_field_validation()
{
    auto role = operational_config(DeviceRole::Transmitter);
    role.device.role = static_cast<DeviceRole>(0xff);
    expect(tank_monitor::config_validation::validate(role).error == ConfigError::InvalidRole,
           "unknown role enum is rejected");

    auto receiver = operational_config(DeviceRole::Receiver);
    receiver.wifi.ssid.fill('\0');
    expect(tank_monitor::config_validation::validate(receiver).error == ConfigError::InvalidWifi,
           "RX requires an SSID");

    auto radio = operational_config(DeviceRole::Transmitter);
    radio.radio.chip = RadioChip::Sx1278;
    radio.radio.frequency_hz = 868000000;
    expect(tank_monitor::config_validation::validate(radio).error == ConfigError::InvalidRadio,
           "SX1278 rejects frequency outside its capability");

        radio = operational_config(DeviceRole::Transmitter);
        radio.radio.explicit_header = false;
        expect(tank_monitor::config_validation::validate(radio).error == ConfigError::InvalidRadio,
            "variable-length protocol requires explicit LoRa headers");

        radio = operational_config(DeviceRole::Transmitter);
        radio.radio.transmit_power_dbm = 1;
        expect(tank_monitor::config_validation::validate(radio).error == ConfigError::InvalidRadio,
            "T-Beam PA_BOOST rejects output below 2 dBm");

    auto pairing = operational_config(DeviceRole::Receiver);
    pairing.pairing.peers[0].enabled = true;
    pairing.pairing.peers[0].node_id = 1;
    pairing.pairing.peers[1].enabled = true;
    pairing.pairing.peers[1].node_id = 2;
    expect(tank_monitor::config_validation::validate(pairing).error == ConfigError::InvalidPairing,
           "v1 permits at most one active peer");

        auto battery = operational_config(DeviceRole::Transmitter);
        battery.battery.percentage_full_voltage = battery.battery.percentage_empty_voltage;
        expect(tank_monitor::config_validation::validate(battery).error == ConfigError::InvalidBattery,
            "battery percentage endpoints require a positive span");

            auto tank = operational_config(DeviceRole::Transmitter);
            tank.tank.volume_model = static_cast<tank_monitor::config::VolumeModel>(0xff);
            expect(tank_monitor::config_validation::validate(tank).error == ConfigError::InvalidTank,
                "unknown tank volume model is rejected");
}

void test_blynk_credentials_are_runtime_data()
{
    auto receiver = operational_config(DeviceRole::Receiver);
    receiver.blynk.enabled = true;
    expect(tank_monitor::config_validation::validate(receiver).error == ConfigError::InvalidBlynk,
           "enabled Blynk requires runtime credentials");

    set_string(receiver.blynk.host, "blynk.cloud");
    set_string(receiver.blynk.auth_token, "safe-test-placeholder");
    receiver.blynk.virtual_pins[0] = 0;
    expect(tank_monitor::config_validation::validate(receiver).valid(),
           "portal-populated Blynk settings validate");
}

}  // namespace

int main()
{
    test_defaults_require_provisioning();
    test_role_complete_configurations();
    test_cross_field_validation();
    test_blynk_credentials_are_runtime_data();

    if (failures == 0) {
        std::cout << "All configuration validation tests passed\n";
    }
    return failures == 0 ? 0 : 1;
}