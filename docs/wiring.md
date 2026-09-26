# Wiring

## T-Beam onboard devices

The confirmed `tbeam-axp2101-v1.2 20230508` profile uses the onboard connections
listed in [hardware-verification.md](hardware-verification.md). Do not attach
external devices to GPIOs already assigned to LoRa, I2C, GNSS, the PMU IRQ, or
the user button.

Always attach the correct 433/470 MHz antenna before enabling SX1278
transmission.

## AJ-SR04M

The sensor is an AJ-SR04M with R19 unpopulated, selecting trigger/echo
pulse-width operation. GPIO13, GPIO25, and GPIO14 are exposed and unused in the
confirmed T-Beam schematic.

| AJ-SR04M | Connection |
| --- | --- |
| VCC | Switched regulated 5 V |
| GND | T-Beam ground |
| TRIG | GPIO13 |
| ECHO | GPIO25 through divider |

Use a 10 kOhm resistor from ECHO to GPIO25 and a 15 kOhm resistor from GPIO25
to ground. This produces approximately 3.0 V from a 5.0 V echo and stays below
3.3 V at a 5.25 V USB supply. Measure the assembled divider before connecting
GPIO25. Never connect the 5 V ECHO output directly to ESP32.

The AXP2101 LDO outputs cannot generate 5 V. Battery operation therefore needs
a regulated boost converter with a low-quiescent-current enable or load switch.
GPIO14 is reserved as the future sensor-power enable. USB VBUS may supply a
bench test, but it is not the battery-powered production supply.

For a one-shot bench measurement, power the sensor, hold the T-Beam middle
button, and reset the board. The firmware emits one 12 microsecond trigger and
waits at most 40 milliseconds for ECHO. Normal boot leaves GPIO13 and GPIO25
unconfigured and does not trigger the sensor.

Test across the minimum and expected operating distances. Record startup and
active current, divider output voltage, timeouts, and measured distance. The
speed-of-sound conversion currently assumes 343 m/s; later calibration and
temperature compensation can be applied through the sensor/tank configuration.