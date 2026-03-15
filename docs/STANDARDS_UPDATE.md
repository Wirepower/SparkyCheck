# Updating standards and regulations

SparkyCheck is built around Australian/New Zealand electrical standards that are revised from time to time. The firmware is structured so you can update to new editions without rewriting core logic.

## Standards in use

| Standard | Use in SparkyCheck | When to update |
|----------|--------------------|----------------|
| **AS/NZS 3000** | Wiring Rules – especially Section 8 (Verification & Testing) | When a new edition (e.g. 3000:2024) is published and adopted |
| **AS/NZS 3017** | Verification guidelines | When a new edition is published |
| **AS/NZS 3760** | In-service safety inspection and testing | Active only in **Field mode**; update when the standard is revised |

## How to implement new rules and regulations

### 1. Update edition and text

- **Files:** `include/StandardsConfig.h`, `src/StandardsConfig.cpp`
- In `StandardEditions`, change the strings (e.g. `"AS/NZS 3000:2018"` → `"AS/NZS 3000:2024"`).
- In `Standards_getInfo()` in `StandardsConfig.cpp`, update any section or title text (e.g. Section 8 wording) to match the new standard.

### 2. Update pass/fail limits and clause references

- **File:** `include/StandardsConfig.h` (and/or new tables in `StandardsConfig.cpp`)
- Add or adjust limits (e.g. continuity ≤ 0.5 Ω, insulation resistance ≥ 1 MΩ, RCD trip times) and clause references so they match the current edition.
- Keep all such values in the Standards config layer so the rest of the app uses “the current rules” without hardcoding numbers.

### 3. Add a new standard

- **File:** `include/Standards.h`
  - Add a new value to the `StandardId` enum (e.g. `STANDARD_3XXX`).
- **File:** `src/StandardsConfig.cpp`
  - In `Standards_getInfo()`, add a case for the new ID with short name, section, and title.
  - In `Standards_isActiveInCurrentMode()`, define whether it applies in Training mode, Field mode, or both.

### 4. Rules version for reports

- **File:** `src/StandardsConfig.cpp`
- In `Standards_getRulesVersion()`, change the returned string (e.g. `"2026-03"`) whenever you ship updated rules, so reports and logs can record which rule set was used.

## Future: config files and OTA

To support updating rules without reflashing firmware:

- Store standard editions, limits, and clause refs in a config file (e.g. JSON) on the SD card or in SPIFFS/LittleFS.
- On startup (or after OTA), load that config and use it instead of the built-in `StandardEditions` and tables.
- Keep the same `Standards.h` API so the rest of the app still calls `Standards_getInfo()`, `Standards_isActiveInCurrentMode()`, etc., and only the config layer changes.

This keeps AS/NZS 3000, 3017, 3760 and any future standards easy to update as regulations change.
