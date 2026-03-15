# How to update SparkyCheck when newer AS/NZS standards are released

This guide explains **what to do** and **which files to change** when a new edition of AS/NZS 3000, 3017, or 3760 is published, so the device continues to reflect current Australian/New Zealand electrical standards.

---

## When to do this

- A **new edition** of one of the standards is published (e.g. AS/NZS 3000:2024 replaces 3000:2018).
- A standard is **amended** and clause numbers, limits, or required test steps change.
- The standard **adds a new required test** — see **Adding a new test type** below.
- The standard **removes a required test** (no longer in Section 8 / 3017) — see **Removing a test type** below.
- The standard **alters an existing test** (wording, add/remove steps, new limits or clause numbers) — see **Altering an existing test** below.
- You want reports and the **About** screen to show the correct edition and rules version.

You can update by **editing the source and reflashing** the device, or later by **OTA** (over-the-air) if you use that for firmware updates.

---

## Overview: what gets updated

| What | Where it lives | You change |
|------|----------------|------------|
| **Standard edition text** (e.g. "AS/NZS 3000:2024") | `StandardsConfig.h` / `StandardsConfig.cpp` | Edition strings and any section/title text |
| **Pass/fail limits** (e.g. continuity ≤ 0.5 Ω, IR ≥ 1 MΩ) | `TestLimits.cpp` | Numeric limits and any comments |
| **Altering an existing test** (wording, steps, limits, clauses) | `VerificationSteps.cpp`, `TestLimits.cpp` | See **Altering an existing test** below |
| **Adding a new test type** (standard adds a required test) | `VerificationSteps.h` + `.cpp` (+ `TestLimits` if numeric result) | See **Adding a new test type** below |
| **Removing a test type** (standard no longer requires a test) | `VerificationSteps.h` + `.cpp` (+ `TestLimits` if it had a unique result) | See **Removing a test type** below |
| **Rules version** (shown in reports and About) | `StandardsConfig.cpp` | The string returned by `Standards_getRulesVersion()` |

You do **not** need to change main app flow or screen touch logic—the test list is data-driven: add/remove/alter tests in `VerificationSteps` and the **Select test** screen follows automatically.

---

## Step-by-step: update after a new standard edition

### 1. Update the standard edition and display text

**Files:** `include/StandardsConfig.h`, `src/StandardsConfig.cpp`

1. Open **`include/StandardsConfig.h`**.
2. In the `StandardEditions` struct, update the edition strings, for example:
   - `"AS/NZS 3000:2018"` → `"AS/NZS 3000:2024"`
   - `"AS/NZS 3017:2022"` → `"AS/NZS 3017:2025"` (when applicable)
   - Same for **AS/NZS 3760** if it has a new edition.
3. Open **`src/StandardsConfig.cpp`**.
4. In **`Standards_getInfo()`**, update any **section** or **title** text if the new standard uses different wording (e.g. Section 8 name or scope).

This is what the **About** screen and any “current standards” text use.

---

### 2. Update pass/fail limits

**Files:** `src/TestLimits.cpp`, and if needed `include/TestLimits.h`

1. Open **`src/TestLimits.cpp`**.
2. Change the numeric limits to match the new standard, for example:
   - **Continuity:** `TestLimits_continuityMaxOhms()` (e.g. 0.5 Ω).
   - **Insulation:** `TestLimits_insulationMinMOhms()` (e.g. 1.0 MΩ) and `TestLimits_insulationMinMOhmsSheathedHeating()` (e.g. 0.01 MΩ).
   - **RCD trip time:** `TestLimits_rcdTripTimeMaxMs()` (e.g. 30 ms).
   - **EFLI:** `TestLimits_efliMaxOhms()` (check AS/NZS 3000 tables for your case).
3. Update the file header comment if you add new limits or change structure.

If the new standard introduces a **new type of limit**, add a new function in `TestLimits.cpp` and declare it in `TestLimits.h`; then use it from `VerificationSteps.cpp` where that result is validated.

---

### 3. Update verification steps and clause references

**Files:** `src/VerificationSteps.cpp`, and if needed `include/VerificationSteps.h`

1. Open **`src/VerificationSteps.cpp`**.
2. **Test names** (e.g. “Earth continuity (conductors)”): update the `s_testNames[]` array if the new standard uses different terminology.
3. **Step text**: in each test’s step array (e.g. `s_continuity[]`, `s_insulation[]`), update:
   - **Safety and instruction text** to match the new edition.
   - **Clause references** (e.g. `"AS/NZS 3000 Clause 8.3.5"`) to the correct clause numbers in the new standard.
4. If the new standard **adds or removes a step** for a test, add or remove an entry in the corresponding step array and ensure the **result step** (if any) still has the correct `resultKind` and validation (see `VerificationSteps_validateResult` and `TestLimits_*`).
5. In **`VerificationSteps_getClauseForResult()`**, update any clause strings returned for reports.

Keep **zeroing of test leads** in the relevant tests (continuity, earth continuity) if the standard still requires it.

---

### 4. Bump the rules version (important)

**File:** `src/StandardsConfig.cpp`

1. Open **`src/StandardsConfig.cpp`**.
2. Find **`Standards_getRulesVersion()`**.
3. Change the returned string to a **new value** whenever you ship updated rules, for example:
   - `"2026-03"` → `"2026-06"` (or use the year and month of the new standard / your release).

This string is shown in **reports** and in **About**, so users and auditors can see which rule set the device was using.

---

## Deploying the update

### Option A: Build and upload via USB (typical)

1. Save all edited files.
2. In PlatformIO: **Build** (`pio run`).
3. Connect the device via USB and **Upload** (`pio run -t upload`).
4. Optionally run **Upload filesystem** if you use a custom partition and need it (`pio run -t uploadfs`).

The device will now use the new editions, limits, steps, and rules version.

### Option B: OTA (over-the-air)

1. Perform all the file changes above and build the firmware as usual.
2. Use your normal OTA method (e.g. PlatformIO OTA upload, or an in-app “Check for updates” flow if you add one).
3. Ensure the device is on **WiFi** (e.g. via **Settings → WiFi connection**).
4. Upload the new firmware image to the device.

After OTA, the device runs the new rules; no need to plug in USB.

---

## Altering an existing test (wording, steps, limits, or clauses)

When the standard **changes** an existing test (same test, but different wording, more/fewer steps, new limits, or new clause numbers), update the data only—no enum or UI changes.

### What to change

**Files:** `src/VerificationSteps.cpp`, `src/TestLimits.cpp`, and if needed `include/VerificationSteps.h` / `include/TestLimits.h`

1. **Test name** — In **`s_testNames[]`**, change the label for that test if the standard uses different terminology.

2. **Step text and clauses** — In that test’s step array (e.g. `s_continuity[]`, `s_rcd[]`):
   - Edit **safety and instruction** strings to match the new edition.
   - Update **clause references** (e.g. `"AS/NZS 3000 Clause 8.3.5"`) to the new clause numbers.
   - **Add a step:** add another `{ STEP_..., "Title", "Instruction", "Clause", ... }` entry in the array. The count in **`s_stepCounts[]`** updates automatically if you use `sizeof(array)/sizeof(array[0])` for that test.
   - **Remove a step:** delete the entry from the array. If you remove the **result entry** step, ensure the test still ends with a verification step (e.g. Yes/No) so the coach can show “Verified” and generate a report; no change to `TestLimits` or `VerifyResultKind` needed for that test.

3. **Pass/fail limits** — If the standard changes a **numeric limit** (e.g. continuity max, IR min, RCD trip time), update the corresponding function(s) in **`TestLimits.cpp`** (and the comment in **`VerificationSteps.cpp`** if the step text mentions the value).

4. **Clause for reports** — If the clause for a result type changes, update **`VerificationSteps_getClauseForResult()`** in **`VerificationSteps.cpp`** for that `VerifyResultKind`.

5. **Bump the rules version** in **`StandardsConfig.cpp`** when you ship the update.

---

## Adding a new test type (when the standard requires an additional test)

When AS/NZS 3000 Section 8, 3017, or 3760 introduces a **new required verification test** (not just a change to an existing one), add it as follows. The app’s **Select test** screen is driven by `VerificationSteps`—no changes are needed in `Screens.cpp`; the new test will appear in the list once added here.

### 1. Add the test to the enum

**File:** `include/VerificationSteps.h`

- In **`VerifyTestId`**, add a new enum value **before** `VERIFY_TEST_COUNT`, for example:
  - `VERIFY_NEW_TEST_NAME`,
- `VERIFY_TEST_COUNT` will then include it automatically.

### 2. Add the test name and steps

**File:** `src/VerificationSteps.cpp`

1. **Test name:** In **`s_testNames[]`**, add one more string (in the **same order** as the enum). For example: `"New test name (as in standard)"`.

2. **Step sequence:** Define a new step array (e.g. `s_newtest[]`) using the same pattern as existing tests:
   - **`STEP_SAFETY`** — Mandatory safety/zeroing text and clause; user taps “OK / I have done this”.
   - **`STEP_VERIFY_YESNO`** — e.g. “Is the circuit isolated?” or similar; user taps Yes/No.
   - **`STEP_RESULT_ENTRY`** — Only if the test has a **numeric result** (e.g. a reading with pass/fail). Use `resultKind`, `resultLabel`, and `unit` (e.g. `"ohm"`, `"MOhm"`, `"ms"`). Otherwise use **`STEP_VERIFY_YESNO`** or **`STEP_INFO`** for the last step.
   - **`STEP_INFO`** — Informational step; user taps OK.

   Each step needs: `type`, `title`, `instruction`, `clause`, and for result steps: `resultKind`, `resultLabel`, `unit` (others `RESULT_NONE`, `NULL`, `NULL`).

3. **Register the test:** In **`s_steps[]`**, add the new array (e.g. `s_newtest`). In **`s_stepCounts[]`**, add the count for that array (e.g. `sizeof(s_newtest) / sizeof(s_newtest[0])`). Keep the **same order** as in `VerifyTestId` and `s_testNames[]`.

### 3. If the new test has a numeric result (pass/fail limit)

**Files:** `include/VerificationSteps.h`, `src/VerificationSteps.cpp`, `include/TestLimits.h`, `src/TestLimits.cpp`

1. **`include/VerificationSteps.h`** — In **`VerifyResultKind`**, add a new value (e.g. `RESULT_NEW_OHM`).

2. **`src/TestLimits.cpp`** — Implement the limit (e.g. `TestLimits_newTestMaxOhms()`) and a pass function (e.g. `TestLimits_newTestPass()`). Declare them in **`include/TestLimits.h`**.

3. **`src/VerificationSteps.cpp`** — In **`VerificationSteps_validateResult()`**, add a `case` for the new `VerifyResultKind` that calls the new TestLimits function. In **`VerificationSteps_getClauseForResult()`**, add a `case` that returns the correct clause string for reports.

4. In your new step array, use the new **`resultKind`**, plus **`resultLabel`** and **`unit`** for the result step.

If the new test is **verification only** (no numeric reading), use **`RESULT_NONE`** for all steps and do not add anything to `TestLimits` or `validateResult`/`getClauseForResult`. The coach will show “Verified” and generate a report line with value “Verified”.

### 4. Bump the rules version

After adding the test (and any new limits), update **`Standards_getRulesVersion()`** in **`src/StandardsConfig.cpp`** so reports show the new rule set.

---

## Removing a test type (when the standard no longer requires a test)

When the standard **drops** a verification test (e.g. it’s removed from Section 8 or 3017), remove it from the app so it no longer appears in **Select test**.

### 1. Remove the test from the enum and arrays

**File:** `include/VerificationSteps.h`

- In **`VerifyTestId`**, **delete** the enum value for the test you are removing (e.g. `VERIFY_SOMETHING`).
- Ensure the remaining values are still in a continuous sequence (0, 1, 2, …) so that **`VERIFY_TEST_COUNT`** is correct. Do **not** leave a gap (e.g. don’t keep `VERIFY_REMOVED = 99`); remove the line entirely so the list renumbers.

**File:** `src/VerificationSteps.cpp`

1. **Test name:** Remove the corresponding entry from **`s_testNames[]`** (same index as the enum you removed).
2. **Step array:** Delete the step array for that test (e.g. `s_something[]`) and remove its entries from **`s_steps[]`** and **`s_stepCounts[]`**. The order of the three arrays must still match the order in **`VerifyTestId`** (first enum = first name = first in `s_steps` / `s_stepCounts`).

### 2. If the removed test had a unique numeric result kind

Only do this if **no other test** uses the same `VerifyResultKind`.

**Files:** `include/VerificationSteps.h`, `src/VerificationSteps.cpp`, `include/TestLimits.h`, `src/TestLimits.cpp`

1. **`include/VerificationSteps.h`** — In **`VerifyResultKind`**, remove the enum value (e.g. `RESULT_SOMETHING_OHM`). Keep the enum contiguous (no gaps).
2. **`src/VerificationSteps.cpp`** — In **`VerificationSteps_validateResult()`**, remove the `case` for that kind. In **`VerificationSteps_getClauseForResult()`**, remove the `case` for that kind.
3. **`src/TestLimits.cpp`** — Remove the limit and pass functions for that result (e.g. `TestLimits_somethingMaxOhms()`, `TestLimits_somethingPass()`). Remove their declarations from **`include/TestLimits.h`**.

If **another test** still uses that result kind (e.g. two tests both used `RESULT_CONTINUITY_OHM`), do **not** remove the `VerifyResultKind` or the TestLimits functions; only remove the test’s step array, name, and enum entry as in step 1.

### 3. Bump the rules version

Update **`Standards_getRulesVersion()`** in **`src/StandardsConfig.cpp`** so reports reflect the updated rule set.

---

## Adding a completely new standard

If a **new** AS/NZS standard (e.g. a new number) must be supported:

1. **`include/Standards.h`**  
   Add a new value to the `StandardId` enum (e.g. `STANDARD_3XXX`).

2. **`include/StandardsConfig.h`**  
   Add an edition string in `StandardEditions` (e.g. `AS_NZS_3XXX = "AS/NZS 3XXX:2025"`).

3. **`src/StandardsConfig.cpp`**  
   - In **`Standards_getInfo()`**, add a case for the new ID (short name, section, title).  
   - In **`Standards_isActiveInCurrentMode()`**, set whether it applies in **Training** only, **Field** only, or both.

4. If the new standard defines **new tests or limits**, add them using **Adding a new test type** above and the pass/fail limits section; no separate UI changes are required.

For more detail on the code layout, see **`docs/STANDARDS_UPDATE.md`**.

---

## Quick checklist

Before you consider the update done:

- [ ] Edition strings updated in **`StandardsConfig.h`** (and any section/title text in **`StandardsConfig.cpp`**).
- [ ] Pass/fail limits updated in **`TestLimits.cpp`** (and **`TestLimits.h`** if you added or removed a limit type).
- [ ] **Altered tests:** Step text, clause refs, and step count updated in **`VerificationSteps.cpp`**; limits in **`TestLimits.cpp`**; **`VerificationSteps_getClauseForResult()`** if clause for a result type changed.
- [ ] **New test:** **`VerifyTestId`**, **`s_testNames[]`**, step array, **`s_steps[]`**, **`s_stepCounts[]`** updated; if it has a numeric result, **`VerifyResultKind`**, **TestLimits**, **`validateResult`** and **`getClauseForResult`** updated.
- [ ] **Removed test:** **`VerifyTestId`** (and enum order), **`s_testNames[]`**, step array, **`s_steps[]`**, **`s_stepCounts[]`** updated; if it had a **unique** result kind, **`VerifyResultKind`**, **TestLimits**, **`validateResult`** and **`getClauseForResult`** updated.
- [ ] **Rules version** bumped in **`StandardsConfig.cpp`** → **`Standards_getRulesVersion()`**.
- [ ] Project **builds** and you have **uploaded** the new firmware (USB or OTA).

---

## Reference

- **Technical detail and file map:** `docs/STANDARDS_UPDATE.md`  
- **General project and build:** main **`README.md`** in the project root
