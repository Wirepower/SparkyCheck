/**
 * SparkyCheck – App state (mode, session) persisted in NVS.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Operating mode: Training (apprentice) or Field (qualified). */
typedef enum {
  APP_MODE_TRAINING = 0,
  APP_MODE_FIELD    = 1
} AppMode;

/** Load saved mode from NVS; default Training. */
void AppState_load(void);

/** Save current mode to NVS. */
void AppState_save(void);

/** Get current mode. */
AppMode AppState_getMode(void);

/** Set mode and sync to Standards layer + NVS. */
void AppState_setMode(AppMode mode);

/** True if Field mode (3760 active). */
bool AppState_isFieldMode(void);

/** Default PIN (minimum 4 digits). Change via AppState_setPin() from protected menu. */
#define APP_STATE_DEFAULT_PIN 12345

/** Check if the given PIN is correct (for protected menu in Training mode). */
bool AppState_checkPin(uint32_t pin);

/** Get stored PIN (returns default if never set). */
uint32_t AppState_getPin(void);

/** Set new PIN (e.g. after teacher changes it). */
void AppState_setPin(uint32_t pin);

/** Screen rotation: 0 = portrait, 1 = landscape. Persisted in NVS. */
int AppState_getRotation(void);
void AppState_setRotation(int rotation);

/** Buzzer on/off. Persisted in NVS. Default on (1). */
bool AppState_getBuzzerEnabled(void);
void AppState_setBuzzerEnabled(bool on);

/** WiFi credentials (saved for reconnect). */
void AppState_getWifiCredentials(char* ssid, unsigned ssid_size, char* pass, unsigned pass_size);
void AppState_setWifiCredentials(const char* ssid, const char* pass);

/** OTA settings (manifest URL + automation toggles). */
#define APP_STATE_OTA_URL_LEN 192
void AppState_getOtaManifestUrl(char* buf, unsigned size);
void AppState_setOtaManifestUrl(const char* s);
bool AppState_getOtaAutoCheckEnabled(void);
void AppState_setOtaAutoCheckEnabled(bool on);
bool AppState_getOtaAutoInstallEnabled(void);
void AppState_setOtaAutoInstallEnabled(bool on);

/** Training-mode cloud sync settings (optional endpoint). */
#define APP_STATE_TRAINING_SYNC_URL_LEN 192
#define APP_STATE_TRAINING_SYNC_TOKEN_LEN 96
#define APP_STATE_TRAINING_SYNC_CUBICLE_LEN 24
#define APP_STATE_DEVICE_ID_LEN 24
typedef enum {
  TRAINING_SYNC_TARGET_AUTO = 0,
  TRAINING_SYNC_TARGET_GOOGLE = 1,
  TRAINING_SYNC_TARGET_SHAREPOINT = 2
} TrainingSyncTarget;
bool AppState_getTrainingSyncEnabled(void);
void AppState_setTrainingSyncEnabled(bool on);
void AppState_getTrainingSyncEndpoint(char* buf, unsigned size);
void AppState_setTrainingSyncEndpoint(const char* s);
void AppState_getTrainingSyncToken(char* buf, unsigned size);
void AppState_setTrainingSyncToken(const char* s);
void AppState_getTrainingSyncCubicleId(char* buf, unsigned size);
void AppState_setTrainingSyncCubicleId(const char* s);
void AppState_getDeviceIdOverride(char* buf, unsigned size);
void AppState_setDeviceIdOverride(const char* s);
TrainingSyncTarget AppState_getTrainingSyncTarget(void);
void AppState_setTrainingSyncTarget(TrainingSyncTarget target);
bool AppState_getEmailReportEnabled(void);
void AppState_setEmailReportEnabled(bool on);

/** Email (SMTP) settings – device sender. PIN-protected in UI. */
#define APP_STATE_EMAIL_STR_LEN 64
void AppState_getSmtpServer(char* buf, unsigned size);
void AppState_setSmtpServer(const char* s);
void AppState_getSmtpPort(char* buf, unsigned size);
void AppState_setSmtpPort(const char* s);
void AppState_getSmtpUser(char* buf, unsigned size);
void AppState_setSmtpUser(const char* s);
void AppState_getSmtpPass(char* buf, unsigned size);
void AppState_setSmtpPass(const char* s);
/** Report recipient: Teacher (Training) or Recipient (Field). One field, label differs by mode. */
void AppState_getReportToEmail(char* buf, unsigned size);
void AppState_setReportToEmail(const char* s);

#ifdef __cplusplus
}
#endif
