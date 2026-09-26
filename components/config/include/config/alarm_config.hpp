#pragma once

#include <cstdint>

namespace tank_monitor::config {

enum class AlarmDirection : std::uint8_t {
    BelowOrEqual,
    AboveOrEqual,
};

struct AlarmPolicy {
    bool enabled{false};
    AlarmDirection direction{AlarmDirection::AboveOrEqual};
    double trigger_threshold{0.0};
    double clear_hysteresis{0.0};
    std::uint16_t consecutive_confirmations{1};
    std::uint32_t reminder_interval_seconds{0};
};

}  // namespace tank_monitor::config