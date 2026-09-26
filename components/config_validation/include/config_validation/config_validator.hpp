#pragma once

#include "config/app_config.hpp"

#include <cstdint>

namespace tank_monitor::config_validation {

enum class ConfigError : std::uint8_t {
    None,
    UnsupportedVersion,
    NotConfigured,
    InvalidRole,
    InvalidNodeId,
    InvalidWifi,
    InvalidRadio,
    InvalidPairing,
    InvalidTank,
    InvalidSensor,
    InvalidAlarm,
    InvalidBattery,
    InvalidBlynk,
};

struct ConfigValidationResult {
    ConfigError error{ConfigError::None};

    [[nodiscard]] constexpr bool valid() const
    {
        return error == ConfigError::None;
    }
};

[[nodiscard]] ConfigValidationResult validate(const config::AppConfig& configuration);

}  // namespace tank_monitor::config_validation