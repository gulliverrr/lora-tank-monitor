#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace tank_monitor::protocol {

constexpr std::uint16_t kMagic = 0x4c54;
constexpr std::uint8_t kProtocolVersion = 1;
constexpr std::size_t kHeaderSize = 40;
constexpr std::size_t kCrcSize = 4;
constexpr std::size_t kMaximumFrameSize = 255;
constexpr std::size_t kMaximumPayloadSize = kMaximumFrameSize - kHeaderSize - kCrcSize;

enum class MessageType : std::uint8_t {
    Measurement = 1,
    Acknowledgement = 2,
    PairRequest = 3,
    PairResponse = 4,
    Status = 5,
    Error = 6,
};

enum class FieldTag : std::uint8_t {
    DistanceCentimetres = 1,
    LevelCentimetres = 2,
    PercentageBasisPoints = 3,
    VolumeUnits = 4,
    BatteryMillivolts = 5,
    SensorStatus = 6,
    AlarmFlags = 7,
    VolumeUnit = 8,
};

struct Header {
    MessageType message_type{MessageType::Measurement};
    std::uint8_t flags{0};
    std::uint64_t sender_id{0};
    std::uint64_t receiver_id{0};
    std::uint32_t tank_id{0};
    std::uint32_t boot_nonce{0};
    std::uint32_t sequence{0};
    std::uint32_t uptime_seconds{0};
};

struct Payload {
    std::array<std::uint8_t, kMaximumPayloadSize> bytes{};
    std::size_t size{0};
};

struct Packet {
    Header header{};
    Payload payload{};
};

struct EncodedFrame {
    std::array<std::uint8_t, kMaximumFrameSize> bytes{};
    std::size_t size{0};
};

enum class CodecError : std::uint8_t {
    None,
    InvalidArgument,
    PayloadTooLarge,
    FrameTooShort,
    InvalidMagic,
    UnsupportedVersion,
    InvalidHeaderLength,
    InvalidMessageType,
    LengthMismatch,
    CrcMismatch,
    MalformedTlv,
    FieldNotFound,
    FieldLengthMismatch,
};

struct DecodeResult {
    CodecError error{CodecError::None};
    Packet packet{};

    [[nodiscard]] constexpr bool valid() const
    {
        return error == CodecError::None;
    }
};

class PayloadBuilder {
public:
    [[nodiscard]] CodecError add_u8(FieldTag tag, std::uint8_t value);
    [[nodiscard]] CodecError add_u16(FieldTag tag, std::uint16_t value);
    [[nodiscard]] CodecError add_u32(FieldTag tag, std::uint32_t value);
    [[nodiscard]] const Payload& payload() const;

private:
    [[nodiscard]] CodecError add(FieldTag tag, const std::uint8_t* data, std::size_t length);

    Payload payload_{};
};

[[nodiscard]] CodecError encode(const Packet& packet, EncodedFrame& frame);
[[nodiscard]] DecodeResult decode(const std::uint8_t* data, std::size_t length);

[[nodiscard]] CodecError find_u8(const Payload& payload, FieldTag tag, std::uint8_t& value);
[[nodiscard]] CodecError find_u16(const Payload& payload, FieldTag tag, std::uint16_t& value);
[[nodiscard]] CodecError find_u32(const Payload& payload, FieldTag tag, std::uint32_t& value);

[[nodiscard]] std::uint32_t crc32(const std::uint8_t* data, std::size_t length);

}  // namespace tank_monitor::protocol