# Component boundaries

Each runtime concern will be an independently testable ESP-IDF component. The
directories are introduced only when their first implementation is buildable.

| Component | Responsibility |
| --- | --- |
| `board` | Confirmed board profile, PMU, pins, buttons, battery, and sleep |
| `config` | Versioned schema, defaults, validation, migration, and redaction |
| `storage` | Atomic NVS persistence and factory reset |
| `provisioning` | Captive DNS/HTTP portal and staged configuration |
| `wifi` | Event-driven scanning, connection, and retry state |
| `lora` | Radio abstraction and SX127x implementation |
| `protocol` | Versioned packet codec, integrity, sequencing, and pairing |
| `sensor` | Ultrasonic sensor abstraction and JSN-SR04T implementation |
| `tank` | Pure distance, level, percentage, and volume calculations |
| `alarms` | Configurable threshold, hysteresis, and confirmation states |
| `blynk` | Native HTTPS publishing and event reporting |
| `display` | OLED screens and button-driven menu state |
| `diagnostics` | Bounded counters and redacted status snapshots |
| `system` | Shared timing, identity, reset, and application event types |

Application code depends on component interfaces, not concrete hardware
drivers. Physical and deployment-specific values come from persistent
configuration rather than component constants.