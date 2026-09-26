#pragma once

#include "config/app_config.hpp"

#include <cstdint>

namespace tank_monitor::config {

enum class MigrationStatus : std::uint8_t {
    Current,
    Migrated,
    UnsupportedOlderVersion,
    FutureVersion,
};

[[nodiscard]] MigrationStatus migrate_to_current(AppConfig& configuration);

}  // namespace tank_monitor::config