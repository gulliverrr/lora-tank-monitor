# Security policy

Please report vulnerabilities privately through GitHub's security advisory
feature when available. Do not open a public issue containing credentials or
details that would expose a deployed device.

The project must not contain Wi-Fi passwords, Blynk tokens, private keys, real
device credentials, or installation-specific data. Provisioning and NVS
security limitations will be documented as those features are implemented.

Development builds currently provide CRC integrity and a dedicated application
NVS partition, but do not provide confidentiality at rest. The classic ESP32
cannot use the newer HMAC-backed NVS key protection, and the partition's
`encrypted` flag has no effect while flash encryption is disabled. Anyone with
physical flash access can recover stored Wi-Fi and Blynk credentials.

Production releases should enable ESP32 flash encryption and secure boot using
Espressif's production provisioning process. These eFuse changes are
irreversible and must not be enabled automatically by development firmware.

Version 1 LoRa pairing filters packets by identity and CRC but does not
cryptographically authenticate them. It therefore does not protect against a
nearby attacker forging or replaying radio packets.