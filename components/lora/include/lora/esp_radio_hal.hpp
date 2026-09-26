#pragma once

#include "RadioLib.h"

#include "driver/spi_master.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace tank_monitor::lora {

class EspRadioHal final : public RadioLibHal {
public:
    EspRadioHal(std::int8_t sck, std::int8_t miso, std::int8_t mosi);
    ~EspRadioHal() override;

    void pinMode(std::uint32_t pin, std::uint32_t mode) override;
    void digitalWrite(std::uint32_t pin, std::uint32_t value) override;
    std::uint32_t digitalRead(std::uint32_t pin) override;
    void attachInterrupt(std::uint32_t interrupt_number, void (*callback)(), std::uint32_t mode) override;
    void detachInterrupt(std::uint32_t interrupt_number) override;
    void delay(RadioLibTime_t milliseconds) override;
    void delayMicroseconds(RadioLibTime_t microseconds) override;
    RadioLibTime_t millis() override;
    RadioLibTime_t micros() override;
    long pulseIn(std::uint32_t pin, std::uint32_t state, RadioLibTime_t timeout) override;
    void spiBegin() override;
    void spiBeginTransaction() override;
    void spiTransfer(std::uint8_t* output, std::size_t length, std::uint8_t* input) override;
    void spiEndTransaction() override;
    void spiEnd() override;
    void init() override;
    void term() override;
    void yield() override;

private:
    std::int8_t sck_;
    std::int8_t miso_;
    std::int8_t mosi_;
    spi_device_handle_t spi_device_{nullptr};
    bool owns_bus_{false};
};

}  // namespace tank_monitor::lora