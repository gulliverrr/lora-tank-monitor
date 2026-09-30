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

For battery operation at 5 V, use a regulated boost converter with an enable
input. Connect battery input to the converter, converter 5 V output to sensor
VCC, and converter ground/sensor ground/T-Beam ground together. Connect GPIO14
to the converter EN input (through a divider only if its EN pin requires a
lower voltage); add a 100 kOhm EN-to-ground pull-down so the converter defaults
off during reset and deep sleep. GPIO14 is a control signal only: do not power
the sensor directly from the GPIO. Switch VCC, not GND, so TRIG/ECHO retain a
common reference and cannot back-power the sensor through signal pins.

If the specific sensor is verified to operate correctly from the T-Beam's 3.3 V
rail, the boost converter can be omitted and a 3.3 V load switch used instead;
its input is 3.3 V, output is sensor VCC, EN is GPIO14 with a pull-down, and all
grounds remain common. The firmware's previous 5 V divider values are only
appropriate for a 5 V ECHO high level; re-check the divider if changing supply.
USB VBUS is suitable for bench tests, not battery-powered production.

The onboard red CHG indicator is associated with the PMU/charging circuit. The
firmware requests the AXP2101 charging LED off, but some board revisions wire
the indicator directly to charger status; in that case it follows charging
state and cannot be gated by an ESP32 GPIO without a hardware modification.

For a one-shot bench measurement, power the sensor, hold the T-Beam middle
button, and reset the board. The firmware emits one 12 microsecond trigger and
waits at most 40 milliseconds for ECHO. Normal boot leaves GPIO13 and GPIO25
unconfigured and does not trigger the sensor.

Test across the minimum and expected operating distances. Record startup and
active current, divider output voltage, timeouts, and measured distance. The
speed-of-sound conversion currently assumes 343 m/s; later calibration and
temperature compensation can be applied through the sensor/tank configuration.