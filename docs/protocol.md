# LoRa application protocol

Protocol version 1 uses a maximum 255-byte frame. Integers are unsigned and
encoded most-significant byte first. Firmware serializes each field explicitly;
it never transmits an in-memory C++ structure.

## Header

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 2 | Magic `0x4c54` (`LT`) |
| 2 | 1 | Protocol version |
| 3 | 1 | Header length, currently 40 |
| 4 | 1 | Message type |
| 5 | 1 | Flags |
| 6 | 8 | Sender node ID |
| 14 | 8 | Intended receiver ID; zero is reserved for discovery |
| 22 | 4 | Tank ID |
| 26 | 4 | Random boot/session nonce |
| 30 | 4 | Sequence number |
| 34 | 4 | Sender uptime in seconds |
| 38 | 2 | TLV payload length |

The payload contains fields encoded as one-byte tag, one-byte length, and the
value. Known measurement tags currently cover distance in centimetres, level in
centimetres, percentage in basis points, volume in litres, battery in
millivolts, sensor status, and alarm flags. Unknown well-formed tags are skipped
so minor firmware additions remain compatible.

The current TX checkpoint includes battery millivolts and sensor status in every
measurement. When the echo and tank details are valid, it also includes raw
distance in whole centimetres, calibrated water level in whole centimetres,
fill percentage in basis points (`6834` is `68.34%`), and calculated volume in
whole litres.

A four-byte IEEE CRC32 follows the payload. This supplements the radio PHY CRC
and detects application framing errors. It is not cryptographic authentication.

## Receive rules

RX must reject invalid magic, version, header length, message type, total
length, CRC, receiver identity, tank identity, or sender identity. An unpaired
RX permanently records the first valid sender/tank pair it receives. Later
packets are processed only when both values match that saved pairing. The
provisioning portal can clear this binding without erasing the remaining
configuration.

TX samples the AJ-SR04M, broadcasts one `Measurement` packet, and sleeps. It
does not wait for a receiver, acknowledgement, or pairing state. RX continuously
rearms its receive operation and processes a valid packet whenever TX wakes.

Identity-only pairing does not prevent spoofing or replay by an attacker with a
compatible radio. Cryptographic packet authentication is outside version 1.