#include "config/app_config.hpp"

namespace tank_monitor::config {

AppConfig default_config()
{
    AppConfig configuration{};
    configuration.alarms.low_level.direction = AlarmDirection::BelowOrEqual;
    configuration.alarms.critical_high.direction = AlarmDirection::AboveOrEqual;
    configuration.alarms.low_battery.direction = AlarmDirection::BelowOrEqual;
    configuration.alarms.stale_data.direction = AlarmDirection::AboveOrEqual;
    return configuration;
}

}  // namespace tank_monitor::config