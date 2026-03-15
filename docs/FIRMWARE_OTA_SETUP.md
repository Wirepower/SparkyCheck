# Fleet firmware OTA setup (push fixes to all devices)

This guide sets up SparkyCheck so devices can check for firmware updates and install them automatically from a hosted manifest.

---

## What is now implemented in firmware

- Device fetches an OTA manifest JSON over WiFi.
- Compares `version` in manifest vs local `FIRMWARE_VERSION`.
- Honors rollout percentage (`rollout_pct`) for staged deployments.
- Installs update from `firmware_url` when eligible.
- Runs auto-check on boot; auto-install can be toggled in **Settings → Firmware updates**.

Key files:

- `src/OtaUpdate.cpp`, `include/OtaUpdate.h`
- `src/Screens.cpp` (Firmware updates UI)
- `src/main.cpp` (boot auto-flow call)
- `src/AppState.cpp` / `include/AppState.h` (persist OTA settings)

---

## 1) One-time project configuration

In `platformio.ini` build flags:

- `FIRMWARE_VERSION` should be updated every release (for example `2026.03.15`).
- Set `OTA_MANIFEST_URL` to your hosted manifest JSON URL.
- Keep `OTA_CHANNEL="stable"` unless you want multiple channels.

Example:

```ini
-DFIRMWARE_VERSION=\"2026.03.15\"
-DOTA_MANIFEST_URL=\"https://your-host.example.com/sparkycheck/manifest.json\"
-DOTA_CHANNEL=\"stable\"
```

TLS:

- Current default is `OTA_TLS_INSECURE=1` (encrypted transport, no certificate pinning).
- For production-hardening, set `OTA_TLS_INSECURE=0` and define `OTA_TLS_ROOT_CA`.

---

## 2) Host firmware binaries + manifest

You need a stable HTTPS location for:

- `manifest.json`
- firmware `.bin` files

Common options:

- Cloud storage + CDN
- Static web host
- GitHub release assets + a stable manifest URL on your site

---

## 3) Manifest format

Example:

```json
{
  "channel": "stable",
  "version": "2026.03.16",
  "firmware_url": "https://your-host.example.com/sparkycheck/sparkycheck-2026.03.16.bin",
  "md5": "6f5902ac237024bdd0c176cb93063dc4",
  "rollout_pct": 100,
  "force": false,
  "notes": "Fixes report export and WiFi reconnect reliability."
}
```

Fields:

- `channel` (string): must match device `OTA_CHANNEL`.
- `version` (string): newer than current firmware version.
- `firmware_url` (string): direct URL to `.bin`.
- `md5` (optional): checksum verification before install.
- `rollout_pct` (1-100): staged rollout percentage.
- `force` (bool): force install when update is available.
- `notes` (optional): informational.

---

## 4) Provision devices (one-time bootstrap)

1. Flash/update all devices once to firmware containing this OTA module.
2. Ensure each device has WiFi credentials in **Settings → WiFi connection**.
3. In **Settings → Firmware updates**, confirm:
   - Manifest URL is configured.
   - Auto-check is ON.
   - Auto-install is ON (default in this implementation).

After this bootstrap, updates can be pushed remotely by updating manifest + hosting a new `.bin`.

---

## 5) Release workflow (each bugfix release)

1. Change `FIRMWARE_VERSION` to a newer value.
2. Build firmware: `pio run`.
3. Locate generated `.bin` in `.pio/build/<env>/`.
4. Compute MD5 of `.bin`.
5. Upload `.bin` to your OTA host.
6. Update `manifest.json` to the new version and URL.
7. Start with staged rollout (for example `rollout_pct: 10`).
8. Monitor fleet behavior, then move to `25`, `50`, `100`.

At `100`, all online devices in that channel will update automatically.

---

## 6) Recommended fleet model

- Use **channels**:
  - `beta` for internal testers
  - `stable` for production electricians
- Keep a rollback artifact available (previous good `.bin` + manifest).
- Treat manifest changes as controlled release operations.

---

## 7) Operational notes

- Devices check on boot (auto-flow).
- Manual controls available in **Settings → Firmware updates**:
  - Check now
  - Install now
  - Toggle auto-check
  - Toggle auto-install
- If a device is offline, it updates once it reconnects and checks again.

