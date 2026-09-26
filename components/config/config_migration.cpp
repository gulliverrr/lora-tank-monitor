#include "config/config_migration.hpp"

namespace tank_monitor::config {

MigrationStatus migrate_to_current(AppConfig& configuration)
{
    if (configuration.schema_version == kCurrentConfigVersion) {
        return MigrationStatus::Current;
    }
    if (configuration.schema_version > kCurrentConfigVersion) {
        return MigrationStatus::FutureVersion;
    }

    // Schema version 1 is the first persistent format. Add sequential
    // transformations here before incrementing kCurrentConfigVersion.
    return MigrationStatus::UnsupportedOlderVersion;
}

}  // namespace tank_monitor::config