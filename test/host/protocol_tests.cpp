#include "protocol/protocol_codec.hpp"
#include "protocol/sequence_tracker.hpp"

#include <algorithm>
#include <array>
#include <iostream>

namespace {

using tank_monitor::protocol::CodecError;
using tank_monitor::protocol::FieldTag;
using tank_monitor::protocol::MessageType;
using tank_monitor::protocol::Packet;
using tank_monitor::protocol::PayloadBuilder;
using tank_monitor::protocol::SequenceStatus;
using tank_monitor::protocol::SequenceTracker;

int failures = 0;

void expect(bool condition, const char* description)
{
    if (!condition) {
        std::cerr << "FAIL: " << description << '\n';
        ++failures;
    }
}

Packet measurement_packet()
{
    PayloadBuilder builder;
    expect(builder.add_u32(FieldTag::DistanceCentimetres, 123) == CodecError::None,
           "distance field added");
    expect(builder.add_u16(FieldTag::BatteryMillivolts, 3712) == CodecError::None,
           "battery field added");
    expect(builder.add_u8(FieldTag::SensorStatus, 0) == CodecError::None,
           "sensor status field added");

    Packet packet{};
    packet.header.message_type = MessageType::Measurement;
    packet.header.flags = 0xa5;
    packet.header.sender_id = 0x0102030405060708ULL;
    packet.header.receiver_id = 0x1112131415161718ULL;
    packet.header.boot_nonce = 0x31323334;
    packet.header.sequence = 0x41424344;
    packet.header.uptime_seconds = 0x51525354;
    packet.payload = builder.payload();
    return packet;
}

void test_round_trip_and_network_order()
{
    tank_monitor::protocol::EncodedFrame frame{};
    expect(tank_monitor::protocol::encode(measurement_packet(), frame) == CodecError::None,
           "packet encodes");
       expect(frame.size == 53, "encoded frame has expected length");

    const std::array<std::uint8_t, 10> expected_prefix{
        0x4c, 0x54, 0x02, 0x24, 0x01, 0xa5, 0x01, 0x02, 0x03, 0x04};
    expect(std::equal(expected_prefix.begin(), expected_prefix.end(), frame.bytes.begin()),
           "header uses stable network-order prefix");

    const auto decoded = tank_monitor::protocol::decode(frame.bytes.data(), frame.size);
    expect(decoded.valid(), "encoded packet decodes");
    expect(decoded.packet.header.sender_id == 0x0102030405060708ULL, "sender ID round trips");
    expect(decoded.packet.header.sequence == 0x41424344, "sequence round trips");

    std::uint32_t distance = 0;
    std::uint16_t battery = 0;
    expect(tank_monitor::protocol::find_u32(
               decoded.packet.payload, FieldTag::DistanceCentimetres, distance) == CodecError::None,
           "distance field found");
    expect(distance == 123, "distance field round trips");
    expect(tank_monitor::protocol::find_u16(
               decoded.packet.payload, FieldTag::BatteryMillivolts, battery) == CodecError::None,
           "battery field found");
    expect(battery == 3712, "battery field round trips");
}

void test_validation_failures()
{
    tank_monitor::protocol::EncodedFrame frame{};
    expect(tank_monitor::protocol::encode(measurement_packet(), frame) == CodecError::None,
           "validation fixture encodes");

    auto corrupted = frame;
    corrupted.bytes[10] ^= 0x01;
    expect(tank_monitor::protocol::decode(corrupted.bytes.data(), corrupted.size).error ==
               CodecError::CrcMismatch,
           "corruption fails CRC");
    expect(tank_monitor::protocol::decode(frame.bytes.data(), frame.size - 1).error ==
               CodecError::LengthMismatch,
           "truncated frame fails length");

    auto wrong_version = frame;
    wrong_version.bytes[2] = 1;
    expect(tank_monitor::protocol::decode(wrong_version.bytes.data(), wrong_version.size).error ==
               CodecError::UnsupportedVersion,
           "unsupported version is rejected before CRC");
}

void test_unknown_tlv_is_skipped()
{
    auto packet = measurement_packet();
    packet.payload.bytes[packet.payload.size++] = 0xfe;
    packet.payload.bytes[packet.payload.size++] = 2;
    packet.payload.bytes[packet.payload.size++] = 0xaa;
    packet.payload.bytes[packet.payload.size++] = 0xbb;

    tank_monitor::protocol::EncodedFrame frame{};
    expect(tank_monitor::protocol::encode(packet, frame) == CodecError::None,
           "packet with future TLV encodes");
    const auto decoded = tank_monitor::protocol::decode(frame.bytes.data(), frame.size);
    std::uint16_t battery = 0;
    expect(decoded.valid(), "packet with unknown TLV decodes");
    expect(tank_monitor::protocol::find_u16(
               decoded.packet.payload, FieldTag::BatteryMillivolts, battery) == CodecError::None,
           "known field remains readable with unknown TLV");
}

void test_sequence_tracking()
{
    SequenceTracker tracker;
    expect(tracker.observe(10, 5).status == SequenceStatus::First, "first packet classified");
    expect(tracker.observe(10, 6).status == SequenceStatus::Next, "next packet classified");
    expect(tracker.observe(10, 6).status == SequenceStatus::Duplicate,
           "duplicate packet classified");
    const auto gap = tracker.observe(10, 10);
    expect(gap.status == SequenceStatus::Gap && gap.missing_count == 3,
           "missing packet count reported");
    expect(tracker.observe(10, 8).status == SequenceStatus::OutOfOrder,
           "out-of-order packet classified");
    expect(tracker.observe(11, 1).status == SequenceStatus::NewSession,
           "new boot nonce starts a session");

        tracker.reset();
        expect(tracker.observe(20, UINT32_MAX).status == SequenceStatus::First,
            "maximum sequence starts tracking");
        expect(tracker.observe(20, 0).status == SequenceStatus::Next,
            "sequence wraparound is consecutive");
}

void test_acknowledgement_correlation()
{
    const auto status = measurement_packet();
    Packet acknowledgement{};
    acknowledgement.header.message_type = MessageType::Acknowledgement;
    acknowledgement.header.sender_id = 0x1112131415161718ULL;
    acknowledgement.header.receiver_id = status.header.sender_id;
    acknowledgement.header.boot_nonce = status.header.boot_nonce;
    acknowledgement.header.sequence = status.header.sequence;

    tank_monitor::protocol::EncodedFrame frame{};
    expect(tank_monitor::protocol::encode(acknowledgement, frame) == CodecError::None,
        "acknowledgement encodes");
    expect(frame.size == 40, "empty acknowledgement has expected frame length");

    const auto decoded = tank_monitor::protocol::decode(frame.bytes.data(), frame.size);
    expect(decoded.valid(), "acknowledgement decodes");
    expect(decoded.packet.header.message_type == MessageType::Acknowledgement,
        "acknowledgement type round trips");
    expect(decoded.packet.header.receiver_id == status.header.sender_id,
        "acknowledgement targets original sender");
    expect(decoded.packet.header.boot_nonce == status.header.boot_nonce,
        "acknowledgement preserves boot nonce");
    expect(decoded.packet.header.sequence == status.header.sequence,
        "acknowledgement preserves sequence");
}

void test_calibrated_measurement_fields()
{
    PayloadBuilder builder;
    expect(builder.add_u32(FieldTag::DistanceCentimetres, 68) == CodecError::None,
        "distance measurement field added");
    expect(builder.add_u32(FieldTag::LevelCentimetres, 153) == CodecError::None,
        "level measurement field added");
    expect(builder.add_u32(FieldTag::PercentageBasisPoints, 6834) == CodecError::None,
        "percentage measurement field added");
    expect(builder.add_u32(FieldTag::VolumeUnits, 123) == CodecError::None,
        "volume measurement field added");

    Packet packet{};
    packet.header.message_type = MessageType::Measurement;
    packet.payload = builder.payload();
    tank_monitor::protocol::EncodedFrame frame{};
    expect(tank_monitor::protocol::encode(packet, frame) == CodecError::None,
        "calibrated measurement encodes");

    const auto decoded = tank_monitor::protocol::decode(frame.bytes.data(), frame.size);
    std::uint32_t percentage_basis_points = 0;
    std::uint32_t volume_litres = 0;
    expect(decoded.valid(), "calibrated measurement decodes");
    expect(tank_monitor::protocol::find_u32(
               decoded.packet.payload,
               FieldTag::PercentageBasisPoints,
               percentage_basis_points) == CodecError::None &&
               percentage_basis_points == 6834,
        "percentage keeps basis-point units");
    expect(tank_monitor::protocol::find_u32(
               decoded.packet.payload,
               FieldTag::VolumeUnits,
             volume_litres) == CodecError::None &&
             volume_litres == 123,
         "volume keeps litre units");
}

}  // namespace

int main()
{
    test_round_trip_and_network_order();
    test_validation_failures();
    test_unknown_tlv_is_skipped();
    test_sequence_tracking();
    test_acknowledgement_correlation();
    test_calibrated_measurement_fields();

    if (failures == 0) {
        std::cout << "All protocol tests passed\n";
    }
    return failures == 0 ? 0 : 1;
}