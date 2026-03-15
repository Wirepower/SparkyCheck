#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GOOGLE_SYNC_DEVICE_ID_LEN 24
#define GOOGLE_SYNC_STATUS_LEN 96

typedef struct {
  const char* report_id;
  const char* test_name;
  const char* value;
  const char* unit;
  bool passed;
  const char* clause;
} GoogleSyncResult;

/** Initialize status text. */
void GoogleSync_init(void);

/** True when Training sync is enabled and endpoint URL is configured. */
bool GoogleSync_isConfigured(void);

/** Deterministic device ID derived from chip MAC (e.g., ESP32-1A2B3C). */
void GoogleSync_getDeviceId(char* buf, unsigned size);

/** Last sync status for UI display. */
void GoogleSync_getLastStatus(char* buf, unsigned size);

/** Send one completed test result to configured Google endpoint. */
bool GoogleSync_sendResult(const GoogleSyncResult* result);

/** Send a connectivity test row to verify endpoint settings. */
bool GoogleSync_sendPing(void);

#ifdef __cplusplus
}
#endif
