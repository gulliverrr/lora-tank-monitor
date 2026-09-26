#pragma once

#include "config/app_config.hpp"

#include <cstdint>

namespace tank_monitor::storage {

enum class StoreStatus : std::uint8_t {
    Ok,
    NotFound,
    NotInitialized,
    InvalidRecord,
    StorageError,
    VerificationFailed,
};

struct LoadResult {
    StoreStatus status{StoreStatus::NotInitialized};
    config::AppConfig configuration{};

    [[nodiscard]] constexpr bool loaded() const
    {
        return status == StoreStatus::Ok;
    }
};

class ConfigStore {
public:
    [[nodiscard]] StoreStatus initialize();
    [[nodiscard]] LoadResult load() const;
    [[nodiscard]] StoreStatus save(const config::AppConfig& configuration);
    [[nodiscard]] StoreStatus factory_reset();

private:
    bool initialized_{false};
};

}  // namespace tank_monitor::storage