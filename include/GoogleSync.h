#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GOOGLE_SYNC_DEVICE_ID_LEN 24
#define GOOGLE_SYNC_STATUS_LEN 96

typedef struct {
  const char* sync_event;   /* e.g. test_started, step_next, step_back, result_confirmed */
  int test_id;
  const char* step_title;
  int step_index;           /* 1-based */
  int step_count;
  bool has_result;          /* true when value/result fields are valid */
  const char* session_id;   /* optional override; usually auto-assigned */
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

/** Send one training sync event to configured endpoint. */
bool GoogleSync_sendResult(const GoogleSyncResult* result);

/** Send a connectivity test row to verify endpoint settings. */
bool GoogleSync_sendPing(void);

/** Clear current training sync session ID (new row key on next event). */
void GoogleSync_resetSession(void);

/** Copy current sync session ID (auto-generated if empty). */
void GoogleSync_getSessionId(char* buf, unsigned size);

#ifdef __cplusplus
}
#endif
