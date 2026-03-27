/**
 * Piezo buzzer: Adafruit Large Enclosed Piezo (ADA1739), driven with PWM/tone.
 * Resonant ~1300 Hz and ~4000 Hz – tones chosen for good volume in that range.
 * GPIO = BUZZER_PIN (set in platformio.ini when known).
 */

#include "Buzzer.h"
#include "AppState.h"
#include <Arduino.h>
#include <esp_arduino_version.h>

#if BUZZER_PIN >= 0
#define BUZZER_LEDC_CHANNEL  0
#define BUZZER_LEDC_FREQ     2000
#define BUZZER_LEDC_RES_BITS 8
#endif

void Buzzer_init(void) {
#if BUZZER_PIN >= 0
  Serial.printf("[Buzzer] init: BUZZER_PIN=%d\n", BUZZER_PIN);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
  Serial.println("[Buzzer] LEDC attach(pin, freq, bits)");
  ledcAttach(BUZZER_PIN, BUZZER_LEDC_FREQ, BUZZER_LEDC_RES_BITS);
#else
  Serial.println("[Buzzer] LEDC setup + attachPin(channel)");
  ledcSetup(BUZZER_LEDC_CHANNEL, BUZZER_LEDC_FREQ, BUZZER_LEDC_RES_BITS);
  ledcAttachPin(BUZZER_PIN, BUZZER_LEDC_CHANNEL);
#endif
#else
  Serial.println("[Buzzer] disabled (BUZZER_PIN < 0)");
#endif
}

#if BUZZER_PIN >= 0
/** Play a tone at frequency (Hz) for duration (ms). 0 Hz = silence. */
static void toneMs(unsigned int freqHz, int durationMs) {
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
  const uint8_t toneTarget = BUZZER_PIN;
#else
  const uint8_t toneTarget = BUZZER_LEDC_CHANNEL;
#endif
  if (freqHz > 0)
    ledcWriteTone(toneTarget, (uint32_t)freqHz);
  else
    ledcWriteTone(toneTarget, 0);
  if (durationMs > 0)
    delay((unsigned int)durationMs);
  ledcWriteTone(toneTarget, 0);
}

static void beep(unsigned int freqHz, int ms) {
  if (!AppState_getBuzzerEnabled()) return;
  toneMs(freqHz, ms);
}
/** Unmuted for boot chime only (always plays). */
static void beepUnmuted(unsigned int freqHz, int ms) {
  toneMs(freqHz, ms);
}
#endif

void Buzzer_beepClick(void) {
#if BUZZER_PIN >= 0
  if (!AppState_getBuzzerEnabled()) return;
  beep(1300, 25);   /* Near piezo resonance for a crisp tap */
#endif
}

void Buzzer_beepPass(void) {
#if BUZZER_PIN >= 0
  if (!AppState_getBuzzerEnabled()) return;
  beep(1200, 80);
  delay(60);
  beep(1500, 80);   /* Two-note “happy” in resonant band */
#endif
}

void Buzzer_beepFail(void) {
#if BUZZER_PIN >= 0
  if (!AppState_getBuzzerEnabled()) return;
  beep(400, 300);   /* Low “sad” tone */
#endif
}

void Buzzer_beepWarning(void) {
#if BUZZER_PIN >= 0
  if (!AppState_getBuzzerEnabled()) return;
  beep(1300, 150);  /* Near resonance for clear alert */
#endif
}

void Buzzer_startupChime(void) {
#if BUZZER_PIN >= 0
  Serial.printf("[Buzzer] startup chime: pin=%d enabled=%d\n", BUZZER_PIN, AppState_getBuzzerEnabled() ? 1 : 0);
  /* Always play: boot finished, ready for user input. Not affected by Settings. */
  beepUnmuted(1000, 50);
  delay(40);
  beepUnmuted(1200, 50);
  delay(40);
  beepUnmuted(1300, 80);   /* Rise into resonant band */
  Serial.println("[Buzzer] startup chime complete");
#endif
}
