#pragma once

#include "config/alarm_config.hpp"
#include "config/tank_config.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace tank_monitor::config {

constexpr std::uint32_t kCurrentConfigVersion = 2;
constexpr std::size_t kMaximumPairedNodes = 4;
constexpr std::size_t kBlynkDatastreamCount = 12;

template <std::size_t Capacity>
using FixedString = std::array<char, Capacity>;

enum class DeviceRole : std::uint8_t {
    Unconfigured,
    Transmitter,
    Receiver,
};

enum class RadioChip : std::uint8_t {
    Unspecified,
    Sx1276,
    Sx1278,
};

struct DeviceConfig {
    DeviceRole role{DeviceRole::Unconfigured};
    std::uint64_t node_id{0};
    bool display_enabled{true};
    std::uint32_t display_timeout_seconds{30};
    std::uint16_t provisioning_long_press_ms{5000};
};

struct WifiConfig {
    FixedString<33> ssid{};
    FixedString<65> password{};
    FixedString<33> hostname{};
    std::uint32_t reconnect_minimum_ms{1000};
    std::uint32_t reconnect_maximum_ms{60000};
};

struct RadioConfig {
    RadioChip chip{RadioChip::Unspecified};
    std::uint32_t frequency_hz{0};
    std::uint32_t bandwidth_hz{125000};
    std::uint8_t spreading_factor{9};
    std::uint8_t coding_rate_denominator{5};
    std::uint8_t sync_word{0x12};
    std::uint16_t preamble_symbols{8};
    std::int8_t transmit_power_dbm{14};
    bool explicit_header{true};
    bool phy_crc_enabled{true};
    std::uint32_t receive_timeout_ms{2000};
    std::uint32_t transmit_timeout_ms{5000};
};

struct PairedNodeConfig {
    bool enabled{false};
    std::uint64_t node_id{0};
    FixedString<33> label{};
};

struct PairingConfig {
    std::array<PairedNodeConfig, kMaximumPairedNodes> peers{};
    std::uint16_t pairing_window_seconds{120};
};

struct SensorConfig {
    std::uint32_t trigger_timeout_us{40000};
    std::uint16_t power_warmup_ms{1000};
    std::uint8_t sample_count{3};
    std::uint16_t inter_sample_delay_ms{100};
    double maximum_sample_spread_cm{10.0};
    std::uint32_t measurement_interval_seconds{1800};
    std::uint32_t critical_recheck_interval_seconds{60};
    std::uint8_t transmission_retries{2};
};

struct AlarmConfig {
    AlarmPolicy low_level{};
    AlarmPolicy critical_high{};
    AlarmPolicy low_battery{};
    AlarmPolicy stale_data{};
};

enum class BatteryPercentageModel : std::uint8_t {
    Hidden,
    LinearVoltage,
};

struct BatteryConfig {
    double low_voltage_threshold{3.4};
    double critical_voltage_cutoff{3.2};
    BatteryPercentageModel percentage_model{BatteryPercentageModel::LinearVoltage};
    double percentage_empty_voltage{3.2};
    double percentage_full_voltage{4.2};
    double calibration_scale{1.0};
    double calibration_offset_volts{0.0};
    std::uint16_t nominal_capacity_mah{3500};
    std::uint8_t usable_capacity_percent{80};
};

struct BlynkConfig {
    bool enabled{false};
    FixedString<65> host{};
    std::uint16_t port{443};
    FixedString<65> template_id{};
    FixedString<65> device_name{};
    FixedString<97> auth_token{};
    std::uint32_t publish_interval_seconds{60};
    std::array<std::int16_t, kBlynkDatastreamCount> virtual_pins{
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
};

struct AppConfig {
    std::uint32_t schema_version{kCurrentConfigVersion};
    std::uint32_t generation{0};
    bool configured{false};
    DeviceConfig device{};
    WifiConfig wifi{};
    RadioConfig radio{};
    PairingConfig pairing{};
    TankConfig tank{};
    SensorConfig sensor{};
    AlarmConfig alarms{};
    BatteryConfig battery{};
    BlynkConfig blynk{};
};

[[nodiscard]] AppConfig default_config();

}  // namespace tank_monitor::config