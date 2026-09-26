#include "lora/lora_manager.hpp"

#include "board/tbeam_v1_2.hpp"

#include "esp_log.h"

namespace tank_monitor::lora {
namespace {

constexpr char kLogTag[] = "lora";

}  // namespace

LoRaManager::LoRaManager()
    : hal_(board::tbeam_v1_2::kRadioSck,
           board::tbeam_v1_2::kRadioMiso,
           board::tbeam_v1_2::kRadioMosi),
      module_(&hal_,
              board::tbeam_v1_2::kRadioChipSelect,
              board::tbeam_v1_2::kRadioDio0,
              board::tbeam_v1_2::kRadioReset,
              board::tbeam_v1_2::kRadioDio1),
      radio_(&module_)
{
}

RadioDiagnostics LoRaManager::initialize(
    const config::RadioConfig& configuration,
    bool radio_rail_enabled)
{
    diagnostics_ = {};
    diagnostics_.frequency_hz = configuration.frequency_hz;
    diagnostics_.bandwidth_hz = configuration.bandwidth_hz;
    diagnostics_.spreading_factor = configuration.spreading_factor;
    diagnostics_.coding_rate_denominator = configuration.coding_rate_denominator;
    diagnostics_.transmit_power_dbm = configuration.transmit_power_dbm;

    if (!radio_rail_enabled) {
        diagnostics_.state = RadioState::PowerRailOff;
        return diagnostics_;
    }
    if (configuration.chip != config::RadioChip::Sx1278 || !configuration.explicit_header) {
        diagnostics_.state = RadioState::UnsupportedConfiguration;
        return diagnostics_;
    }

    ConfigLoRa_t radio_configuration{};
    radio_configuration.frequency = static_cast<float>(configuration.frequency_hz) / 1'000'000.0F;
    radio_configuration.bandwidth = static_cast<float>(configuration.bandwidth_hz) / 1000.0F;
    radio_configuration.spreadingFactor = configuration.spreading_factor;
    radio_configuration.codingRate = configuration.coding_rate_denominator;
    radio_configuration.syncWord = configuration.sync_word;
    radio_configuration.power = configuration.transmit_power_dbm;
    radio_configuration.preambleLength = configuration.preamble_symbols;

    diagnostics_.driver_error = radio_.begin(radio_configuration);
    if (diagnostics_.driver_error != RADIOLIB_ERR_NONE) {
        diagnostics_.state = RadioState::DriverError;
        ESP_LOGE(kLogTag, "SX1278 initialization failed: %d", diagnostics_.driver_error);
        return diagnostics_;
    }
    diagnostics_.chip_version = static_cast<std::uint8_t>(radio_.getChipVersion());
    diagnostics_.driver_error = radio_.setCRC(configuration.phy_crc_enabled);
    if (diagnostics_.driver_error == RADIOLIB_ERR_NONE) {
        diagnostics_.driver_error = radio_.explicitHeader();
    }
    if (diagnostics_.driver_error == RADIOLIB_ERR_NONE) {
        diagnostics_.driver_error = radio_.sleep();
    }
    if (diagnostics_.driver_error != RADIOLIB_ERR_NONE) {
        diagnostics_.state = RadioState::DriverError;
        ESP_LOGE(kLogTag, "SX1278 final configuration failed: %d", diagnostics_.driver_error);
        return diagnostics_;
    }

    diagnostics_.state = RadioState::ReadySleeping;
    ESP_LOGI(kLogTag,
             "SX1278 ready: version=0x%02x frequency=%lu Hz bandwidth=%lu Hz SF%u CR4/%u power=%d dBm (sleeping)",
             diagnostics_.chip_version,
             static_cast<unsigned long>(diagnostics_.frequency_hz),
             static_cast<unsigned long>(diagnostics_.bandwidth_hz),
             diagnostics_.spreading_factor,
             diagnostics_.coding_rate_denominator,
             diagnostics_.transmit_power_dbm);
    return diagnostics_;
}

TransmitResult LoRaManager::transmit(const protocol::EncodedFrame& frame)
{
    TransmitResult result{};
    if (!diagnostics_.ready() || frame.size == 0 || frame.size > frame.bytes.size()) {
        return result;
    }

    result.driver_error = radio_.transmit(frame.bytes.data(), frame.size);
    result.programmed_payload_length = module_.SPIreadRegister(RADIOLIB_SX127X_REG_PAYLOAD_LENGTH);
    const std::int16_t sleep_error = radio_.sleep();
    if (result.driver_error == RADIOLIB_ERR_NONE && sleep_error != RADIOLIB_ERR_NONE) {
        result.driver_error = sleep_error;
    }
    return result;
}

ReceiveResult LoRaManager::receive(std::uint32_t timeout_ms)
{
    ReceiveResult result{};
    if (!diagnostics_.ready() || timeout_ms == 0) {
        return result;
    }

    result.driver_error = radio_.receive(
        result.frame.bytes.data(), result.frame.bytes.size(), timeout_ms);
    if (result.driver_error == RADIOLIB_ERR_NONE) {
        result.radio_reported_length = radio_.getPacketLength(false);
        const std::size_t payload_size =
            static_cast<std::size_t>(result.frame.bytes[38]) << 8U | result.frame.bytes[39];
        const std::size_t application_length = protocol::kHeaderSize + payload_size + protocol::kCrcSize;
        result.frame.size = application_length <= result.frame.bytes.size()
            ? application_length : result.radio_reported_length;
        result.rssi_dbm = radio_.getRSSI();
        result.snr_db = radio_.getSNR();
    }
    const std::int16_t sleep_error = radio_.sleep();
    if (result.driver_error == RADIOLIB_ERR_NONE && sleep_error != RADIOLIB_ERR_NONE) {
        result.driver_error = sleep_error;
        result.frame.size = 0;
    }
    return result;
}

const RadioDiagnostics& LoRaManager::diagnostics() const
{
    return diagnostics_;
}

}  // namespace tank_monitor::lora