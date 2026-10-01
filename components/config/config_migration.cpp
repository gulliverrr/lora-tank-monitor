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

    // Schema 1 records are upgraded by the storage codec, which drops legacy tank IDs.
    return MigrationStatus::UnsupportedOlderVersion;
}

}  // namespace tank_monitor::config