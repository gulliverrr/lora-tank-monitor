#pragma once

#include "config/app_config.hpp"
#include "config_validation/config_validator.hpp"

#include <cstddef>
#include <cstdint>

namespace tank_monitor::provisioning {

constexpr std::size_t kMaximumFormBodySize = 6144;

enum class FormError : std::uint8_t {
    None,
    Empty,
    TooLarge,
    MalformedEncoding,
    FieldTooLong,
    InvalidValue,
    ValidationFailed,
};

struct FormResult {
    FormError error{FormError::None};
    config_validation::ConfigError validation_error{config_validation::ConfigError::None};
    config::AppConfig configuration{};

    [[nodiscard]] constexpr bool valid() const
    {
        return error == FormError::None;
    }
};

[[nodiscard]] FormResult parse_config_form(
    const char* body,
    std::size_t size,
    const config::AppConfig& base,
    std::uint64_t hardware_node_id);

}  // namespace tank_monitor::provisioning