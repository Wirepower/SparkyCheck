Boot / splash assets for SparkyCheck.

A pre-made "boot hero" image (electrical verification theme) is available in the
Cursor project assets folder. Copy it here as boot_hero.png if you want to show
it on the device (e.g. via SPIFFS/LittleFS or by converting to a C array).

The first boot screen is currently drawn in code (see src/BootScreen.cpp) and
shows "SparkyCheck", "Created by Frank", and a checkmark/cable graphic.
