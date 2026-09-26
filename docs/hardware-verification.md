# Hardware verification

The confirmed board is marked `tbeam-axp2101-v1.2 20230508` with a
`433/470MHz` radio sticker. This selects the classic ESP32 T-Beam V1.2 profile,
AXP2101 PMU, SSD1306 OLED, and SX1278 radio.

## Confirmed board profile

| Function | GPIO/address |
| --- | ---: |
| I2C SDA / SCL | 21 / 22 |
| AXP2101 | I2C `0x34`, IRQ GPIO35 |
| SSD1306 OLED | I2C `0x3C` |
| User button | GPIO38, active low |
| SX1278 SPI SCK/MISO/MOSI | 5 / 19 / 27 |
| SX1278 CS / RESET | 18 / 23 |
| SX1278 DIO0/DIO1/DIO2 | 26 / 33 / 32 |
| GNSS RX/TX | 34 / 12 |

AXP2101 DC1 powers the ESP32, ALDO2 powers the radio, and ALDO3 powers GNSS.
Firmware must not disable DC1. The self-test only probes I2C and reads GPIOs;
it does not alter PMU outputs. Power telemetry enables battery detection and
the battery-voltage ADC, then reads battery voltage/percentage and existing
ALDO2/ALDO3 states. It does not reconfigure rail voltages or enable states.

GPIO38 is an input-only ESP32 pin and has no internal pull resistor. The board's
external bias holds it high when released; firmware must not request an internal
pull-up. Pressed and released states have been verified on hardware.

The board advertises external PSRAM, but the generic firmware currently leaves
`CONFIG_SPIRAM` disabled to avoid unnecessary TX power and memory complexity.
The self-test therefore reports zero usable PSRAM heap by design; this is not a
hardware absence result.

The first flash that introduced the coredump partition found stale bytes from
the previous partition layout. Clear only that partition once with:

```bash
esptool.py --chip esp32 --port /dev/ttyACM0 erase_region 0x221000 0x10000
```

Do not erase the whole flash after provisioning, because that also destroys
persistent configuration.

## Original verification gate

Hardware-dependent implementation is intentionally blocked until both boards
are identified. Before adding pin definitions or enabling a power rail, record:

1. PCB product name and revision printed on the silkscreen. Confirmed.
2. ESP32 module marking and flash capacity.
3. PMU marking, expected to be AXP2101. Confirmed by silkscreen profile; probe pending.
4. LoRa transceiver marking and fitted frequency variant. 433/470 MHz SX1278 profile confirmed.
5. OLED controller, I2C address, and whether it is soldered to the board. SSD1306 at `0x3C`; probe pending.
6. Exposed GPIO pins available for sensor trigger, echo, and switched power.
7. JSN-SR04T board revision and selected operating mode.

Photographs of both PCB faces and close-ups of the PMU and radio module are the
preferred evidence. Do not infer a pin map solely from the product listing.

The JSN-SR04T normally requires a regulated 5 V supply. Its echo output must be
level-shifted to 3.3 V before reaching the ESP32. The sensor supply should be
switched completely off between TX measurements; the switching and boost
circuit must be verified before firmware drives it.