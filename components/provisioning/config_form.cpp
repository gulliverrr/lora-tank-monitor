#include "provisioning/config_form.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string_view>
#include <type_traits>

namespace tank_monitor::provisioning {
namespace {

class FormReader {
public:
    FormReader(const char* body, std::size_t size) : body_(body), size_(size) {}

    template <std::size_t Capacity>
    bool text(const char* name, std::array<char, Capacity>& destination, bool preserve_when_empty = false) const
    {
        std::array<char, Capacity> decoded{};
        bool found = false;
        if (!value(name, decoded.data(), decoded.size(), found)) {
            return false;
        }
        if (!found) {
            return true;
        }
        if (preserve_when_empty && decoded[0] == '\0') {
            return true;
        }
        destination = decoded;
        return true;
    }

    bool boolean(const char* name, bool& destination) const
    {
        std::array<char, 8> decoded{};
        bool found = false;
        if (!value(name, decoded.data(), decoded.size(), found)) {
            return false;
        }
        if (!found) {
            destination = false;
            return true;
        }
        if (std::strcmp(decoded.data(), "1") == 0 || std::strcmp(decoded.data(), "true") == 0 ||
            std::strcmp(decoded.data(), "on") == 0) {
            destination = true;
            return true;
        }
        if (std::strcmp(decoded.data(), "0") == 0 || std::strcmp(decoded.data(), "false") == 0) {
            destination = false;
            return true;
        }
        return false;
    }

    template <typename Integer>
    bool integer(const char* name, Integer& destination) const
    {
        static_assert(std::is_integral_v<Integer>);
        std::array<char, 32> decoded{};
        bool found = false;
        if (!value(name, decoded.data(), decoded.size(), found) || !found || decoded[0] == '\0') {
            return false;
        }
        long long parsed = 0;
        const char* begin = decoded.data();
        const char* end = begin + std::strlen(begin);
        const auto conversion = std::from_chars(begin, end, parsed, 10);
        if (conversion.ec != std::errc{} || conversion.ptr != end) {
            return false;
        }
        if constexpr (std::is_signed_v<Integer>) {
            if (parsed < static_cast<long long>(std::numeric_limits<Integer>::min()) ||
                parsed > static_cast<long long>(std::numeric_limits<Integer>::max())) {
                return false;
            }
        } else if (parsed < 0 ||
                   static_cast<unsigned long long>(parsed) >
                       static_cast<unsigned long long>(std::numeric_limits<Integer>::max())) {
            return false;
        }
        destination = static_cast<Integer>(parsed);
        return true;
    }

    bool floating(const char* name, double& destination) const
    {
        std::array<char, 48> decoded{};
        bool found = false;
        if (!value(name, decoded.data(), decoded.size(), found) || !found || decoded[0] == '\0') {
            return false;
        }
        errno = 0;
        char* end = nullptr;
        const double parsed = std::strtod(decoded.data(), &end);
        if (errno != 0 || end == decoded.data() || *end != '\0' || !std::isfinite(parsed)) {
            return false;
        }
        destination = parsed;
        return true;
    }

    bool choice(const char* name, char* destination, std::size_t capacity) const
    {
        bool found = false;
        return value(name, destination, capacity, found) && found;
    }

private:
    static int hex_value(char character)
    {
        if (character >= '0' && character <= '9') {
            return character - '0';
        }
        if (character >= 'a' && character <= 'f') {
            return character - 'a' + 10;
        }
        if (character >= 'A' && character <= 'F') {
            return character - 'A' + 10;
        }
        return -1;
    }

    static bool decode(std::string_view encoded, char* destination, std::size_t capacity)
    {
        if (destination == nullptr || capacity == 0) {
            return false;
        }
        std::size_t output = 0;
        for (std::size_t index = 0; index < encoded.size(); ++index) {
            if (output + 1 >= capacity) {
                return false;
            }
            if (encoded[index] == '+') {
                destination[output++] = ' ';
            } else if (encoded[index] == '%') {
                if (index + 2 >= encoded.size()) {
                    return false;
                }
                const int high = hex_value(encoded[index + 1]);
                const int low = hex_value(encoded[index + 2]);
                if (high < 0 || low < 0 || (high == 0 && low == 0)) {
                    return false;
                }
                destination[output++] = static_cast<char>((high << 4) | low);
                index += 2;
            } else {
                destination[output++] = encoded[index];
            }
        }
        destination[output] = '\0';
        return true;
    }

    bool value(const char* name, char* destination, std::size_t capacity, bool& found) const
    {
        found = false;
        destination[0] = '\0';
        const std::string_view target{name};
        std::size_t offset = 0;
        while (offset < size_) {
            const std::size_t separator = std::string_view(body_ + offset, size_ - offset).find('&');
            const std::size_t pair_size = separator == std::string_view::npos ? size_ - offset : separator;
            const std::string_view pair(body_ + offset, pair_size);
            const std::size_t equals = pair.find('=');
            if (equals == std::string_view::npos) {
                return false;
            }
            std::array<char, 48> decoded_name{};
            if (!decode(pair.substr(0, equals), decoded_name.data(), decoded_name.size())) {
                return false;
            }
            if (std::string_view(decoded_name.data()) == target) {
                found = true;
                return decode(pair.substr(equals + 1), destination, capacity);
            }
            offset += pair_size + (separator == std::string_view::npos ? 0 : 1);
        }
        return true;
    }

    const char* body_;
    std::size_t size_;
};

bool parse_role(const FormReader& form, config::DeviceRole& role)
{
    std::array<char, 16> value{};
    if (!form.choice("role", value.data(), value.size())) {
        return false;
    }
    if (std::strcmp(value.data(), "tx") == 0) {
        role = config::DeviceRole::Transmitter;
        return true;
    }
    if (std::strcmp(value.data(), "rx") == 0) {
        role = config::DeviceRole::Receiver;
        return true;
    }
    return false;
}

bool parse_radio(const FormReader& form, config::RadioConfig& radio)
{
    std::array<char, 16> chip{};
    if (!form.choice("radio_chip", chip.data(), chip.size())) {
        return false;
    }
    radio.chip = std::strcmp(chip.data(), "sx1278") == 0 ? config::RadioChip::Sx1278 :
        (std::strcmp(chip.data(), "sx1276") == 0 ? config::RadioChip::Sx1276 : config::RadioChip::Unspecified);
    return radio.chip != config::RadioChip::Unspecified &&
        form.integer("frequency_hz", radio.frequency_hz) &&
        form.integer("bandwidth_hz", radio.bandwidth_hz) &&
        form.integer("spreading_factor", radio.spreading_factor) &&
        form.integer("coding_rate", radio.coding_rate_denominator) &&
        form.integer("sync_word", radio.sync_word) &&
        form.integer("preamble", radio.preamble_symbols) &&
        form.integer("tx_power", radio.transmit_power_dbm) &&
        form.integer("rx_timeout_ms", radio.receive_timeout_ms) &&
        form.integer("tx_timeout_ms", radio.transmit_timeout_ms);
}

bool parse_tank(const FormReader& form, config::TankConfig& tank)
{
    std::array<char, 24> unit{};
    std::array<char, 24> range_policy{};
    double sensor_to_bottom = 0.0;
    double sensor_to_surface = 0.0;
    if (!(form.floating("sensor_bottom_cm", sensor_to_bottom) &&
          form.floating("sensor_surface_cm", sensor_to_surface) &&
          form.floating("tank_capacity_litres", tank.capacity_litres) &&
          form.choice("volume_unit", unit.data(), unit.size()) &&
          form.choice("range_policy", range_policy.data(), range_policy.size()))) {
        return false;
    }
    tank.tank_id = 1;
    std::snprintf(tank.tank_name.data(), tank.tank_name.size(), "Tank");
    tank.sensor_reference_height_cm = sensor_to_bottom;
    tank.minimum_sensor_distance_cm = sensor_to_surface;
    tank.maximum_sensor_distance_cm = sensor_to_bottom;
    tank.empty_level_cm = 0.0;
    tank.full_level_cm = sensor_to_bottom - sensor_to_surface;
    tank.sensor_offset_cm = 0.0;
    tank.volume_model = config::VolumeModel::CapacityFromPercent;
    tank.volume_litres_per_centimetre = 0.0;
    tank.display_unit = std::strcmp(unit.data(), "litres") == 0 ? config::VolumeUnit::Litres :
        (std::strcmp(unit.data(), "us_gallons") == 0 ? config::VolumeUnit::UsGallons :
         (std::strcmp(unit.data(), "imperial_gallons") == 0 ? config::VolumeUnit::ImperialGallons :
          static_cast<config::VolumeUnit>(0xff)));
    tank.out_of_range_policy = std::strcmp(range_policy.data(), "reject") == 0 ? config::OutOfRangePolicy::Reject :
        (std::strcmp(range_policy.data(), "clamp") == 0 ? config::OutOfRangePolicy::Clamp :
         (std::strcmp(range_policy.data(), "report") == 0 ? config::OutOfRangePolicy::Report : static_cast<config::OutOfRangePolicy>(0xff)));
    return true;
}

bool parse_blynk(const FormReader& form, config::BlynkConfig& blynk)
{
    if (!(form.boolean("blynk_enabled", blynk.enabled) &&
          form.text("blynk_host", blynk.host) && form.integer("blynk_port", blynk.port) &&
          form.text("blynk_template", blynk.template_id) &&
          form.text("blynk_device", blynk.device_name) &&
          form.text("blynk_token", blynk.auth_token, true) &&
          form.integer("blynk_interval_s", blynk.publish_interval_seconds))) {
        return false;
    }
    blynk.virtual_pins.fill(-1);
    for (std::size_t slot = 0; slot < blynk.virtual_pins.size(); ++slot) {
        std::array<char, 24> field{};
        std::int16_t metric = -1;
        std::snprintf(field.data(), field.size(), "blynk_slot_%u", static_cast<unsigned int>(slot));
        if (!form.integer(field.data(), metric) || metric < -1 ||
            metric >= static_cast<std::int16_t>(blynk.virtual_pins.size()) ||
            (metric >= 0 && blynk.virtual_pins[metric] >= 0)) {
            return false;
        }
        if (metric >= 0) blynk.virtual_pins[metric] = static_cast<std::int16_t>(slot);
    }
    return true;
}

}  // namespace

FormResult parse_config_form(
    const char* body,
    std::size_t size,
    const config::AppConfig& base,
    std::uint64_t hardware_node_id)
{
    FormResult result{};
    result.configuration = base;
    if (body == nullptr || size == 0) {
        result.error = FormError::Empty;
        return result;
    }
    if (size > kMaximumFormBodySize) {
        result.error = FormError::TooLarge;
        return result;
    }

    const FormReader form(body, size);
    auto& value = result.configuration;
    value.schema_version = config::kCurrentConfigVersion;
    value.configured = true;
    value.device.node_id = hardware_node_id;
    value.device.display_enabled = true;
    value.device.display_timeout_seconds = 30;
    value.device.provisioning_long_press_ms = 5000;
    value.sensor = config::SensorConfig{};
    value.battery = config::BatteryConfig{};
    value.wifi.reconnect_minimum_ms = 1000;
    value.wifi.reconnect_maximum_ms = 60000;
    std::snprintf(value.wifi.hostname.data(), value.wifi.hostname.size(), "tank-monitor");
    if (!(parse_role(form, value.device.role) && parse_radio(form, value.radio))) {
        result.error = FormError::InvalidValue;
        return result;
    }
    bool valid_role_settings = false;
    if (value.device.role == config::DeviceRole::Transmitter) {
        std::uint32_t interval_minutes = 30;
        valid_role_settings = parse_tank(form, value.tank) &&
            form.integer("measurement_interval_minutes", interval_minutes) &&
            interval_minutes >= 1 && interval_minutes <= 1440;
        if (valid_role_settings) value.sensor.measurement_interval_seconds = interval_minutes * 60U;
    } else {
        valid_role_settings = form.text("wifi_ssid", value.wifi.ssid) &&
            form.text("wifi_password", value.wifi.password, true) && parse_blynk(form, value.blynk);
    }
    if (!valid_role_settings) {
        result.error = FormError::InvalidValue;
        return result;
    }

    const auto validation = config_validation::validate(value);
    if (!validation.valid()) {
        result.error = FormError::ValidationFailed;
        result.validation_error = validation.error;
    }
    return result;
}

}  // namespace tank_monitor::provisioning