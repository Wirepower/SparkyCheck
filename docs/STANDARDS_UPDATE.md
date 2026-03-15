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

## Verification steps and test limits (OTA-updatable)

Step text, clause references, and pass/fail limits are kept in a small set of files so they can be updated when standards change—including via **OTA firmware or rules updates** without changing core app logic.

| Content | Files | When to update |
|--------|--------|----------------|
| Test names and step sequences (safety, verify Yes/No, result entry) | `src/VerificationSteps.cpp`, `include/VerificationSteps.h` | New edition of AS/NZS 3000 Section 8 or 3017 changes required steps or wording |
| Pass/fail limits (continuity, IR, RCD, EFLI) | `src/TestLimits.cpp`, `include/TestLimits.h` | Standard revises numeric limits or criteria |
| Standard editions and rules version | `include/StandardsConfig.h`, `src/StandardsConfig.cpp` | New edition numbers; bump rules version for reports |

**When you ship an OTA update that only changes steps or limits:**

1. Update the relevant files above (or deliver updated `VerificationSteps.cpp` / `TestLimits.cpp` / config).
2. In `StandardsConfig.cpp`, update `Standards_getRulesVersion()` to a new string (e.g. `"2026-06"`) so reports record which rule set was used.
3. No change to main app flow is required; the same APIs are used.

Future option: load step text and limits from a config file (e.g. JSON) on LittleFS or from an OTA-delivered file, and fall back to built-in data if the file is missing. The same `VerificationSteps_*` and `TestLimits_*` APIs can be preserved.

---

## Future: config files and OTA

To support updating rules without reflashing firmware:

- Store standard editions, limits, clause refs, and step text in a config file (e.g. JSON) on the SD card or in LittleFS.
- On startup (or after OTA), load that config and use it instead of the built-in `StandardEditions`, `VerificationSteps`, and `TestLimits` data.
- Keep the same `Standards.h`, `VerificationSteps.h`, and `TestLimits.h` APIs so the rest of the app is unchanged; only the config layer changes.

This keeps AS/NZS 3000, 3017, 3760 and any future standards easy to update as regulations change.
