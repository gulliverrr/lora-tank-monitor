#pragma once

#include "config/alarm_config.hpp"

#include <cstdint>

namespace tank_monitor::alarms {

enum class AlarmPhase : std::uint8_t {
    Disabled,
    Inactive,
    Pending,
    Active,
    Clearing,
};

enum class AlarmEvent : std::uint8_t {
    None,
    Activated,
    Cleared,
    Reminder,
};

enum class PolicyError : std::uint8_t {
    None,
    InvalidDirection,
    NonFiniteThreshold,
    InvalidHysteresis,
    InvalidConfirmationCount,
};

struct AlarmUpdate {
    AlarmPhase phase{AlarmPhase::Disabled};
    AlarmEvent event{AlarmEvent::None};
    std::uint16_t confirmation_count{0};

    [[nodiscard]] constexpr bool active() const
    {
        return phase == AlarmPhase::Active || phase == AlarmPhase::Clearing;
    }
};

[[nodiscard]] PolicyError validate(const config::AlarmPolicy& policy);

class AlarmStateMachine {
public:
    explicit AlarmStateMachine(config::AlarmPolicy policy);

    [[nodiscard]] AlarmUpdate update(double value, std::uint32_t now_seconds);
    [[nodiscard]] AlarmUpdate snapshot() const;
    void configure(config::AlarmPolicy policy);
    void reset();

private:
    [[nodiscard]] bool trigger_condition(double value) const;
    [[nodiscard]] bool clear_condition(double value) const;

    config::AlarmPolicy policy_{};
    AlarmPhase phase_{AlarmPhase::Disabled};
    std::uint16_t confirmation_count_{0};
    std::uint32_t last_notification_seconds_{0};
};

}  // namespace tank_monitor::alarms