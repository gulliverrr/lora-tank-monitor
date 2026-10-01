# LoRa radio

The confirmed T-Beam profile contains an SX1278 for 433/470 MHz. Radio access
uses RadioLib 7.7.1 through a project-owned ESP-IDF `spi_master`/GPIO HAL. The
application depends on the `LoRaManager` abstraction rather than RadioLib
directly.

## Board connections

| SX1278 signal | ESP32 GPIO |
| --- | ---: |
| SCK | 5 |
| MISO | 19 |
| MOSI | 27 |
| NSS/CS | 18 |
| RESET | 23 |
| DIO0 | 26 |
| DIO1 | 33 |
| DIO2 | 32 |

AXP2101 ALDO2 supplies the radio at 3.3 V. Initialization refuses to touch the
radio if that rail is reported off.

## Configuration

Persistent radio settings map to RadioLib's LoRa configuration: frequency,
bandwidth, spreading factor, coding-rate denominator, sync word, preamble, and
PA_BOOST output power. Application packets have variable lengths, so explicit
headers are mandatory. PHY CRC is configurable independently of the
application CRC32.

On valid operational boot, firmware resets and identifies the SX1278, applies
settings, and reads the version register. Accepted version values are `0x11`,
`0x12`, and `0x13`.

On each boot the radio performs one role-aware operation:

- TX powers the sensor, measures once, broadcasts one protocol `Measurement`
	frame, and returns to deep sleep for the configured measurement interval. It
	does not wait for an acknowledgement and does not retry. While the OLED
	diagnostics are on, holding the button for 1.5 seconds measures and
	transmits again.
- RX listens for one configured receive-timeout period, validates application
	framing, receiver identity, and saved sender pairing, logs
	link metrics, then immediately rearms reception. RX is mains-powered and
	listens continuously; the configured timeout only bounds each internal radio
	receive cycle and does not require synchronized board resets.

The stored transmission-retry count is validated but not currently used.
Frequency, power, and duty cycle remain the installer's regulatory
responsibility.