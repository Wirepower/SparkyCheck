/**
 * SparkyCheck – Piezo buzzer (optional).
 *
 * Hardware: Adafruit Large Enclosed Piezo Element w/Wires (ADA1739), e.g.:
 *   https://core-electronics.com.au/large-enclosed-piezo-element-w-wires.html
 * Resonant frequencies ~1300 Hz and ~4000 Hz; works with 3–5 V square wave. Tone
 * frequencies are chosen in that range for good volume.
 *
 * GPIO: set BUZZER_PIN when known. Options:
 *   • platformio.ini build_flags: -DBUZZER_PIN=12  (example)
 *   • Or define in a config header included before this
 * Use -1 or leave undefined to disable (no buzzer connected).
 */

#pragma once

/** GPIO for piezo buzzer (-1 = disabled). Set when hardware is wired. */
#ifndef BUZZER_PIN
#define BUZZER_PIN (-1)
#endif

#ifdef __cplusplus
extern "C" {
#endif

void Buzzer_init(void);
void Buzzer_beepClick(void);   /* Short tap for button press (respects Settings) */
void Buzzer_beepPass(void);   /* Happy tone – pass (respects Settings) */
void Buzzer_beepFail(void);   /* Sad tone – fail (respects Settings) */
void Buzzer_beepWarning(void); /* Single medium beep (respects Settings) */
void Buzzer_startupChime(void); /* Boot-finished chime – always plays, cannot be turned off */

#ifdef __cplusplus
}
#endif
