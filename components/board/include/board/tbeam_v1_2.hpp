#pragma once

#include "driver/gpio.h"

#include <cstdint>

namespace tank_monitor::board::tbeam_v1_2 {

constexpr char kProfileName[] = "T-Beam AXP2101 V1.2 SX1278";

constexpr gpio_num_t kI2cSda = GPIO_NUM_21;
constexpr gpio_num_t kI2cScl = GPIO_NUM_22;
constexpr std::uint8_t kPmuAddress = 0x34;
constexpr std::uint8_t kDisplayAddress = 0x3c;
constexpr gpio_num_t kPmuInterrupt = GPIO_NUM_35;
constexpr gpio_num_t kUserButton = GPIO_NUM_38;

constexpr gpio_num_t kRadioSck = GPIO_NUM_5;
constexpr gpio_num_t kRadioMiso = GPIO_NUM_19;
constexpr gpio_num_t kRadioMosi = GPIO_NUM_27;
constexpr gpio_num_t kRadioChipSelect = GPIO_NUM_18;
constexpr gpio_num_t kRadioReset = GPIO_NUM_23;
constexpr gpio_num_t kRadioDio0 = GPIO_NUM_26;
constexpr gpio_num_t kRadioDio1 = GPIO_NUM_33;
constexpr gpio_num_t kRadioDio2 = GPIO_NUM_32;

constexpr gpio_num_t kGpsReceive = GPIO_NUM_34;
constexpr gpio_num_t kGpsTransmit = GPIO_NUM_12;

constexpr gpio_num_t kUltrasonicTrigger = GPIO_NUM_13;
constexpr gpio_num_t kUltrasonicEcho = GPIO_NUM_25;
constexpr gpio_num_t kSensorPowerEnable = GPIO_NUM_14;

}  // namespace tank_monitor::board::tbeam_v1_2