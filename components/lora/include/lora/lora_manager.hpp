#pragma once

#include "config/app_config.hpp"
#include "lora/esp_radio_hal.hpp"
#include "protocol/protocol_codec.hpp"

#include "RadioLib.h"

#include <cstdint>

namespace tank_monitor::lora {

enum class RadioState : std::uint8_t {
    NotInitialized,
    ReadySleeping,
    PowerRailOff,
    UnsupportedConfiguration,
    DriverError,
};

struct RadioDiagnostics {
    RadioState state{RadioState::NotInitialized};
    std::int16_t driver_error{0};
    std::uint8_t chip_version{0};
    std::uint32_t frequency_hz{0};
    std::uint32_t bandwidth_hz{0};
    std::uint8_t spreading_factor{0};
    std::uint8_t coding_rate_denominator{0};
    std::int8_t transmit_power_dbm{0};

    [[nodiscard]] constexpr bool ready() const
    {
        return state == RadioState::ReadySleeping;
    }
};

struct TransmitResult {
    std::int16_t driver_error{RADIOLIB_ERR_UNKNOWN};
    std::uint8_t programmed_payload_length{0};

    [[nodiscard]] constexpr bool sent() const
    {
        return driver_error == RADIOLIB_ERR_NONE;
    }
};

struct ReceiveResult {
    std::int16_t driver_error{RADIOLIB_ERR_UNKNOWN};
    protocol::EncodedFrame frame{};
    std::size_t radio_reported_length{0};
    float rssi_dbm{0.0F};
    float snr_db{0.0F};

    [[nodiscard]] constexpr bool received() const
    {
        return driver_error == RADIOLIB_ERR_NONE;
    }
};

class LoRaManager {
public:
    LoRaManager();

    [[nodiscard]] RadioDiagnostics initialize(
        const config::RadioConfig& configuration,
        bool radio_rail_enabled);
    [[nodiscard]] TransmitResult transmit(const protocol::EncodedFrame& frame);
    [[nodiscard]] ReceiveResult receive(std::uint32_t timeout_ms);
    [[nodiscard]] const RadioDiagnostics& diagnostics() const;

private:
    EspRadioHal hal_;
    Module module_;
    SX1278 radio_;
    RadioDiagnostics diagnostics_{};
};

}  // namespace tank_monitor::lora