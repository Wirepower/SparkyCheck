# SparkyCheck EEZ UI (Mockup-Based)

This folder contains an **editable EEZ Studio project** built from the mockup layouts:

- `SparkyCheck-Mockup-UI.eez-project`

## What this is

- A visual UI scaffold in EEZ Studio format (LVGL project).
- Screens map to the mockups:
  - Overview
  - Boot_Security_Modes
  - Field_Mode_Testing
  - Training_Sync
  - RCD_Context
  - SWP_Procedures
  - Operations

## Open and edit in EEZ Studio

1. Install EEZ Studio.
2. Open `SparkyCheck-Mockup-UI.eez-project`.
3. Edit pages/widgets visually (labels, buttons, spacing, colors, fonts).
4. Build from EEZ Studio to generate code into `src/ui` (default path in project settings).

## Integration note (important)

Current firmware uses a custom UI engine (`src/Screens.cpp`) and does **not** yet run LVGL in `main.cpp`.

So this EEZ project is currently a **design/editable scaffold**.
To make it run on device, a next step is required:

- Add LVGL runtime initialization and display/touch glue in firmware.
- Call generated `ui_init()` and `ui_tick()` in the app loop.
- Wire widget events to existing app logic (AppState, OtaUpdate, GoogleSync, etc.).

## Recommended next step

Start by integrating one EEZ screen (Main Menu equivalent) in parallel with current UI, then migrate screen-by-screen.

