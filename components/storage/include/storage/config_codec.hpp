#pragma once

#include "config/app_config.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace tank_monitor::storage {

constexpr std::uint32_t kConfigRecordMagic = 0x4c544346;
constexpr std::uint16_t kConfigRecordFormatVersion = 1;
constexpr std::size_t kMaximumConfigRecordSize = 2048;

enum class CodecError : std::uint8_t {
    None,
    BufferTooSmall,
    InvalidArgument,
    InvalidMagic,
    UnsupportedRecordVersion,
    UnsupportedSchemaVersion,
    LengthMismatch,
    CrcMismatch,
    MalformedPayload,
};

struct ConfigRecord {
    std::array<std::uint8_t, kMaximumConfigRecordSize> bytes{};
    std::size_t size{0};
};

struct DecodeResult {
    CodecError error{CodecError::None};
    config::AppConfig configuration{};

    [[nodiscard]] constexpr bool valid() const
    {
        return error == CodecError::None;
    }
};

[[nodiscard]] CodecError encode(
    const config::AppConfig& configuration,
    ConfigRecord& record);

[[nodiscard]] DecodeResult decode(const std::uint8_t* data, std::size_t size);

}  // namespace tank_monitor::storage