#include "protocol/protocol_codec.hpp"

#include <algorithm>

namespace tank_monitor::protocol {
namespace {

void write_u16(std::uint8_t* destination, std::uint16_t value)
{
    destination[0] = static_cast<std::uint8_t>(value >> 8U);
    destination[1] = static_cast<std::uint8_t>(value);
}

void write_u32(std::uint8_t* destination, std::uint32_t value)
{
    for (std::size_t index = 0; index < 4; ++index) {
        destination[index] = static_cast<std::uint8_t>(value >> ((3U - index) * 8U));
    }
}

void write_u64(std::uint8_t* destination, std::uint64_t value)
{
    for (std::size_t index = 0; index < 8; ++index) {
        destination[index] = static_cast<std::uint8_t>(value >> ((7U - index) * 8U));
    }
}

std::uint16_t read_u16(const std::uint8_t* source)
{
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(source[0]) << 8U | source[1]);
}

std::uint32_t read_u32(const std::uint8_t* source)
{
    std::uint32_t value = 0;
    for (std::size_t index = 0; index < 4; ++index) {
        value = value << 8U | source[index];
    }
    return value;
}

std::uint64_t read_u64(const std::uint8_t* source)
{
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < 8; ++index) {
        value = value << 8U | source[index];
    }
    return value;
}

bool valid_message_type(std::uint8_t value)
{
    return value >= static_cast<std::uint8_t>(MessageType::Measurement) &&
        value <= static_cast<std::uint8_t>(MessageType::Error);
}

CodecError find_field(
    const Payload& payload,
    FieldTag tag,
    const std::uint8_t*& data,
    std::size_t& length)
{
    std::size_t offset = 0;
    while (offset < payload.size) {
        if (payload.size - offset < 2) {
            return CodecError::MalformedTlv;
        }
        const auto field_tag = static_cast<FieldTag>(payload.bytes[offset]);
        const std::size_t field_length = payload.bytes[offset + 1];
        offset += 2;
        if (field_length > payload.size - offset) {
            return CodecError::MalformedTlv;
        }
        if (field_tag == tag) {
            data = &payload.bytes[offset];
            length = field_length;
            return CodecError::None;
        }
        offset += field_length;
    }
    return CodecError::FieldNotFound;
}

}  // namespace

CodecError PayloadBuilder::add_u8(FieldTag tag, std::uint8_t value)
{
    return add(tag, &value, 1);
}

CodecError PayloadBuilder::add_u16(FieldTag tag, std::uint16_t value)
{
    std::uint8_t bytes[2]{};
    write_u16(bytes, value);
    return add(tag, bytes, sizeof(bytes));
}

CodecError PayloadBuilder::add_u32(FieldTag tag, std::uint32_t value)
{
    std::uint8_t bytes[4]{};
    write_u32(bytes, value);
    return add(tag, bytes, sizeof(bytes));
}

const Payload& PayloadBuilder::payload() const
{
    return payload_;
}

CodecError PayloadBuilder::add(FieldTag tag, const std::uint8_t* data, std::size_t length)
{
    if (data == nullptr || length > 255) {
        return CodecError::InvalidArgument;
    }
    if (length + 2 > payload_.bytes.size() - payload_.size) {
        return CodecError::PayloadTooLarge;
    }

    payload_.bytes[payload_.size++] = static_cast<std::uint8_t>(tag);
    payload_.bytes[payload_.size++] = static_cast<std::uint8_t>(length);
    std::copy_n(data, length, payload_.bytes.begin() + payload_.size);
    payload_.size += length;
    return CodecError::None;
}

CodecError encode(const Packet& packet, EncodedFrame& frame)
{
    if (packet.payload.size > kMaximumPayloadSize) {
        return CodecError::PayloadTooLarge;
    }
    if (!valid_message_type(static_cast<std::uint8_t>(packet.header.message_type))) {
        return CodecError::InvalidMessageType;
    }

    frame = {};
    write_u16(&frame.bytes[0], kMagic);
    frame.bytes[2] = kProtocolVersion;
    frame.bytes[3] = static_cast<std::uint8_t>(kHeaderSize);
    frame.bytes[4] = static_cast<std::uint8_t>(packet.header.message_type);
    frame.bytes[5] = packet.header.flags;
    write_u64(&frame.bytes[6], packet.header.sender_id);
    write_u64(&frame.bytes[14], packet.header.receiver_id);
    write_u32(&frame.bytes[22], packet.header.tank_id);
    write_u32(&frame.bytes[26], packet.header.boot_nonce);
    write_u32(&frame.bytes[30], packet.header.sequence);
    write_u32(&frame.bytes[34], packet.header.uptime_seconds);
    write_u16(&frame.bytes[38], static_cast<std::uint16_t>(packet.payload.size));
    std::copy_n(packet.payload.bytes.begin(), packet.payload.size, frame.bytes.begin() + kHeaderSize);

    const std::size_t crc_offset = kHeaderSize + packet.payload.size;
    write_u32(&frame.bytes[crc_offset], crc32(frame.bytes.data(), crc_offset));
    frame.size = crc_offset + kCrcSize;
    return CodecError::None;
}

DecodeResult decode(const std::uint8_t* data, std::size_t length)
{
    DecodeResult result{};
    if (data == nullptr) {
        result.error = CodecError::InvalidArgument;
        return result;
    }
    if (length < kHeaderSize + kCrcSize) {
        result.error = CodecError::FrameTooShort;
        return result;
    }
    if (read_u16(data) != kMagic) {
        result.error = CodecError::InvalidMagic;
        return result;
    }
    if (data[2] != kProtocolVersion) {
        result.error = CodecError::UnsupportedVersion;
        return result;
    }
    if (data[3] != kHeaderSize) {
        result.error = CodecError::InvalidHeaderLength;
        return result;
    }
    if (!valid_message_type(data[4])) {
        result.error = CodecError::InvalidMessageType;
        return result;
    }

    const std::size_t payload_size = read_u16(&data[38]);
    if (payload_size > kMaximumPayloadSize ||
        length != kHeaderSize + payload_size + kCrcSize) {
        result.error = CodecError::LengthMismatch;
        return result;
    }
    const std::size_t crc_offset = kHeaderSize + payload_size;
    if (read_u32(&data[crc_offset]) != crc32(data, crc_offset)) {
        result.error = CodecError::CrcMismatch;
        return result;
    }

    result.packet.header.message_type = static_cast<MessageType>(data[4]);
    result.packet.header.flags = data[5];
    result.packet.header.sender_id = read_u64(&data[6]);
    result.packet.header.receiver_id = read_u64(&data[14]);
    result.packet.header.tank_id = read_u32(&data[22]);
    result.packet.header.boot_nonce = read_u32(&data[26]);
    result.packet.header.sequence = read_u32(&data[30]);
    result.packet.header.uptime_seconds = read_u32(&data[34]);
    result.packet.payload.size = payload_size;
    std::copy_n(&data[kHeaderSize], payload_size, result.packet.payload.bytes.begin());
    return result;
}

CodecError find_u8(const Payload& payload, FieldTag tag, std::uint8_t& value)
{
    const std::uint8_t* data = nullptr;
    std::size_t length = 0;
    const auto error = find_field(payload, tag, data, length);
    if (error != CodecError::None) {
        return error;
    }
    if (length != 1) {
        return CodecError::FieldLengthMismatch;
    }
    value = data[0];
    return CodecError::None;
}

CodecError find_u16(const Payload& payload, FieldTag tag, std::uint16_t& value)
{
    const std::uint8_t* data = nullptr;
    std::size_t length = 0;
    const auto error = find_field(payload, tag, data, length);
    if (error != CodecError::None) {
        return error;
    }
    if (length != 2) {
        return CodecError::FieldLengthMismatch;
    }
    value = read_u16(data);
    return CodecError::None;
}

CodecError find_u32(const Payload& payload, FieldTag tag, std::uint32_t& value)
{
    const std::uint8_t* data = nullptr;
    std::size_t length = 0;
    const auto error = find_field(payload, tag, data, length);
    if (error != CodecError::None) {
        return error;
    }
    if (length != 4) {
        return CodecError::FieldLengthMismatch;
    }
    value = read_u32(data);
    return CodecError::None;
}

std::uint32_t crc32(const std::uint8_t* data, std::size_t length)
{
    if (data == nullptr) {
        return 0;
    }

    std::uint32_t crc = 0xffffffffU;
    for (std::size_t index = 0; index < length; ++index) {
        crc ^= data[index];
        for (std::uint8_t bit = 0; bit < 8; ++bit) {
            const std::uint32_t mask = 0U - (crc & 1U);
            crc = (crc >> 1U) ^ (0xedb88320U & mask);
        }
    }
    return ~crc;
}

}  // namespace tank_monitor::protocol