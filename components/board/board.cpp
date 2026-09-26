#include "board/board.hpp"

#include "board/tbeam_v1_2.hpp"

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "esp_flash.h"

#include <array>

namespace tank_monitor::board {
namespace {

constexpr char kLogTag[] = "board";
constexpr std::uint32_t kI2cFrequencyHz = 100000;
constexpr int kProbeTimeoutMs = 50;
i2c_master_bus_handle_t i2c_bus = nullptr;

bool initialize_i2c_bus()
{
    if (i2c_bus != nullptr) {
        return true;
    }

    const i2c_master_bus_config_t bus_configuration{
        .i2c_port = I2C_NUM_0,
        .sda_io_num = tbeam_v1_2::kI2cSda,
        .scl_io_num = tbeam_v1_2::kI2cScl,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,
        .trans_queue_depth = 0,
        .flags = {
            .enable_internal_pullup = 1,
            .allow_pd = 0,
        },
    };

    const esp_err_t error = i2c_new_master_bus(&bus_configuration, &i2c_bus);
    if (error != ESP_OK) {
        ESP_LOGE(kLogTag, "Failed to initialize I2C bus: %s", esp_err_to_name(error));
        i2c_bus = nullptr;
        return false;
    }
    return true;
}

DevicePresence probe(i2c_master_bus_handle_t bus, std::uint8_t address)
{
    const esp_err_t error = i2c_master_probe(bus, address, kProbeTimeoutMs);
    if (error == ESP_OK) {
        return DevicePresence::Present;
    }
    if (error == ESP_ERR_NOT_FOUND || error == ESP_ERR_TIMEOUT) {
        return DevicePresence::Absent;
    }
    ESP_LOGE(kLogTag, "I2C probe 0x%02x failed: %s", address, esp_err_to_name(error));
    return DevicePresence::Unknown;
}

std::uint64_t read_hardware_node_id()
{
    std::array<std::uint8_t, 6> mac{};
    if (esp_efuse_mac_get_default(mac.data()) != ESP_OK) {
        return 0;
    }

    std::uint64_t node_id = 0;
    for (const std::uint8_t octet : mac) {
        node_id = node_id << 8U | octet;
    }
    return node_id;
}

const char* presence_name(DevicePresence presence)
{
    switch (presence) {
    case DevicePresence::Unknown:
        return "ERROR";
    case DevicePresence::Absent:
        return "ABSENT";
    case DevicePresence::Present:
        return "OK";
    }
    return "ERROR";
}

}  // namespace

const char* profile_name()
{
    return tbeam_v1_2::kProfileName;
}

i2c_master_bus_handle_t i2c_bus_handle()
{
    return initialize_i2c_bus() ? i2c_bus : nullptr;
}

SelfTestResult run_self_test()
{
    SelfTestResult result{};
    result.hardware_node_id = read_hardware_node_id();
    result.psram_size_bytes =
        static_cast<std::uint32_t>(heap_caps_get_total_size(MALLOC_CAP_SPIRAM));
    if (esp_flash_get_size(nullptr, &result.flash_size_bytes) != ESP_OK) {
        ESP_LOGE(kLogTag, "Failed to read flash size");
        result.flash_size_bytes = 0;
    }

    const gpio_config_t button_configuration{
        .pin_bit_mask = 1ULL << static_cast<unsigned int>(tbeam_v1_2::kUserButton),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    if (gpio_config(&button_configuration) != ESP_OK) {
        ESP_LOGE(kLogTag, "Failed to configure user button");
        return result;
    }
    result.user_button_pressed = gpio_get_level(tbeam_v1_2::kUserButton) == 0;

    i2c_master_bus_handle_t bus = i2c_bus_handle();
    if (bus == nullptr) {
        return result;
    }

    result.pmu = probe(bus, tbeam_v1_2::kPmuAddress);
    result.display = probe(bus, tbeam_v1_2::kDisplayAddress);
    result.initialized = true;

    ESP_LOGI(kLogTag, "Profile: %s", profile_name());
    ESP_LOGI(kLogTag, "Node ID: %012llx", static_cast<unsigned long long>(result.hardware_node_id));
    ESP_LOGI(kLogTag, "Flash: %lu bytes", static_cast<unsigned long>(result.flash_size_bytes));
    ESP_LOGI(kLogTag, "Usable PSRAM heap: %lu bytes",
             static_cast<unsigned long>(result.psram_size_bytes));
    ESP_LOGI(kLogTag, "PMU 0x%02x: %s", tbeam_v1_2::kPmuAddress, presence_name(result.pmu));
    ESP_LOGI(kLogTag, "OLED 0x%02x: %s", tbeam_v1_2::kDisplayAddress, presence_name(result.display));
    ESP_LOGI(kLogTag, "Button GPIO%d: %s",
             static_cast<int>(tbeam_v1_2::kUserButton),
             result.user_button_pressed ? "PRESSED" : "released");

    return result;
}

}  // namespace tank_monitor::board