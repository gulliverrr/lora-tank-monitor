#include "alarms/alarm_state_machine.hpp"

#include <iostream>
#include <limits>

namespace {

using tank_monitor::alarms::AlarmEvent;
using tank_monitor::alarms::AlarmPhase;
using tank_monitor::alarms::AlarmStateMachine;
using tank_monitor::alarms::PolicyError;
using tank_monitor::config::AlarmDirection;
using tank_monitor::config::AlarmPolicy;

int failures = 0;

void expect(bool condition, const char* description)
{
    if (!condition) {
        std::cerr << "FAIL: " << description << '\n';
        ++failures;
    }
}

AlarmPolicy high_policy()
{
    AlarmPolicy policy{};
    policy.enabled = true;
    policy.direction = AlarmDirection::AboveOrEqual;
    policy.trigger_threshold = 95.0;
    policy.clear_hysteresis = 5.0;
    policy.consecutive_confirmations = 3;
    policy.reminder_interval_seconds = 60;
    return policy;
}

void test_high_alarm_confirmation_and_clear()
{
    AlarmStateMachine alarm(high_policy());
    expect(alarm.update(95.0, 10).phase == AlarmPhase::Pending, "first high sample is pending");
    expect(alarm.update(96.0, 11).confirmation_count == 2, "second high sample increments count");
    const auto activated = alarm.update(97.0, 12);
    expect(activated.phase == AlarmPhase::Active, "third high sample activates alarm");
    expect(activated.event == AlarmEvent::Activated, "activation event emitted once");

    expect(alarm.update(94.0, 20).phase == AlarmPhase::Active,
           "hysteresis prevents premature clearing");
    expect(alarm.update(90.0, 21).phase == AlarmPhase::Clearing, "first clear sample is pending");
    expect(alarm.update(89.0, 22).confirmation_count == 2, "second clear sample increments count");
    const auto cleared = alarm.update(90.0, 23);
    expect(cleared.phase == AlarmPhase::Inactive, "third clear sample clears alarm");
    expect(cleared.event == AlarmEvent::Cleared, "clear event emitted once");
}

void test_confirmation_must_be_consecutive()
{
    AlarmStateMachine alarm(high_policy());
       expect(alarm.update(96.0, 1).phase == AlarmPhase::Pending,
                 "first sample starts confirmation");
    expect(alarm.update(94.0, 2).phase == AlarmPhase::Inactive,
           "normal sample resets trigger confirmation");
    expect(alarm.update(96.0, 3).confirmation_count == 1,
           "confirmation restarts after interrupted sequence");

    const auto nan = alarm.update(std::numeric_limits<double>::quiet_NaN(), 4);
    expect(nan.phase == AlarmPhase::Inactive, "invalid sample cannot activate alarm");
}

void test_low_alarm_and_reminder()
{
    auto policy = high_policy();
    policy.direction = AlarmDirection::BelowOrEqual;
    policy.trigger_threshold = 20.0;
    policy.clear_hysteresis = 3.0;
    policy.consecutive_confirmations = 1;
    AlarmStateMachine alarm(policy);

    expect(alarm.update(20.0, 100).event == AlarmEvent::Activated,
           "low threshold activates at equality");
    expect(alarm.update(21.0, 159).event == AlarmEvent::None,
           "reminder waits for configured interval");
    expect(alarm.update(21.0, 160).event == AlarmEvent::Reminder,
           "active alarm emits reminder");
    expect(alarm.update(23.0, 161).event == AlarmEvent::Cleared,
           "low alarm clears at hysteresis boundary");
}

void test_disabled_and_invalid_policy()
{
    AlarmPolicy disabled{};
    AlarmStateMachine alarm(disabled);
    expect(alarm.update(100.0, 1).phase == AlarmPhase::Disabled,
           "disabled alarm remains disabled");

    auto invalid = high_policy();
    invalid.consecutive_confirmations = 0;
    expect(tank_monitor::alarms::validate(invalid) == PolicyError::InvalidConfirmationCount,
           "zero confirmation count is rejected");
    alarm.configure(invalid);
    expect(alarm.snapshot().phase == AlarmPhase::Disabled,
           "invalid policy cannot run");
}

}  // namespace

int main()
{
    test_high_alarm_confirmation_and_clear();
    test_confirmation_must_be_consecutive();
    test_low_alarm_and_reminder();
    test_disabled_and_invalid_policy();

    if (failures == 0) {
        std::cout << "All alarm state machine tests passed\n";
    }
    return failures == 0 ? 0 : 1;
}