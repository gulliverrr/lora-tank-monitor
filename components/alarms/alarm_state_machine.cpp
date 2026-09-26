#include "alarms/alarm_state_machine.hpp"

#include <cmath>
#include <limits>

namespace tank_monitor::alarms {

PolicyError validate(const config::AlarmPolicy& policy)
{
    if (policy.direction != config::AlarmDirection::BelowOrEqual &&
        policy.direction != config::AlarmDirection::AboveOrEqual) {
        return PolicyError::InvalidDirection;
    }
    if (!std::isfinite(policy.trigger_threshold)) {
        return PolicyError::NonFiniteThreshold;
    }
    if (!std::isfinite(policy.clear_hysteresis) || policy.clear_hysteresis < 0.0) {
        return PolicyError::InvalidHysteresis;
    }
    if (policy.consecutive_confirmations == 0) {
        return PolicyError::InvalidConfirmationCount;
    }
    return PolicyError::None;
}

AlarmStateMachine::AlarmStateMachine(config::AlarmPolicy policy)
{
    configure(policy);
}

AlarmUpdate AlarmStateMachine::update(double value, std::uint32_t now_seconds)
{
    if (!policy_.enabled || validate(policy_) != PolicyError::None) {
        reset();
        return snapshot();
    }

    if (!std::isfinite(value)) {
        confirmation_count_ = 0;
        if (phase_ == AlarmPhase::Pending) {
            phase_ = AlarmPhase::Inactive;
        } else if (phase_ == AlarmPhase::Clearing) {
            phase_ = AlarmPhase::Active;
        }
        return snapshot();
    }

    const bool currently_active =
        phase_ == AlarmPhase::Active || phase_ == AlarmPhase::Clearing;
    if (!currently_active) {
        if (!trigger_condition(value)) {
            phase_ = AlarmPhase::Inactive;
            confirmation_count_ = 0;
            return snapshot();
        }

        phase_ = AlarmPhase::Pending;
        ++confirmation_count_;
        if (confirmation_count_ >= policy_.consecutive_confirmations) {
            phase_ = AlarmPhase::Active;
            confirmation_count_ = 0;
            last_notification_seconds_ = now_seconds;
            return {phase_, AlarmEvent::Activated, confirmation_count_};
        }
        return snapshot();
    }

    if (clear_condition(value)) {
        phase_ = AlarmPhase::Clearing;
        ++confirmation_count_;
        if (confirmation_count_ >= policy_.consecutive_confirmations) {
            phase_ = AlarmPhase::Inactive;
            confirmation_count_ = 0;
            return {phase_, AlarmEvent::Cleared, confirmation_count_};
        }
        return snapshot();
    }

    phase_ = AlarmPhase::Active;
    confirmation_count_ = 0;
    if (policy_.reminder_interval_seconds > 0 &&
        now_seconds - last_notification_seconds_ >= policy_.reminder_interval_seconds) {
        last_notification_seconds_ = now_seconds;
        return {phase_, AlarmEvent::Reminder, confirmation_count_};
    }
    return snapshot();
}

AlarmUpdate AlarmStateMachine::snapshot() const
{
    return {phase_, AlarmEvent::None, confirmation_count_};
}

void AlarmStateMachine::configure(config::AlarmPolicy policy)
{
    policy_ = policy;
    reset();
}

void AlarmStateMachine::reset()
{
    phase_ = policy_.enabled && validate(policy_) == PolicyError::None
        ? AlarmPhase::Inactive
        : AlarmPhase::Disabled;
    confirmation_count_ = 0;
    last_notification_seconds_ = 0;
}

bool AlarmStateMachine::trigger_condition(double value) const
{
    if (policy_.direction == config::AlarmDirection::BelowOrEqual) {
        return value <= policy_.trigger_threshold;
    }
    return value >= policy_.trigger_threshold;
}

bool AlarmStateMachine::clear_condition(double value) const
{
    if (policy_.direction == config::AlarmDirection::BelowOrEqual) {
        return value >= policy_.trigger_threshold + policy_.clear_hysteresis;
    }
    return value <= policy_.trigger_threshold - policy_.clear_hysteresis;
}

}  // namespace tank_monitor::alarms