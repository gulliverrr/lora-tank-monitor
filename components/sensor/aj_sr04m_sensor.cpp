#include "sensor/aj_sr04m_sensor.hpp"

#include "board/tbeam_v1_2.hpp"
#include "sensor/ultrasonic_math.hpp"

#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <algorithm>

namespace tank_monitor::sensor {
namespace {

constexpr char kLogTag[] = "ultrasonic";
constexpr std::uint32_t kTriggerPulseMicroseconds = 12;
constexpr std::uint32_t kMinimumValidPulseMicroseconds = 100;

struct CaptureState {
    volatile std::int64_t rising_edge_microseconds{0};
    volatile std::uint32_t pulse_microseconds{0};
    TaskHandle_t waiting_task{nullptr};
};

CaptureState capture;

void IRAM_ATTR echo_interrupt(void*)
{
    const std::int64_t now = esp_timer_get_time();
    if (gpio_get_level(board::tbeam_v1_2::kUltrasonicEcho) != 0) {
        capture.rising_edge_microseconds = now;
        return;
    }

    const std::int64_t rising = capture.rising_edge_microseconds;
    if (rising <= 0 || now <= rising) {
        return;
    }
    capture.pulse_microseconds = static_cast<std::uint32_t>(now - rising);
    BaseType_t task_woken = pdFALSE;
    if (capture.waiting_task != nullptr) {
        vTaskNotifyGiveFromISR(capture.waiting_task, &task_woken);
    }
    portYIELD_FROM_ISR(task_woken);
}

}  // namespace

bool AjSr04mSensor::set_power_enabled(bool enabled)
{
    const gpio_num_t sensor_power_enable = board::tbeam_v1_2::kSensorPowerEnable;
    const gpio_config_t configuration{
        .pin_bit_mask = 1ULL << static_cast<unsigned int>(sensor_power_enable),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    if (gpio_config(&configuration) != ESP_OK ||
        gpio_set_level(sensor_power_enable, enabled ? 1 : 0) != ESP_OK) {
        ESP_LOGE(kLogTag, "Failed to set sensor-power GPIO14 to %u", enabled ? 1U : 0U);
        return false;
    }
    return true;
}

bool AjSr04mSensor::initialize()
{
    if (initialized_) {
        return true;
    }

    const gpio_config_t trigger_configuration{
        .pin_bit_mask = 1ULL << static_cast<unsigned int>(board::tbeam_v1_2::kUltrasonicTrigger),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    const gpio_config_t echo_configuration{
        .pin_bit_mask = 1ULL << static_cast<unsigned int>(board::tbeam_v1_2::kUltrasonicEcho),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    if (gpio_config(&trigger_configuration) != ESP_OK ||
        gpio_config(&echo_configuration) != ESP_OK ||
        gpio_set_level(board::tbeam_v1_2::kUltrasonicTrigger, 0) != ESP_OK) {
        ESP_LOGE(kLogTag, "Failed to configure AJ-SR04M GPIOs");
        return false;
    }

    esp_err_t error = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(kLogTag, "Failed to install GPIO ISR service: %s", esp_err_to_name(error));
        return false;
    }
    error = gpio_isr_handler_add(board::tbeam_v1_2::kUltrasonicEcho, echo_interrupt, nullptr);
    if (error != ESP_OK) {
        ESP_LOGE(kLogTag, "Failed to attach echo ISR: %s", esp_err_to_name(error));
        return false;
    }

    initialized_ = true;
    return true;
}

SensorReading AjSr04mSensor::measure(std::uint32_t timeout_microseconds)
{
    if (!initialized_) {
        return {SensorStatus::NotInitialized, 0, 0};
    }
    if (measuring_) {
        return {SensorStatus::Busy, 0, 0};
    }
    if (timeout_microseconds == 0) {
        return {SensorStatus::Timeout, 0, 0};
    }

    measuring_ = true;
    capture.rising_edge_microseconds = 0;
    capture.pulse_microseconds = 0;
    capture.waiting_task = xTaskGetCurrentTaskHandle();
    static_cast<void>(ulTaskNotifyTake(pdTRUE, 0));

    gpio_set_level(board::tbeam_v1_2::kUltrasonicTrigger, 1);
    esp_rom_delay_us(kTriggerPulseMicroseconds);
    gpio_set_level(board::tbeam_v1_2::kUltrasonicTrigger, 0);

    const TickType_t timeout_ticks =
        std::max<TickType_t>(1, pdMS_TO_TICKS((timeout_microseconds + 999U) / 1000U));
    const bool captured = ulTaskNotifyTake(pdTRUE, timeout_ticks) != 0;
    capture.waiting_task = nullptr;
    measuring_ = false;
    if (!captured) {
        return {SensorStatus::Timeout, 0, 0};
    }

    const std::uint32_t pulse = capture.pulse_microseconds;
    if (pulse < kMinimumValidPulseMicroseconds || pulse > timeout_microseconds) {
        return {SensorStatus::InvalidPulse, pulse, 0};
    }
    return {
        SensorStatus::Valid,
        pulse,
        echo_microseconds_to_millimetres(pulse),
    };
}

}  // namespace tank_monitor::sensor