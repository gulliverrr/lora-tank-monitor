# Provisioning

When persistent configuration is missing or invalid, firmware starts an open
Wi-Fi access point named `LoRaTank-XXXX`. The suffix is derived from the device
identity, so users do not need to know or type a MAC address. The OLED shows the
AP name and portal address.

Connect a phone or computer to the AP and open:

```text
http://192.168.4.1/
```

A wildcard DNS service resolves requested host names to the device and common
Android, Apple, Windows, and ChromeOS captive-network probes redirect to the
portal. Some phones may still require manually opening `192.168.4.1`.

The portal supports asynchronous Wi-Fi rescanning and staged forms for
device role, Wi-Fi, LoRa, tank/calibration, sensor timing, alarms, battery, and
Blynk settings. Submissions are URL-decoded into a temporary bounded
configuration and pass the same cross-field validator used at boot. A valid
stage enables **Save and reboot**. Saving uses the verified dual-slot NVS
transaction, replies to the browser, then reboots.

State-changing requests require a random per-boot session token obtained by the
same-origin page. Wi-Fi passwords and Blynk tokens are write-only: a blank
field preserves an existing staged value, and status APIs never return secrets.

After valid configuration is saved, normal boot does not start the AP. Hold the
middle GPIO38 button while resetting to re-enter provisioning. The page loads
saved nonsecret fields from a redacted endpoint; password and token inputs stay
blank. Factory reset requires both a browser confirmation and an explicit
confirmation header, erases application configuration only, and reboots back
into provisioning.

The AP is deliberately open by project decision. Provision only nearby in a
trusted physical environment. The final operational flow disables the AP after
a valid configuration is saved and requires a deliberate local action to
re-enter provisioning.

## Transport verification

1. Confirm the OLED shows `PROVISIONING`, the AP name, and `192.168.4.1`.
2. Join the displayed AP from a phone.
3. Browse directly to `http://192.168.4.1/` if the operating system does not
   open the portal automatically.
4. Browse to `http://192.168.4.1/api/status` and confirm JSON contains the AP
   name and IP but no credentials.
5. Browse to a nonexistent path and confirm it redirects to the portal root.
6. Select **Scan networks**, confirm nearby SSIDs appear, and select one.
7. Keep the default TX role and select **Validate staged configuration**.
   Confirm the page reports a valid staged configuration without rebooting.
8. Reload before saving and confirm staged nonsecret edits remain populated.
9. Select **Save and reboot**. The AP should disappear after the response.
10. Monitor serial and confirm lines similar to:

```text
config_store: Loaded slot 0 generation 1
Persistent configuration is valid: generation=1 role=1 tank=Tank 1
```

11. Hold the middle button while resetting, reconnect to the AP, and confirm
   saved nonsecret values are prefilled while password/token fields are blank.
12. Use **Factory reset**, confirm the destructive prompt, and verify reboot
   returns to an unconfigured provisioning state without erasing firmware.