#include "board/power_manager.hpp"

#include "board/board.hpp"

#define XPOWERS_CHIP_AXP2101
#include "XPowersLib.h"

#include "esp_log.h"

namespace tank_monitor::board {
namespace {

constexpr char kLogTag[] = "power";
XPowersPMU power;
bool initialized = false;

}  // namespace

bool initialize_power_manager()
{
    if (initialized) {
        return true;
    }

    const i2c_master_bus_handle_t bus = i2c_bus_handle();
    if (bus == nullptr || !power.begin(bus, AXP2101_SLAVE_ADDRESS)) {
        ESP_LOGE(kLogTag, "AXP2101 initialization failed");
        return false;
    }

    if (!power.enableBattDetection() || !power.enableBattVoltageMeasure()) {
        ESP_LOGE(kLogTag, "Failed to enable battery telemetry");
        return false;
    }
    if (!power.disableALDO3()) {
        ESP_LOGE(kLogTag, "Failed to disable unused GPS rail");
        return false;
    }

    initialized = true;
    ESP_LOGI(kLogTag, "AXP2101 chip ID: 0x%02x", power.getChipID());
    return true;
}

PowerStatus read_power_status()
{
    PowerStatus status{};
    status.initialized = initialize_power_manager();
    if (!status.initialized) {
        return status;
    }

    status.battery_connected = power.isBatteryConnect();
    status.battery_millivolts = power.getBattVoltage();
    status.radio_rail_enabled = power.isEnableALDO2();
    status.radio_rail_millivolts = power.getALDO2Voltage();
    status.gps_rail_enabled = power.isEnableALDO3();
    status.gps_rail_millivolts = power.getALDO3Voltage();

    ESP_LOGI(kLogTag,
             "Battery: %s, %u mV",
             status.battery_connected ? "connected" : "absent",
             status.battery_millivolts);
    ESP_LOGI(kLogTag,
             "Rails: LoRa=%s/%u mV GPS=%s/%u mV",
             status.radio_rail_enabled ? "on" : "off",
             status.radio_rail_millivolts,
             status.gps_rail_enabled ? "on" : "off",
             status.gps_rail_millivolts);
    return status;
}

}  // namespace tank_monitor::board