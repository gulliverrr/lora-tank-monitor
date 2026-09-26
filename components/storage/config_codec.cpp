#include "storage/config_codec.hpp"

#include <algorithm>
#include <cstring>
#include <type_traits>

namespace tank_monitor::storage {
namespace {

constexpr std::size_t kHeaderSize = 20;
constexpr std::size_t kCrcSize = 4;

std::uint32_t crc32(const std::uint8_t* data, std::size_t size)
{
    std::uint32_t crc = 0xffffffffU;
    for (std::size_t index = 0; index < size; ++index) {
        crc ^= data[index];
        for (std::uint8_t bit = 0; bit < 8; ++bit) {
            const std::uint32_t mask = 0U - (crc & 1U);
            crc = (crc >> 1U) ^ (0xedb88320U & mask);
        }
    }
    return ~crc;
}

class Writer {
public:
    Writer(std::uint8_t* data, std::size_t capacity) : data_(data), capacity_(capacity) {}

    bool u8(std::uint8_t value) { return bytes(&value, sizeof(value)); }
    bool boolean(bool value) { return u8(value ? 1U : 0U); }

    bool u16(std::uint16_t value)
    {
        std::uint8_t encoded[2]{
            static_cast<std::uint8_t>(value),
            static_cast<std::uint8_t>(value >> 8U),
        };
        return bytes(encoded, sizeof(encoded));
    }

    bool i16(std::int16_t value) { return u16(static_cast<std::uint16_t>(value)); }

    bool u32(std::uint32_t value)
    {
        std::uint8_t encoded[4]{};
        for (std::size_t index = 0; index < sizeof(encoded); ++index) {
            encoded[index] = static_cast<std::uint8_t>(value >> (index * 8U));
        }
        return bytes(encoded, sizeof(encoded));
    }

    bool u64(std::uint64_t value)
    {
        std::uint8_t encoded[8]{};
        for (std::size_t index = 0; index < sizeof(encoded); ++index) {
            encoded[index] = static_cast<std::uint8_t>(value >> (index * 8U));
        }
        return bytes(encoded, sizeof(encoded));
    }

    bool floating(double value)
    {
        static_assert(sizeof(double) == sizeof(std::uint64_t));
        std::uint64_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        return u64(bits);
    }

    template <typename Enum>
    bool enumeration(Enum value)
    {
        static_assert(std::is_enum_v<Enum>);
        return u8(static_cast<std::uint8_t>(value));
    }

    template <std::size_t Capacity>
    bool string(const config::FixedString<Capacity>& value)
    {
        return bytes(reinterpret_cast<const std::uint8_t*>(value.data()), value.size());
    }

    [[nodiscard]] std::size_t size() const { return offset_; }

private:
    bool bytes(const std::uint8_t* source, std::size_t count)
    {
        if (source == nullptr || count > capacity_ - offset_) {
            return false;
        }
        std::copy_n(source, count, data_ + offset_);
        offset_ += count;
        return true;
    }

    std::uint8_t* data_{nullptr};
    std::size_t capacity_{0};
    std::size_t offset_{0};
};

class Reader {
public:
    Reader(const std::uint8_t* data, std::size_t size) : data_(data), size_(size) {}

    bool u8(std::uint8_t& value) { return bytes(&value, sizeof(value)); }

    bool boolean(bool& value)
    {
        std::uint8_t encoded = 0;
        if (!u8(encoded) || encoded > 1U) {
            return false;
        }
        value = encoded != 0;
        return true;
    }

    bool u16(std::uint16_t& value)
    {
        std::uint8_t encoded[2]{};
        if (!bytes(encoded, sizeof(encoded))) {
            return false;
        }
        value = static_cast<std::uint16_t>(encoded[0]) |
            static_cast<std::uint16_t>(encoded[1]) << 8U;
        return true;
    }

    bool i16(std::int16_t& value)
    {
        std::uint16_t encoded = 0;
        if (!u16(encoded)) {
            return false;
        }
        value = static_cast<std::int16_t>(encoded);
        return true;
    }

    bool u32(std::uint32_t& value)
    {
        std::uint8_t encoded[4]{};
        if (!bytes(encoded, sizeof(encoded))) {
            return false;
        }
        value = 0;
        for (std::size_t index = 0; index < sizeof(encoded); ++index) {
            value |= static_cast<std::uint32_t>(encoded[index]) << (index * 8U);
        }
        return true;
    }

    bool u64(std::uint64_t& value)
    {
        std::uint8_t encoded[8]{};
        if (!bytes(encoded, sizeof(encoded))) {
            return false;
        }
        value = 0;
        for (std::size_t index = 0; index < sizeof(encoded); ++index) {
            value |= static_cast<std::uint64_t>(encoded[index]) << (index * 8U);
        }
        return true;
    }

    bool floating(double& value)
    {
        std::uint64_t bits = 0;
        if (!u64(bits)) {
            return false;
        }
        std::memcpy(&value, &bits, sizeof(value));
        return true;
    }

    template <typename Enum>
    bool enumeration(Enum& value)
    {
        static_assert(std::is_enum_v<Enum>);
        std::uint8_t encoded = 0;
        if (!u8(encoded)) {
            return false;
        }
        value = static_cast<Enum>(encoded);
        return true;
    }

    template <std::size_t Capacity>
    bool string(config::FixedString<Capacity>& value)
    {
        return bytes(reinterpret_cast<std::uint8_t*>(value.data()), value.size()) &&
            std::find(value.begin(), value.end(), '\0') != value.end();
    }

    [[nodiscard]] bool finished() const { return offset_ == size_; }

private:
    bool bytes(std::uint8_t* destination, std::size_t count)
    {
        if (destination == nullptr || count > size_ - offset_) {
            return false;
        }
        std::copy_n(data_ + offset_, count, destination);
        offset_ += count;
        return true;
    }

    const std::uint8_t* data_{nullptr};
    std::size_t size_{0};
    std::size_t offset_{0};
};

bool write_alarm(Writer& writer, const config::AlarmPolicy& alarm)
{
    return writer.boolean(alarm.enabled) && writer.enumeration(alarm.direction) &&
        writer.floating(alarm.trigger_threshold) && writer.floating(alarm.clear_hysteresis) &&
        writer.u16(alarm.consecutive_confirmations) &&
        writer.u32(alarm.reminder_interval_seconds);
}

bool read_alarm(Reader& reader, config::AlarmPolicy& alarm)
{
    return reader.boolean(alarm.enabled) && reader.enumeration(alarm.direction) &&
        reader.floating(alarm.trigger_threshold) && reader.floating(alarm.clear_hysteresis) &&
        reader.u16(alarm.consecutive_confirmations) &&
        reader.u32(alarm.reminder_interval_seconds);
}

bool write_payload(Writer& writer, const config::AppConfig& value)
{
    if (!(writer.boolean(value.configured) && writer.enumeration(value.device.role) &&
          writer.u64(value.device.node_id) && writer.boolean(value.device.display_enabled) &&
          writer.u32(value.device.display_timeout_seconds) &&
          writer.u16(value.device.provisioning_long_press_ms) &&
          writer.string(value.wifi.ssid) && writer.string(value.wifi.password) &&
          writer.string(value.wifi.hostname) && writer.u32(value.wifi.reconnect_minimum_ms) &&
          writer.u32(value.wifi.reconnect_maximum_ms) && writer.enumeration(value.radio.chip) &&
          writer.u32(value.radio.frequency_hz) && writer.u32(value.radio.bandwidth_hz) &&
          writer.u8(value.radio.spreading_factor) &&
          writer.u8(value.radio.coding_rate_denominator) && writer.u8(value.radio.sync_word) &&
          writer.u16(value.radio.preamble_symbols) &&
          writer.u8(static_cast<std::uint8_t>(value.radio.transmit_power_dbm)) &&
          writer.boolean(value.radio.explicit_header) && writer.boolean(value.radio.phy_crc_enabled) &&
          writer.u32(value.radio.receive_timeout_ms) && writer.u32(value.radio.transmit_timeout_ms) &&
          writer.u16(value.pairing.pairing_window_seconds))) {
        return false;
    }
    for (const auto& peer : value.pairing.peers) {
        if (!(writer.boolean(peer.enabled) && writer.u64(peer.node_id) &&
              writer.u32(peer.tank_id) && writer.string(peer.label))) {
            return false;
        }
    }
        if (!(writer.u32(value.tank.tank_id) && writer.string(value.tank.tank_name) &&
            writer.floating(value.tank.capacity_litres) &&
          writer.enumeration(value.tank.display_unit) && writer.enumeration(value.tank.volume_model) &&
          writer.floating(value.tank.volume_litres_per_centimetre) &&
          writer.floating(value.tank.sensor_reference_height_cm) &&
          writer.floating(value.tank.minimum_sensor_distance_cm) &&
          writer.floating(value.tank.maximum_sensor_distance_cm) &&
          writer.floating(value.tank.empty_level_cm) && writer.floating(value.tank.full_level_cm) &&
          writer.floating(value.tank.sensor_offset_cm) &&
          writer.enumeration(value.tank.out_of_range_policy) &&
          writer.u32(value.sensor.trigger_timeout_us) && writer.u16(value.sensor.power_warmup_ms) &&
          writer.u8(value.sensor.sample_count) && writer.u16(value.sensor.inter_sample_delay_ms) &&
          writer.floating(value.sensor.maximum_sample_spread_cm) &&
          writer.u32(value.sensor.measurement_interval_seconds) &&
          writer.u32(value.sensor.critical_recheck_interval_seconds) &&
          writer.u8(value.sensor.transmission_retries) &&
          write_alarm(writer, value.alarms.low_level) &&
          write_alarm(writer, value.alarms.critical_high) &&
          write_alarm(writer, value.alarms.low_battery) &&
          write_alarm(writer, value.alarms.stale_data) &&
          writer.floating(value.battery.low_voltage_threshold) &&
          writer.floating(value.battery.critical_voltage_cutoff) &&
          writer.enumeration(value.battery.percentage_model) &&
          writer.floating(value.battery.percentage_empty_voltage) &&
          writer.floating(value.battery.percentage_full_voltage) &&
          writer.floating(value.battery.calibration_scale) &&
          writer.floating(value.battery.calibration_offset_volts) &&
          writer.u16(value.battery.nominal_capacity_mah) &&
          writer.u8(value.battery.usable_capacity_percent) &&
          writer.boolean(value.blynk.enabled) && writer.string(value.blynk.host) &&
          writer.u16(value.blynk.port) && writer.string(value.blynk.template_id) &&
          writer.string(value.blynk.device_name) && writer.string(value.blynk.auth_token) &&
          writer.u32(value.blynk.publish_interval_seconds))) {
        return false;
    }
    for (const std::int16_t pin : value.blynk.virtual_pins) {
        if (!writer.i16(pin)) {
            return false;
        }
    }
    return true;
}

bool read_payload(Reader& reader, config::AppConfig& value)
{
    std::uint8_t transmit_power = 0;
    if (!(reader.boolean(value.configured) && reader.enumeration(value.device.role) &&
          reader.u64(value.device.node_id) && reader.boolean(value.device.display_enabled) &&
          reader.u32(value.device.display_timeout_seconds) &&
          reader.u16(value.device.provisioning_long_press_ms) &&
          reader.string(value.wifi.ssid) && reader.string(value.wifi.password) &&
          reader.string(value.wifi.hostname) && reader.u32(value.wifi.reconnect_minimum_ms) &&
          reader.u32(value.wifi.reconnect_maximum_ms) && reader.enumeration(value.radio.chip) &&
          reader.u32(value.radio.frequency_hz) && reader.u32(value.radio.bandwidth_hz) &&
          reader.u8(value.radio.spreading_factor) &&
          reader.u8(value.radio.coding_rate_denominator) && reader.u8(value.radio.sync_word) &&
          reader.u16(value.radio.preamble_symbols) && reader.u8(transmit_power) &&
          reader.boolean(value.radio.explicit_header) && reader.boolean(value.radio.phy_crc_enabled) &&
          reader.u32(value.radio.receive_timeout_ms) && reader.u32(value.radio.transmit_timeout_ms) &&
          reader.u16(value.pairing.pairing_window_seconds))) {
        return false;
    }
    value.radio.transmit_power_dbm = static_cast<std::int8_t>(transmit_power);
    for (auto& peer : value.pairing.peers) {
        if (!(reader.boolean(peer.enabled) && reader.u64(peer.node_id) &&
              reader.u32(peer.tank_id) && reader.string(peer.label))) {
            return false;
        }
    }
        if (!(reader.u32(value.tank.tank_id) && reader.string(value.tank.tank_name) &&
            reader.floating(value.tank.capacity_litres) &&
          reader.enumeration(value.tank.display_unit) && reader.enumeration(value.tank.volume_model) &&
          reader.floating(value.tank.volume_litres_per_centimetre) &&
          reader.floating(value.tank.sensor_reference_height_cm) &&
          reader.floating(value.tank.minimum_sensor_distance_cm) &&
          reader.floating(value.tank.maximum_sensor_distance_cm) &&
          reader.floating(value.tank.empty_level_cm) && reader.floating(value.tank.full_level_cm) &&
          reader.floating(value.tank.sensor_offset_cm) &&
          reader.enumeration(value.tank.out_of_range_policy) &&
          reader.u32(value.sensor.trigger_timeout_us) && reader.u16(value.sensor.power_warmup_ms) &&
          reader.u8(value.sensor.sample_count) && reader.u16(value.sensor.inter_sample_delay_ms) &&
          reader.floating(value.sensor.maximum_sample_spread_cm) &&
          reader.u32(value.sensor.measurement_interval_seconds) &&
          reader.u32(value.sensor.critical_recheck_interval_seconds) &&
          reader.u8(value.sensor.transmission_retries) &&
          read_alarm(reader, value.alarms.low_level) &&
          read_alarm(reader, value.alarms.critical_high) &&
          read_alarm(reader, value.alarms.low_battery) &&
          read_alarm(reader, value.alarms.stale_data) &&
          reader.floating(value.battery.low_voltage_threshold) &&
          reader.floating(value.battery.critical_voltage_cutoff) &&
          reader.enumeration(value.battery.percentage_model) &&
          reader.floating(value.battery.percentage_empty_voltage) &&
          reader.floating(value.battery.percentage_full_voltage) &&
          reader.floating(value.battery.calibration_scale) &&
          reader.floating(value.battery.calibration_offset_volts) &&
          reader.u16(value.battery.nominal_capacity_mah) &&
          reader.u8(value.battery.usable_capacity_percent) &&
          reader.boolean(value.blynk.enabled) && reader.string(value.blynk.host) &&
          reader.u16(value.blynk.port) && reader.string(value.blynk.template_id) &&
          reader.string(value.blynk.device_name) && reader.string(value.blynk.auth_token) &&
          reader.u32(value.blynk.publish_interval_seconds))) {
        return false;
    }
    for (auto& pin : value.blynk.virtual_pins) {
        if (!reader.i16(pin)) {
            return false;
        }
    }
    return reader.finished();
}

std::uint16_t read_u16(const std::uint8_t* data)
{
    return static_cast<std::uint16_t>(data[0]) |
        static_cast<std::uint16_t>(data[1]) << 8U;
}

std::uint32_t read_u32(const std::uint8_t* data)
{
    return static_cast<std::uint32_t>(data[0]) |
        static_cast<std::uint32_t>(data[1]) << 8U |
        static_cast<std::uint32_t>(data[2]) << 16U |
        static_cast<std::uint32_t>(data[3]) << 24U;
}

}  // namespace

CodecError encode(const config::AppConfig& configuration, ConfigRecord& record)
{
    record = {};
    Writer header(record.bytes.data(), record.bytes.size());
    if (!(header.u32(kConfigRecordMagic) && header.u16(kConfigRecordFormatVersion) &&
          header.u16(0) && header.u32(configuration.schema_version) &&
          header.u32(configuration.generation) && header.u32(0))) {
        return CodecError::BufferTooSmall;
    }

    Writer payload(record.bytes.data() + kHeaderSize,
                   record.bytes.size() - kHeaderSize - kCrcSize);
    if (!write_payload(payload, configuration)) {
        return CodecError::BufferTooSmall;
    }
    const std::size_t payload_size = payload.size();
    record.bytes[16] = static_cast<std::uint8_t>(payload_size);
    record.bytes[17] = static_cast<std::uint8_t>(payload_size >> 8U);
    record.bytes[18] = static_cast<std::uint8_t>(payload_size >> 16U);
    record.bytes[19] = static_cast<std::uint8_t>(payload_size >> 24U);

    const std::size_t crc_offset = kHeaderSize + payload_size;
    Writer trailer(record.bytes.data() + crc_offset, kCrcSize);
    if (!trailer.u32(crc32(record.bytes.data(), crc_offset))) {
        return CodecError::BufferTooSmall;
    }
    record.size = crc_offset + kCrcSize;
    return CodecError::None;
}

DecodeResult decode(const std::uint8_t* data, std::size_t size)
{
    DecodeResult result{};
    if (data == nullptr) {
        result.error = CodecError::InvalidArgument;
        return result;
    }
    if (size < kHeaderSize + kCrcSize || size > kMaximumConfigRecordSize) {
        result.error = CodecError::LengthMismatch;
        return result;
    }
    if (read_u32(data) != kConfigRecordMagic) {
        result.error = CodecError::InvalidMagic;
        return result;
    }
    if (read_u16(data + 4) != kConfigRecordFormatVersion) {
        result.error = CodecError::UnsupportedRecordVersion;
        return result;
    }
    const std::uint32_t schema_version = read_u32(data + 8);
    if (schema_version != config::kCurrentConfigVersion) {
        result.error = CodecError::UnsupportedSchemaVersion;
        return result;
    }
    const std::uint32_t payload_size = read_u32(data + 16);
    if (payload_size > kMaximumConfigRecordSize - kHeaderSize - kCrcSize ||
        size != kHeaderSize + payload_size + kCrcSize) {
        result.error = CodecError::LengthMismatch;
        return result;
    }
    const std::size_t crc_offset = kHeaderSize + payload_size;
    if (read_u32(data + crc_offset) != crc32(data, crc_offset)) {
        result.error = CodecError::CrcMismatch;
        return result;
    }

    result.configuration = config::default_config();
    result.configuration.schema_version = schema_version;
    result.configuration.generation = read_u32(data + 12);
    Reader payload(data + kHeaderSize, payload_size);
    if (!read_payload(payload, result.configuration)) {
        result.error = CodecError::MalformedPayload;
    }
    return result;
}

}  // namespace tank_monitor::storage