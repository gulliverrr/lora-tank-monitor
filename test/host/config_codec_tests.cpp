#include "storage/config_codec.hpp"

#include <algorithm>
#include <cstring>
#include <iostream>
#include <string_view>

namespace {

using tank_monitor::storage::CodecError;
using tank_monitor::storage::ConfigRecord;

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
    std::copy_n(source.begin(), std::min(source.size(), Capacity - 1), destination.begin());
}

tank_monitor::config::AppConfig populated_configuration()
{
    auto value = tank_monitor::config::default_config();
    value.generation = 19;
    value.configured = true;
    value.device.role = tank_monitor::config::DeviceRole::Receiver;
    value.device.node_id = 0x0102030405060708ULL;
    set_string(value.wifi.ssid, "Example Network");
    set_string(value.wifi.password, "not-a-real-password");
    set_string(value.wifi.hostname, "tank-rx");
    value.radio.chip = tank_monitor::config::RadioChip::Sx1278;
    value.radio.frequency_hz = 433775000;
    value.pairing.peers[0].enabled = true;
    value.pairing.peers[0].node_id = 0xaabbccdd;
    set_string(value.pairing.peers[0].label, "North Tank TX");
    set_string(value.tank.tank_name, "North Tank");
    value.tank.capacity_litres = 1234.5;
    value.tank.sensor_reference_height_cm = 250.25;
    value.alarms.critical_high.enabled = true;
    value.alarms.critical_high.trigger_threshold = 95.5;
    value.battery.percentage_empty_voltage = 3.15;
    value.blynk.enabled = true;
    set_string(value.blynk.host, "blynk.example.invalid");
    set_string(value.blynk.auth_token, "safe-test-placeholder");
    value.blynk.virtual_pins[0] = 10;
    return value;
}

void test_round_trip_is_stable()
{
    const auto original = populated_configuration();
    ConfigRecord encoded{};
    expect(tank_monitor::storage::encode(original, encoded) == CodecError::None,
           "configuration encodes");
    expect(encoded.size > 500 && encoded.size < encoded.bytes.size(),
           "configuration record has bounded expected size");

    const auto decoded = tank_monitor::storage::decode(encoded.bytes.data(), encoded.size);
    expect(decoded.valid(), "configuration decodes");
    expect(decoded.configuration.generation == original.generation, "generation round trips");
    expect(decoded.configuration.device.node_id == original.device.node_id, "node ID round trips");
    expect(decoded.configuration.radio.frequency_hz == original.radio.frequency_hz,
           "radio frequency round trips");
    expect(decoded.configuration.tank.capacity_litres == original.tank.capacity_litres,
           "tank capacity round trips exactly");
        expect(std::strcmp(decoded.configuration.tank.tank_name.data(),
                  original.tank.tank_name.data()) == 0,
            "tank name round trips");
    expect(std::strcmp(decoded.configuration.wifi.ssid.data(), original.wifi.ssid.data()) == 0,
           "Wi-Fi SSID round trips");
    expect(std::strcmp(decoded.configuration.blynk.auth_token.data(),
                       original.blynk.auth_token.data()) == 0,
           "Blynk token round trips without logging");

    ConfigRecord reencoded{};
    expect(tank_monitor::storage::encode(decoded.configuration, reencoded) == CodecError::None,
           "decoded configuration re-encodes");
    expect(reencoded.size == encoded.size &&
               std::equal(encoded.bytes.begin(), encoded.bytes.begin() + encoded.size,
                          reencoded.bytes.begin()),
           "record encoding is deterministic");
}

void test_record_rejection()
{
    ConfigRecord record{};
    expect(tank_monitor::storage::encode(populated_configuration(), record) == CodecError::None,
           "rejection fixture encodes");

    auto corrupted = record;
    corrupted.bytes[100] ^= 0x01;
    expect(tank_monitor::storage::decode(corrupted.bytes.data(), corrupted.size).error ==
               CodecError::CrcMismatch,
           "single-byte corruption fails CRC");
    expect(tank_monitor::storage::decode(record.bytes.data(), record.size - 1).error ==
               CodecError::LengthMismatch,
           "truncated record fails length");

    auto future_record = record;
    future_record.bytes[4] = 2;
    expect(tank_monitor::storage::decode(future_record.bytes.data(), future_record.size).error ==
               CodecError::UnsupportedRecordVersion,
           "future record format is rejected");

    auto future_schema = record;
    future_schema.bytes[8] = tank_monitor::config::kCurrentConfigVersion + 1;
    expect(tank_monitor::storage::decode(future_schema.bytes.data(), future_schema.size).error ==
               CodecError::UnsupportedSchemaVersion,
           "future schema is routed to migration handling");
}

}  // namespace

int main()
{
    test_round_trip_is_stable();
    test_record_rejection();

    if (failures == 0) {
        std::cout << "All configuration codec tests passed\n";
    }
    return failures == 0 ? 0 : 1;
}