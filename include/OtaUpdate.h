#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OTA_VERSION_LEN 24
#define OTA_URL_LEN 192
#define OTA_NOTE_LEN 96
#define OTA_STATUS_LEN 128

typedef enum {
  OTA_CHECK_ERROR = -1,
  OTA_CHECK_NO_UPDATE = 0,
  OTA_CHECK_UPDATE_AVAILABLE = 1
} OtaCheckStatus;

typedef struct {
  char version[OTA_VERSION_LEN];
  char firmwareUrl[OTA_URL_LEN];
  char md5[40];
  char notes[OTA_NOTE_LEN];
  bool force;
  int rolloutPercent;
} OtaManifest;

/** Initialize internal OTA state. */
void OtaUpdate_init(void);

/** Current running firmware version string (build-time constant). */
const char* OtaUpdate_getCurrentVersion(void);

/** True if a manifest URL is configured (NVS or build flag fallback). */
bool OtaUpdate_isConfigured(void);

/** Returns the active manifest URL into buf (NVS value or build default). */
void OtaUpdate_getManifestUrl(char* buf, unsigned size);

/** Last OTA status text (for UI). */
void OtaUpdate_getLastStatus(char* buf, unsigned size);

/** Fetch manifest and compare versions. */
OtaCheckStatus OtaUpdate_checkNow(void);

/** True if check found an update and a firmware URL is ready. */
bool OtaUpdate_hasPendingUpdate(void);

/** Pending update target version (empty string if none). */
void OtaUpdate_getPendingVersion(char* buf, unsigned size);

/** Download and install pending update. Usually reboots on success. */
bool OtaUpdate_installPending(void);

/** Auto-check and optional auto-install according to AppState toggles. */
void OtaUpdate_runAutoFlow(void);

#ifdef __cplusplus
}
#endif
