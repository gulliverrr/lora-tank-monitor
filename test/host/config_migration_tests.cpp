#include "config/config_migration.hpp"

#include <iostream>

namespace {

int failures = 0;

void expect(bool condition, const char* description)
{
    if (!condition) {
        std::cerr << "FAIL: " << description << '\n';
        ++failures;
    }
}

}  // namespace

int main()
{
    using tank_monitor::config::MigrationStatus;

    auto current = tank_monitor::config::default_config();
    expect(tank_monitor::config::migrate_to_current(current) == MigrationStatus::Current,
           "current schema requires no migration");

    auto older = tank_monitor::config::default_config();
    older.schema_version = 0;
    expect(tank_monitor::config::migrate_to_current(older) ==
               MigrationStatus::UnsupportedOlderVersion,
           "pre-release schema zero is not guessed");

    auto future = tank_monitor::config::default_config();
    future.schema_version = tank_monitor::config::kCurrentConfigVersion + 1;
    expect(tank_monitor::config::migrate_to_current(future) == MigrationStatus::FutureVersion,
           "future schema is not downgraded");

    if (failures == 0) {
        std::cout << "All configuration migration tests passed\n";
    }
    return failures == 0 ? 0 : 1;
}