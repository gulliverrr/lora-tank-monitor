#include "storage/config_store.hpp"

#include "storage/config_codec.hpp"
#include "storage/record_selection.hpp"

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#include <algorithm>
#include <memory>
#include <new>

namespace tank_monitor::storage {
namespace {

constexpr char kLogTag[] = "config_store";
constexpr char kPartition[] = "app_cfg";
constexpr char kNamespace[] = "config";
constexpr char kSlotZeroKey[] = "slot0";
constexpr char kSlotOneKey[] = "slot1";
constexpr char kActiveKey[] = "active";

const char* key_for(Slot slot)
{
    return slot == Slot::One ? kSlotOneKey : kSlotZeroKey;
}

struct StoredSlot {
    SlotState state{};
    config::AppConfig configuration{};
};

void read_slot(nvs_handle_t handle, Slot slot, StoredSlot& result)
{
    result = {};
    std::size_t size = 0;
    esp_err_t error = nvs_get_blob(handle, key_for(slot), nullptr, &size);
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        return;
    }
    if (error != ESP_OK || size == 0 || size > kMaximumConfigRecordSize) {
        ESP_LOGW(kLogTag, "Slot %u has invalid size", static_cast<unsigned int>(slot));
        return;
    }

    auto record = std::unique_ptr<ConfigRecord>(new (std::nothrow) ConfigRecord{});
    if (!record) {
        return;
    }
    error = nvs_get_blob(handle, key_for(slot), record->bytes.data(), &size);
    if (error != ESP_OK) {
        ESP_LOGW(kLogTag, "Slot %u read failed: %s",
                 static_cast<unsigned int>(slot), esp_err_to_name(error));
        return;
    }
    const DecodeResult decoded = decode(record->bytes.data(), size);
    if (!decoded.valid()) {
        ESP_LOGW(kLogTag, "Slot %u record invalid: %u",
                 static_cast<unsigned int>(slot), static_cast<unsigned int>(decoded.error));
        return;
    }
    result.state = {true, decoded.configuration.generation};
    result.configuration = decoded.configuration;
}

StoreStatus open_store(nvs_open_mode_t mode, nvs_handle_t& handle)
{
    const esp_err_t error = nvs_open_from_partition(kPartition, kNamespace, mode, &handle);
    if (error == ESP_OK) {
        return StoreStatus::Ok;
    }
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        return StoreStatus::NotFound;
    }
    ESP_LOGE(kLogTag, "NVS open failed: %s", esp_err_to_name(error));
    return StoreStatus::StorageError;
}

}  // namespace

StoreStatus ConfigStore::initialize()
{
    esp_err_t error = nvs_flash_init_partition(kPartition);
    if (error == ESP_ERR_NVS_NO_FREE_PAGES || error == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(kLogTag, "Application NVS requires recovery: %s", esp_err_to_name(error));
        error = nvs_flash_erase_partition(kPartition);
        if (error == ESP_OK) {
            error = nvs_flash_init_partition(kPartition);
        }
    }
    if (error != ESP_OK) {
        ESP_LOGE(kLogTag, "Application NVS initialization failed: %s", esp_err_to_name(error));
        return StoreStatus::StorageError;
    }
    initialized_ = true;
    return StoreStatus::Ok;
}

LoadResult ConfigStore::load() const
{
    LoadResult result{};
    result.configuration = config::default_config();
    if (!initialized_) {
        return result;
    }

    nvs_handle_t handle = 0;
    if (open_store(NVS_READONLY, handle) != StoreStatus::Ok) {
        result.status = StoreStatus::NotFound;
        return result;
    }
    auto zero = std::unique_ptr<StoredSlot>(new (std::nothrow) StoredSlot{});
    auto one = std::unique_ptr<StoredSlot>(new (std::nothrow) StoredSlot{});
    if (!zero || !one) {
        nvs_close(handle);
        result.status = StoreStatus::StorageError;
        return result;
    }
    read_slot(handle, Slot::Zero, *zero);
    read_slot(handle, Slot::One, *one);
    std::uint8_t active_value = static_cast<std::uint8_t>(Slot::None);
    static_cast<void>(nvs_get_u8(handle, kActiveKey, &active_value));
    nvs_close(handle);

    const Slot active = active_value == 1 ? Slot::One :
        (active_value == 0 ? Slot::Zero : Slot::None);
    const Slot selected = select_newest(zero->state, one->state, active);
    if (selected == Slot::None) {
        result.status = StoreStatus::NotFound;
        return result;
    }
    result.configuration = selected == Slot::Zero ? zero->configuration : one->configuration;
    result.status = StoreStatus::Ok;
    ESP_LOGI(kLogTag, "Loaded slot %u generation %lu",
             static_cast<unsigned int>(selected),
             static_cast<unsigned long>(result.configuration.generation));
    return result;
}

StoreStatus ConfigStore::save(const config::AppConfig& configuration)
{
    if (!initialized_) {
        return StoreStatus::NotInitialized;
    }

    nvs_handle_t handle = 0;
    StoreStatus status = open_store(NVS_READWRITE, handle);
    if (status != StoreStatus::Ok) {
        return status;
    }
    auto zero = std::unique_ptr<StoredSlot>(new (std::nothrow) StoredSlot{});
    auto one = std::unique_ptr<StoredSlot>(new (std::nothrow) StoredSlot{});
    if (!zero || !one) {
        nvs_close(handle);
        return StoreStatus::StorageError;
    }
    read_slot(handle, Slot::Zero, *zero);
    read_slot(handle, Slot::One, *one);
    std::uint8_t active_value = static_cast<std::uint8_t>(Slot::None);
    static_cast<void>(nvs_get_u8(handle, kActiveKey, &active_value));
    const Slot active = active_value == 1 ? Slot::One :
        (active_value == 0 ? Slot::Zero : Slot::None);
    const Slot newest = select_newest(zero->state, one->state, active);
    const Slot target = newest == Slot::None ? Slot::Zero : opposite(newest);
    const std::uint32_t newest_generation = newest == Slot::Zero ? zero->state.generation :
        (newest == Slot::One ? one->state.generation : 0);

    auto next = std::unique_ptr<config::AppConfig>(new (std::nothrow) config::AppConfig(configuration));
    auto record = std::unique_ptr<ConfigRecord>(new (std::nothrow) ConfigRecord{});
    auto verification = std::unique_ptr<StoredSlot>(new (std::nothrow) StoredSlot{});
    if (!next || !record || !verification) {
        nvs_close(handle);
        return StoreStatus::StorageError;
    }
    next->schema_version = config::kCurrentConfigVersion;
    next->generation = newest_generation + 1U;
    if (encode(*next, *record) != CodecError::None) {
        nvs_close(handle);
        return StoreStatus::InvalidRecord;
    }

    esp_err_t error = nvs_set_blob(handle, key_for(target), record->bytes.data(), record->size);
    if (error == ESP_OK) {
        error = nvs_commit(handle);
    }
    if (error != ESP_OK) {
        ESP_LOGE(kLogTag, "Slot write failed: %s", esp_err_to_name(error));
        nvs_close(handle);
        return StoreStatus::StorageError;
    }

    read_slot(handle, target, *verification);
    if (!verification->state.valid || verification->state.generation != next->generation) {
        ESP_LOGE(kLogTag, "Slot verification failed");
        nvs_close(handle);
        return StoreStatus::VerificationFailed;
    }
    error = nvs_set_u8(handle, kActiveKey, static_cast<std::uint8_t>(target));
    if (error == ESP_OK) {
        error = nvs_commit(handle);
    }
    nvs_close(handle);
    if (error != ESP_OK) {
        ESP_LOGE(kLogTag, "Active marker update failed: %s", esp_err_to_name(error));
        return StoreStatus::StorageError;
    }
    ESP_LOGI(kLogTag, "Saved slot %u generation %lu",
             static_cast<unsigned int>(target), static_cast<unsigned long>(next->generation));
    return StoreStatus::Ok;
}

StoreStatus ConfigStore::factory_reset()
{
    if (!initialized_) {
        return StoreStatus::NotInitialized;
    }
    nvs_handle_t handle = 0;
    StoreStatus status = open_store(NVS_READWRITE, handle);
    if (status != StoreStatus::Ok) {
        return status;
    }
    esp_err_t error = nvs_erase_all(handle);
    if (error == ESP_OK) {
        error = nvs_commit(handle);
    }
    nvs_close(handle);
    return error == ESP_OK ? StoreStatus::Ok : StoreStatus::StorageError;
}

}  // namespace tank_monitor::storage