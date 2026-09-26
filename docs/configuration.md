# Configuration model

`AppConfig` is the runtime configuration root. Version 1 contains device,
Wi-Fi, radio, pairing, tank, sensor, alarm, battery, and Blynk sections. The
default object is intentionally marked unconfigured and cannot enter normal
operation.

Tank settings include a bounded user-facing name and numeric tank ID. Both are
required for an operational configuration.

Configuration strings and peer collections have fixed capacities suitable for
bounded embedded storage. No credentials or installation dimensions are
compiled into defaults.

## Role validation

Both roles require a generated nonzero node ID and valid radio settings. TX
additionally requires tank geometry; RX additionally requires a Wi-Fi SSID and
optionally Blynk settings. TX operates without Wi-Fi after provisioning.

The installer portal intentionally hardcodes OLED behavior, provisioning hold
time, sensor timing, Wi-Fi retry bounds, and the voltage-derived 18650 battery
estimate. Those values are not installation-specific and are therefore not
editable during setup.

RX starts unpaired. The first valid measurement matching its configured tank ID
becomes its permanent TX pairing; later packets are accepted only from that
sender. The Pairing section of the provisioning portal clears this binding
without changing the rest of the saved configuration. TX has no pairing state
and does not wait for an RX acknowledgement.

Blynk is optional. RX maps each `V0` through `V11` slot to at most one reported
metric: raw distance, water level, fill percentage, water volume, battery
voltage/percentage, compact metadata, or TX/RX health. Tokens and Wi-Fi
passwords are write-only.

The radio validator checks transceiver capability, not regional legality.
Installers remain responsible for selecting a legal frequency, power, and duty
cycle for their jurisdiction.

## Tank geometry

Distances are measured downward from the ultrasonic sensor. The configured
sensor reference height and calibrated empty/full water-surface positions
define the installation. A raw reading is corrected as:

```text
corrected_distance = raw_distance + sensor_offset
surface_position = sensor_reference_height - corrected_distance
percentage = 100 * (surface_position - empty_level) / (full_level - empty_level)
```

Physical sensor-range failures are always rejected. Readings outside the
calibrated empty/full range are either rejected or clamped according to the
configured policy. Volume uses either capacity-scaled percentage or a linear
litres-per-centimetre factor, then converts to litres, US gallons, or imperial
gallons for display.

On TX measurement boot, firmware waits a fixed sensor power warm-up before
issuing the AJ-SR04M trigger pulse.

## Battery percentage

The AXP2101's reported battery percentage is not used. A replaceable cell may
not share any learned fuel-gauge state, and observed hardware reported 6% at
4.203 V.

For users who prefer a percentage, firmware derives an approximate value from
calibrated measured voltage. The default linear mapping is:

```text
3.2 V = 0%
4.2 V = 100%
```

Values outside the fixed endpoints clamp to 0% or 100%. Diagnostics show the
percentage as an approximate value.

Voltage-derived percentage is an estimate because Li-ion discharge voltage is
not linear and changes under load and temperature. Interfaces should mark it as
approximate. Low-battery alarms and critical cutoff decisions always compare
calibrated voltage against their configured voltage thresholds; they never use
the estimated percentage.

## Persistent records

Configuration is encoded field by field; firmware never writes the in-memory
C++ structure to flash. Each record contains a magic value, record-format
version, configuration schema version, generation, payload length, and CRC32.
Unknown future record/schema versions are rejected and routed to migration or
provisioning rather than interpreted as current data.

The `app_cfg` NVS partition stores two complete slots. Saving writes and commits
the inactive slot, reads it back through the decoder, then commits the active
marker. Loading validates both slots and chooses the highest valid generation,
so it can recover from a corrupted slot or power loss before marker update.

Factory reset erases only the application configuration namespace. It does not
erase firmware. The following boot sees no valid configuration and enters the
provisioning path.