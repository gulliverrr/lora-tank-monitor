#pragma once

#include "config/app_config.hpp"
#include "provisioning/config_form.hpp"
#include "storage/config_store.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace tank_monitor::provisioning {

class ProvisioningManager {
public:
    [[nodiscard]] bool start(
        std::uint64_t hardware_node_id,
        const config::AppConfig& base_configuration,
        storage::ConfigStore& config_store);
    void stop();

    [[nodiscard]] FormResult stage(const char* body, std::size_t size);
    [[nodiscard]] storage::StoreStatus save_staged();
    [[nodiscard]] storage::StoreStatus clear_pairing();
    [[nodiscard]] storage::StoreStatus factory_reset();
    [[nodiscard]] bool session_matches(const char* value) const;

    [[nodiscard]] bool running() const;
    [[nodiscard]] bool has_valid_stage() const;
    [[nodiscard]] const config::AppConfig& visible_configuration() const;
    [[nodiscard]] std::uint32_t session_token() const;
    [[nodiscard]] const char* access_point_name() const;
    [[nodiscard]] const char* ip_address() const;

private:
    std::array<char, 33> access_point_name_{};
    config::AppConfig base_configuration_{};
    config::AppConfig staged_configuration_{};
    storage::ConfigStore* config_store_{nullptr};
    std::uint64_t hardware_node_id_{0};
    std::uint32_t session_token_{0};
    bool has_valid_stage_{false};
    bool running_{false};
};

}  // namespace tank_monitor::provisioning