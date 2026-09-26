# LoRa Tank Monitor

LoRa Tank Monitor is an ESP-IDF firmware platform for remotely monitoring a
water tank. A battery-powered transmitter measures the water surface with an
ultrasonic sensor and sends measurements over LoRa. A mains-powered receiver
forwards validated measurements to Blynk over Wi-Fi and presents local status
on an OLED.

> [!WARNING]
> Version 1 is an alerting system. It does not control a valve and cannot stop
> incoming water. Do not treat it as a safety shutoff or the sole protection
> against overflow.

## Project status

The project is under staged development. It currently provides the bootable
foundation, host-tested configuration validation, tank conversion, alarms,
versioned LoRa packets, AJ-SR04M measurements, and atomic dual-slot NVS
persistence. The confirmed T-Beam V1.2 board layer provides I2C/button
diagnostics, AXP2101 battery and rail telemetry, and an SSD1306 boot status
screen. Missing configuration starts a portal with Wi-Fi scanning, complete
forms, atomic save/reboot, button-held re-entry, redacted prefill, and confirmed
factory reset. A RadioLib-backed SX1278 manager now applies persistent radio
settings and performs a one-shot role-aware discovery status exchange.

The target is a LilyGO TTGO T-Beam ESP32 board with an AXP2101 PMU and a
433 MHz SX127x radio. LilyGO documentation associates 433 MHz variants with
SX1278, while the original hardware description specifies SX1276. Board and
radio drivers will be selected only after the exact PCB and radio markings are
confirmed.

## Requirements

- VS Code with the Espressif IDF extension
- ESP-IDF 5.5.x (currently validated with 5.5.2)
- An ESP32-capable USB data cable and serial driver
- A LoRa antenna suitable for the fitted radio frequency

Never transmit without the correct antenna attached.

## Build

```bash
source ~/esp/esp-idf-v5.5/export.sh
idf.py set-target esp32
idf.py build
```

ESP-IDF applies `sdkconfig.defaults` when it creates `sdkconfig`. After pulling
a change to build defaults or the partition table, regenerate local build
configuration before rebuilding:

```bash
idf.py fullclean
rm -f sdkconfig
idf.py set-target esp32
idf.py build
```

The firmware assumes the board's documented 4 MB flash size. Do not flash it
until the board is confirmed to be the intended ESP32 T-Beam variant.

## Flash and monitor

```bash
source ~/esp/esp-idf-v5.5/export.sh
idf.py -p /dev/ttyUSB0 flash monitor
```

Replace `/dev/ttyUSB0` with the detected serial port. Exit the monitor with
`Ctrl+]`.

Expected application log messages include:

```text
LoRa Tank Monitor starting
ESP32 cores=2 revision=...
Reset reason=...
Configuration schema=1
Configuration requires provisioning (reason=...); portal arrives in Stage 4
```

## Host tests

```bash
cmake -S test/host -B build/host-tests -DCMAKE_BUILD_TYPE=Debug
cmake --build build/host-tests
ctest --test-dir build/host-tests --output-on-failure
```

## Configuration and security

No user-specific configuration is compiled into the firmware. Later stages
will provide a temporary captive portal and versioned persistent storage.
Wi-Fi passwords, Blynk tokens, private keys, and installation data must never
be committed to this repository.

The future open provisioning access point will run only when configuration is
missing or after a deliberate local action. Because an open access point does
not encrypt traffic, provisioning must be performed nearby in a trusted
environment. Stored secret fields will be write-only in the portal.

## Repository layout

- `main/`: application entry point and runtime role composition
- `components/`: module ownership and future ESP-IDF components
- `docs/`: design, hardware, configuration, protocol, and operations guides
- `test/`: host and on-device tests added with their implementation stages

See [components/README.md](components/README.md) for the planned component
boundaries. The current configuration fields and radio frame are documented in
[docs/configuration.md](docs/configuration.md) and
[docs/protocol.md](docs/protocol.md).

Hardware assumptions and experimental sensor wiring constraints are documented
in [docs/hardware-verification.md](docs/hardware-verification.md) and
[docs/wiring.md](docs/wiring.md).

Captive portal behavior and verification are documented in
[docs/provisioning.md](docs/provisioning.md).

Radio integration and the link checkpoint are documented in
[docs/radio.md](docs/radio.md).

## License

This project is available under the MIT License. See [LICENSE](LICENSE).