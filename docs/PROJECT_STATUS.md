# SparkyCheck – Project status

**Hardware:** ESP32-S3-Touch-LCD-4.3B (Waveshare). 4.3" capacitive touchscreen, **no physical buttons** — all interaction via touch. **Mode switching (Training / Field) via software** (saved in NVS), not a jumper.

---

## Vision (as per spec)

SparkyCheck is a portable, battery-powered handheld device for electricians and apprentices (e.g. TAFE) to assist with mandatory inspection, testing, and verification under Australian/New Zealand electrical standards. It acts as an **electrical verification coach**: step-by-step guidance, safety reminders, manual result entry with pass/fail validation, and professional reports (CSV + HTML) for print or email.

- **Training Mode** — apprentices under supervision; more guidance, automatic teacher reporting.
- **Field Mode** — qualified electricians; full control, stricter compliance, AS/NZS 3760 (test & tag) fully active.

Standards: **AS/NZS 3000:2018** (Section 8), **AS/NZS 3017:2022**, **AS/NZS 3760:2022** (Field only). Special handling for sheathed heating elements (e.g. ≥0.01 MΩ IR exception). **Zeroing of test leads** is required for resistance tests (continuity, earth continuity) so lead resistance is not included in results; the coach reminds the user and states this in the relevant steps. The standards layer and verification steps are designed to be **updatable/upgradable** when newer AS/NZS editions are published—via source updates or OTA (see `STANDARDS_UPDATE.md`).

---

## Implemented – ready to run

| Item | Description |
|------|--------------|
| **Boot 1** | Creator credit (“Created by Frank Offer 2026”), industry graphic; touch or 6 s to continue |
| **Boot 2** | Safety disclaimer; must tap “I Accept” to continue (no bypass) |
| **Mode select** | In **Settings** only; **PIN protected** (default 12345). Training vs Field; choice saved in NVS (3760 only in Field). Teachers/authorised users enter PIN to change mode. |
| **Main menu** | Start verification, View reports, Settings |
| **Test flow** | One full sequence: (1) Mandatory “Zero your test leads” reminder → must tap “I have zeroed”, (2) Continuity: enter value (0.35 / 0.5 / 1.0 ohm demo buttons), Confirm, (3) Pass/fail result (green/red + buzzer), (4) “End session” → generates report |
| **Pass/fail limits** | Continuity ≤ 0.5 Ω; IR limits (general + sheathed heating) in `TestLimits` for future tests |
| **Report generation** | CSV + styled HTML to LittleFS (`/reports/Job_&lt;timestamp&gt;.csv` and `.html`); clause refs, rules version |
| **Report list** | View saved report basenames from storage |
| **Report saved** | Confirmation screen with basename; OK → main menu |
| **Settings** | Screen rotation, WiFi, Buzzer, **About**, **Firmware updates**, Email, Change mode, Change PIN (Training). Back to menu |
| **Training sync (optional)** | In Training mode, PIN-gated setup to post completed test results to a shared Google Sheet endpoint |
| **About** | What SparkyCheck is, created by Frank Offer, current AS/NZS standards in use (editions), rules version |
| **Firmware updates** | In-app OTA controls: check now, install now, auto-check toggle, auto-install toggle |
| **Buzzer** | Optional GPIO (`BUZZER_PIN`, default -1); startup chime, pass/fail beeps |
| **Standards** | 3000, 3017, 3760; mode-aware; updatable editions and limits |

---

## Flow (end-to-end)

1. Power on → Boot 1 (creator) → touch or 6 s  
2. → Disclaimer → tap “I Accept”  
3. → Mode select (Training / Field) → tap choice then “Continue”  
4. → Main menu → “Start verification”  
5. → Safety reminder “Zero your test leads” → “I have zeroed”  
6. → Continuity: tap value (e.g. 0.35), “Confirm” → Pass or Fail + buzzer  
7. → “End session” → report written to storage → “Report saved” → OK  
8. → Main menu; “View reports” lists basenames; “Settings” shows mode  

---

## Still to add (optional next steps)

| # | Capability | Notes |
|---|------------|--------|
| 1 | More tests | IR, polarity, earth, RCD (with limits in TestLimits); multi-step sequence |
| 2 | Report export | Wi‑Fi hotspot download, BT, email, USB mass storage |
| 3 | SD card | Use SD instead of/with LittleFS; browse, delete, format |
| 4 | OTA hardening | Add certificate pinning (`OTA_TLS_INSECURE=0`) and rollout telemetry |
| 5 | Buzzer GPIO | Set `BUZZER_PIN` in build flags or `Buzzer.h` if hardware present |
| 6 | Numeric keypad | For arbitrary continuity/IR entry instead of preset buttons |
| 7 | Change PIN | Allow teacher to set a new PIN from Settings (after entering current PIN) |

---

## Build and upload

- **Build:** `pio run` (or PlatformIO: Build).  
- **Upload:** `pio run -t upload`.  
- **Upload filesystem** (if using a partition that needs formatting): `pio run -t uploadfs` (optional; report storage uses LittleFS and will format on first use if supported by partition).  
- If report save fails at runtime, ensure the board partition table includes a filesystem (e.g. SPIFFS/LittleFS) partition; see platformio.ini and Espressif docs for custom partition tables.
