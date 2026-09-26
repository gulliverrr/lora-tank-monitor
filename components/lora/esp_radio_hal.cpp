#include "lora/esp_radio_hal.hpp"

#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace tank_monitor::lora {
namespace {

constexpr char kLogTag[] = "radio_hal";
constexpr std::uint32_t kSpiClockHz = 2'000'000;
std::array<void (*)(), GPIO_NUM_MAX> interrupt_callbacks{};

bool valid_gpio(std::uint32_t pin)
{
    return pin != RADIOLIB_NC && pin < GPIO_NUM_MAX;
}

bool valid_output_gpio(std::uint32_t pin)
{
    return valid_gpio(pin) && GPIO_IS_VALID_OUTPUT_GPIO(static_cast<int>(pin));
}

void IRAM_ATTR interrupt_bridge(void* argument)
{
    const std::size_t pin = reinterpret_cast<std::uintptr_t>(argument);
    if (pin < interrupt_callbacks.size() && interrupt_callbacks[pin] != nullptr) {
        interrupt_callbacks[pin]();
    }
}

}  // namespace

EspRadioHal::EspRadioHal(std::int8_t sck, std::int8_t miso, std::int8_t mosi)
    : RadioLibHal(GPIO_MODE_INPUT, GPIO_MODE_OUTPUT, 0, 1, GPIO_INTR_POSEDGE, GPIO_INTR_NEGEDGE),
      sck_(sck), miso_(miso), mosi_(mosi)
{
}

EspRadioHal::~EspRadioHal()
{
    term();
}

void EspRadioHal::pinMode(std::uint32_t pin, std::uint32_t mode)
{
    if (!valid_gpio(pin)) return;
    gpio_config_t configuration{};
    configuration.pin_bit_mask = 1ULL << pin;
    configuration.mode = static_cast<gpio_mode_t>(mode);
    configuration.pull_up_en = GPIO_PULLUP_DISABLE;
    configuration.pull_down_en = GPIO_PULLDOWN_DISABLE;
    configuration.intr_type = GPIO_INTR_DISABLE;
    static_cast<void>(gpio_config(&configuration));
}

void EspRadioHal::digitalWrite(std::uint32_t pin, std::uint32_t value)
{
    if (valid_output_gpio(pin)) {
        static_cast<void>(gpio_set_level(static_cast<gpio_num_t>(pin), value));
    }
}

std::uint32_t EspRadioHal::digitalRead(std::uint32_t pin)
{
    return valid_gpio(pin)
        ? static_cast<std::uint32_t>(gpio_get_level(static_cast<gpio_num_t>(pin))) : 0;
}

void EspRadioHal::attachInterrupt(
    std::uint32_t interrupt_number,
    void (*callback)(),
    std::uint32_t mode)
{
    if (interrupt_number == RADIOLIB_NC || interrupt_number >= interrupt_callbacks.size()) return;
    esp_err_t error = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) return;
    interrupt_callbacks[interrupt_number] = callback;
    static_cast<void>(gpio_set_intr_type(static_cast<gpio_num_t>(interrupt_number),
                                         static_cast<gpio_int_type_t>(mode)));
    static_cast<void>(gpio_isr_handler_add(static_cast<gpio_num_t>(interrupt_number),
                                           interrupt_bridge,
                                           reinterpret_cast<void*>(static_cast<std::uintptr_t>(interrupt_number))));
}

void EspRadioHal::detachInterrupt(std::uint32_t interrupt_number)
{
    if (interrupt_number == RADIOLIB_NC || interrupt_number >= interrupt_callbacks.size()) return;
    static_cast<void>(gpio_isr_handler_remove(static_cast<gpio_num_t>(interrupt_number)));
    static_cast<void>(gpio_set_intr_type(static_cast<gpio_num_t>(interrupt_number), GPIO_INTR_DISABLE));
    interrupt_callbacks[interrupt_number] = nullptr;
}

void EspRadioHal::delay(RadioLibTime_t milliseconds)
{
    if (milliseconds > 0) vTaskDelay(std::max<TickType_t>(1, pdMS_TO_TICKS(milliseconds)));
}

void EspRadioHal::delayMicroseconds(RadioLibTime_t microseconds)
{
    esp_rom_delay_us(microseconds);
}

RadioLibTime_t EspRadioHal::millis()
{
    return static_cast<RadioLibTime_t>(esp_timer_get_time() / 1000);
}

RadioLibTime_t EspRadioHal::micros()
{
    return static_cast<RadioLibTime_t>(esp_timer_get_time());
}

long EspRadioHal::pulseIn(std::uint32_t pin, std::uint32_t state, RadioLibTime_t timeout)
{
    const RadioLibTime_t wait_start = micros();
    while (digitalRead(pin) == state) {
        if (micros() - wait_start >= timeout) return 0;
        taskYIELD();
    }
    while (digitalRead(pin) != state) {
        if (micros() - wait_start >= timeout) return 0;
        taskYIELD();
    }
    const RadioLibTime_t pulse_start = micros();
    while (digitalRead(pin) == state) {
        if (micros() - pulse_start >= timeout) return 0;
    }
    return static_cast<long>(micros() - pulse_start);
}

void EspRadioHal::spiBegin()
{
    if (spi_device_ != nullptr) return;
    spi_bus_config_t bus{};
    bus.mosi_io_num = mosi_;
    bus.miso_io_num = miso_;
    bus.sclk_io_num = sck_;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.max_transfer_sz = 256;
    esp_err_t error = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO);
    owns_bus_ = error == ESP_OK;
    if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(kLogTag, "SPI bus initialization failed: %s", esp_err_to_name(error));
        return;
    }
    spi_device_interface_config_t device{};
    device.mode = 0;
    device.clock_speed_hz = kSpiClockHz;
    device.spics_io_num = -1;
    device.queue_size = 1;
    error = spi_bus_add_device(SPI2_HOST, &device, &spi_device_);
    if (error != ESP_OK) {
        ESP_LOGE(kLogTag, "SPI device initialization failed: %s", esp_err_to_name(error));
        spi_device_ = nullptr;
    }
}

void EspRadioHal::spiBeginTransaction() {}

void EspRadioHal::spiTransfer(std::uint8_t* output, std::size_t length, std::uint8_t* input)
{
    if (spi_device_ == nullptr || output == nullptr || length == 0) {
        if (input != nullptr) std::memset(input, 0, length);
        return;
    }

    for (std::size_t index = 0; index < length; ++index) {
        spi_transaction_t transaction{};
        transaction.flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA;
        transaction.length = 8;
        transaction.tx_data[0] = output[index];
        if (spi_device_polling_transmit(spi_device_, &transaction) != ESP_OK) {
            if (input != nullptr) {
                std::memset(input + index, 0, length - index);
            }
            return;
        }
        if (input != nullptr) {
            input[index] = transaction.rx_data[0];
        }
    }
}

void EspRadioHal::spiEndTransaction() {}

void EspRadioHal::spiEnd()
{
    if (spi_device_ != nullptr) {
        static_cast<void>(spi_bus_remove_device(spi_device_));
        spi_device_ = nullptr;
    }
    if (owns_bus_) {
        static_cast<void>(spi_bus_free(SPI2_HOST));
        owns_bus_ = false;
    }
}

void EspRadioHal::init() { spiBegin(); }
void EspRadioHal::term() { spiEnd(); }
void EspRadioHal::yield() { taskYIELD(); }

}  // namespace tank_monitor::lora