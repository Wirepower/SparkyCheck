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

This repository is already configured to:

`https://raw.githubusercontent.com/Wirepower/SparkyCheck/main/ota/manifest-stable.json`

If your repository path changes, update that URL.

TLS:

- Current default is `OTA_TLS_INSECURE=1` (encrypted transport, no certificate pinning).
- For production-hardening, set `OTA_TLS_INSECURE=0` and define `OTA_TLS_ROOT_CA`.

---

## 2) Where devices retrieve updates from

Devices do **not** pull binaries directly from GitHub by themselves first. They do this in two steps:

1. Fetch the manifest URL from `OTA_MANIFEST_URL` (or NVS override).
2. Read `firmware_url` inside the manifest and download that `.bin`.

So your deployment needs:

- a stable manifest URL (constant),
- a reachable `.bin` URL (changes each release).

---

## 3) GitHub setup (recommended)

Yes, GitHub can host the whole OTA flow.

### What this repo now includes

- `ota/manifest-stable.json` (stable manifest path)
- `.github/workflows/ota-release.yml` (auto OTA pipeline on push to `main`)
- `scripts/generate_ota_manifest.py` (manifest generator)

### Required repo setting

- Repository should be **public** for direct unauthenticated device downloads from raw/release URLs.
  - If private, devices cannot read raw/release URLs without auth (not implemented in firmware).

### Release workflow behavior

Automatic path (default):

- Push firmware code to `main` (`src/`, `include/`, `lib/`, or `platformio.ini` changes).
- Workflow auto-builds and auto-publishes OTA for `stable` channel.

Manual path (optional):

- Run **Actions → OTA Release → Run workflow** to set custom `version`, `channel`, rollout, and force flag.

It will:

1. Build firmware (`pio run`).
2. Create release asset: `sparkycheck-<version>.bin`.
3. Compute MD5.
4. Update `ota/manifest-<channel>.json`.
5. Commit/push manifest update.
6. Create/update GitHub Release tag `fw-<version>` with the `.bin` asset.

---

## 4) Manifest format

You need a stable HTTPS location for:

- `manifest.json`
- firmware `.bin` files

Common options:

- Cloud storage + CDN
- Static web host
- GitHub release assets + a stable manifest URL on your site

---

Example `firmware_url` using this repo’s GitHub release assets:

`https://github.com/Wirepower/SparkyCheck/releases/download/fw-2026.03.16/sparkycheck-2026.03.16.bin`

Example:

```json
{
  "channel": "stable",
  "version": "2026.03.16",
  "firmware_url": "https://github.com/Wirepower/SparkyCheck/releases/download/fw-2026.03.16/sparkycheck-2026.03.16.bin",
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

## 5) Provision devices (one-time bootstrap)

1. Flash/update all devices once to firmware containing this OTA module.
2. Ensure each device has WiFi credentials in **Settings → WiFi connection**.
3. In **Settings → Firmware updates**, confirm:
   - Manifest URL is configured (`OTA_MANIFEST_URL` build flag).
   - Auto-check is ON.
   - Auto-install is ON (default in this implementation).

After this bootstrap, updates can be pushed remotely by updating manifest + hosting a new `.bin`.

---

## 6) Release workflow (each bugfix release)

### Easiest (fully automatic)

1. Push firmware changes to `main`.
2. GitHub Actions will:
   - generate a unique build version for that push,
   - build firmware,
   - publish `.bin` as a GitHub Release asset,
   - update `ota/manifest-stable.json`.
3. Devices on `stable` update automatically.

### Controlled rollout (optional)

Use manual **OTA Release** workflow dispatch to set:

- `channel` (`stable` / `beta`)
- `rollout_pct` (for staged release)
- `force` (if required)

At `100`, all online devices in that channel will update automatically.

---

## 7) Recommended fleet model

- Use **channels**:
  - `beta` for internal testers
  - `stable` for production electricians
- Keep a rollback artifact available (previous good `.bin` + manifest).
- Treat manifest changes as controlled release operations.

---

## 8) Operational notes

- Devices check on boot (auto-flow).
- Manual controls available in **Settings → Firmware updates**:
  - Check now
  - Install now
  - Toggle auto-check
  - Toggle auto-install
- If a device is offline, it updates once it reconnects and checks again.

