#include "GoogleSync.h"

#include "AppState.h"
#include "OtaUpdate.h"
#include "Standards.h"
#include "WifiManager.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <WiFiClientSecure.h>
#include <stdio.h>
#include <string.h>

static char s_lastStatus[GOOGLE_SYNC_STATUS_LEN] = "Training sync idle.";
static char s_sessionId[32] = "";
static bool s_fsReady = false;
static unsigned long s_nextFlushMs = 0;
static const char* kQueuePath = "/sync_queue.ndjson";
static const char* kQueueTmpPath = "/sync_queue.tmp";

static void setStatus(const char* text) {
  if (!text) text = "";
  strncpy(s_lastStatus, text, sizeof(s_lastStatus) - 1);
  s_lastStatus[sizeof(s_lastStatus) - 1] = '\0';
}

static void ensureSessionId(void) {
  if (s_sessionId[0]) return;
  uint64_t mac = ESP.getEfuseMac();
  unsigned long s = millis() / 1000UL;
  snprintf(s_sessionId, sizeof(s_sessionId), "TS-%06llX-%lu",
           (unsigned long long)(mac & 0xFFFFFFULL), s);
}

static const char* targetName(TrainingSyncTarget t) {
  switch (t) {
    case TRAINING_SYNC_TARGET_GOOGLE: return "google";
    case TRAINING_SYNC_TARGET_SHAREPOINT: return "sharepoint";
    default: return "auto";
  }
}

static bool postPayload(const char* endpoint, const String& payload, int* httpCode);

static bool ensureFsReady(void) {
  if (s_fsReady) return true;
  s_fsReady = LittleFS.begin(true);
  return s_fsReady;
}

static bool queueAppend(const String& payload) {
  if (!ensureFsReady()) return false;
  File f = LittleFS.open(kQueuePath, "a");
  if (!f) return false;
  f.print(payload);
  f.print('\n');
  f.close();
  return true;
}

static bool flushQueue(const char* endpoint) {
  if (!endpoint || !endpoint[0]) return false;
  if (!ensureFsReady()) return false;
  if (!LittleFS.exists(kQueuePath)) return true;

  File in = LittleFS.open(kQueuePath, "r");
  if (!in) return false;
  File out = LittleFS.open(kQueueTmpPath, "w");
  if (!out) { in.close(); return false; }

  bool stillSending = true;
  int queuedLeft = 0;
  while (in.available()) {
    String line = in.readStringUntil('\n');
    line.trim();
    if (!line.length()) continue;

    if (stillSending) {
      int code = 0;
      if (postPayload(endpoint, line, &code)) continue;
      stillSending = false;
    }
    out.print(line);
    out.print('\n');
    queuedLeft++;
  }
  in.close();
  out.close();

  if (stillSending) {
    LittleFS.remove(kQueuePath);
    LittleFS.remove(kQueueTmpPath);
    return true;
  }

  LittleFS.remove(kQueuePath);
  if (!LittleFS.rename(kQueueTmpPath, kQueuePath)) {
    setStatus("Queue flush failed (rename).");
    return false;
  }
  char msg[GOOGLE_SYNC_STATUS_LEN];
  snprintf(msg, sizeof(msg), "Queue pending: %d", queuedLeft);
  setStatus(msg);
  return false;
}

void GoogleSync_init(void) {
  s_sessionId[0] = '\0';
  s_nextFlushMs = 0;
  ensureFsReady();
  setStatus("Training sync idle.");
}

void GoogleSync_getDeviceId(char* buf, unsigned size) {
  if (!buf || size == 0) return;
  char overrideId[APP_STATE_DEVICE_ID_LEN];
  AppState_getDeviceIdOverride(overrideId, sizeof(overrideId));
  if (overrideId[0]) {
    strncpy(buf, overrideId, size - 1);
    buf[size - 1] = '\0';
    return;
  }
  uint64_t mac = ESP.getEfuseMac();
  snprintf(buf, size, "ESP32-%06llX", (unsigned long long)(mac & 0xFFFFFFULL));
}

bool GoogleSync_isConfigured(void) {
  if (AppState_isFieldMode()) return false;
  if (!AppState_getTrainingSyncEnabled()) return false;
  char endpoint[APP_STATE_TRAINING_SYNC_URL_LEN];
  AppState_getTrainingSyncEndpoint(endpoint, sizeof(endpoint));
  return endpoint[0] != '\0';
}

void GoogleSync_getLastStatus(char* buf, unsigned size) {
  if (!buf || size == 0) return;
  strncpy(buf, s_lastStatus, size - 1);
  buf[size - 1] = '\0';
}

static bool postPayload(const char* endpoint, const String& payload, int* httpCode) {
  if (!endpoint || !endpoint[0]) return false;
  HTTPClient http;
  int code = 0;
  bool okBegin = false;
  WiFiClientSecure secure;

  if (strncmp(endpoint, "https://", 8) == 0) {
    secure.setInsecure();  // For simple setup; can be hardened later with pinned certs.
    secure.setTimeout(12000);
    okBegin = http.begin(secure, endpoint);
  } else {
    okBegin = http.begin(endpoint);
  }
  if (!okBegin) return false;

  http.setTimeout(12000);
  http.addHeader("Content-Type", "application/json");
  code = http.POST((uint8_t*)payload.c_str(), payload.length());
  if (httpCode) *httpCode = code;
  http.end();
  return code >= 200 && code < 300;
}

static bool buildTrainingSyncPayload(const GoogleSyncResult* result, String& payload, char* endpoint, size_t endpointCap) {
  if (!result) {
    setStatus("Sync skipped: no payload.");
    return false;
  }
  if (AppState_isFieldMode()) {
    setStatus("Sync skipped: Field mode.");
    return false;
  }
  if (!AppState_getTrainingSyncEnabled()) {
    setStatus("Sync skipped: disabled.");
    return false;
  }

  char token[APP_STATE_TRAINING_SYNC_TOKEN_LEN];
  char cubicle[APP_STATE_TRAINING_SYNC_CUBICLE_LEN];
  char deviceId[GOOGLE_SYNC_DEVICE_ID_LEN];
  char rulesVer[16];
  TrainingSyncTarget target = AppState_getTrainingSyncTarget();
  AppState_getTrainingSyncEndpoint(endpoint, endpointCap);
  AppState_getTrainingSyncToken(token, sizeof(token));
  AppState_getTrainingSyncCubicleId(cubicle, sizeof(cubicle));
  GoogleSync_getDeviceId(deviceId, sizeof(deviceId));
  Standards_getRulesVersion(rulesVer, sizeof(rulesVer));

  if (!endpoint[0]) {
    setStatus("Sync skipped: endpoint empty.");
    return false;
  }
  const char* session = result->session_id;
  if (!session || !session[0]) {
    ensureSessionId();
    session = s_sessionId;
  }

  StaticJsonDocument<768> doc;
  doc["auth_token"] = token;
  doc["device_id"] = deviceId;
  doc["cubicle_id"] = cubicle;
  doc["mode"] = "training";
  doc["session_id"] = session;
  doc["sync_event"] = result->sync_event ? result->sync_event : "update";
  doc["sync_target"] = targetName(target);
  doc["email_reporting_enabled"] = AppState_getEmailReportEnabled();
  doc["test_id"] = result->test_id;
  doc["test_key"] = result->test_key ? result->test_key : "";
  doc["step_title"] = result->step_title ? result->step_title : "";
  doc["step_index"] = result->step_index;
  doc["step_count"] = result->step_count;
  doc["has_result"] = result->has_result;
  doc["student_id"] = result->student_id ? result->student_id : "";
  doc["test_started_ms"] = result->test_started_ms;
  doc["test_completed_ms"] = result->test_completed_ms;
  doc["report_id"] = result->report_id ? result->report_id : "";
  doc["test_name"] = result->test_name ? result->test_name : "";
  doc["value"] = (result->has_result && result->value) ? result->value : "";
  doc["unit"] = (result->has_result && result->unit) ? result->unit : "";
  doc["result"] = result->has_result ? (result->passed ? "PASS" : "FAIL") : "";
  doc["passed"] = result->has_result ? result->passed : false;
  doc["clause"] = (result->has_result && result->clause) ? result->clause : "";
  doc["rules_version"] = rulesVer;
  doc["firmware_version"] = OtaUpdate_getCurrentVersion();
  doc["ts_ms"] = (uint32_t)millis();

  serializeJson(doc, payload);
  return true;
}

bool GoogleSync_queueResult(const GoogleSyncResult* result) {
  char endpoint[APP_STATE_TRAINING_SYNC_URL_LEN];
  String payload;
  if (!buildTrainingSyncPayload(result, payload, endpoint, sizeof(endpoint))) return false;
  /* Never flush/sync HTTP here — runs from UI/touch path; flushQueue blocks and trips the WDT. */
  (void)endpoint;
  if (queueAppend(payload)) {
    setStatus("Training sync queued.");
    return true;
  }
  setStatus("Queue append failed.");
  return false;
}

bool GoogleSync_sendResult(const GoogleSyncResult* result) {
  char endpoint[APP_STATE_TRAINING_SYNC_URL_LEN];
  String payload;
  if (!buildTrainingSyncPayload(result, payload, endpoint, sizeof(endpoint))) return false;

  /* Do not flushQueue here — runs from UI path; blocks on HTTPS and can trip the WDT. Drain via GoogleSync_tick(). */

  if (!WifiManager_isConnected()) {
    if (queueAppend(payload)) {
      setStatus("Queued offline (no WiFi).");
    } else {
      setStatus("Queue failed (no WiFi).");
    }
    return false;
  }

  int code = 0;
  bool ok = postPayload(endpoint, payload, &code);
  if (ok) {
    setStatus("Training sync sent.");
    return true;
  }

  if (queueAppend(payload)) {
    if (code > 0) {
      char msg[GOOGLE_SYNC_STATUS_LEN];
      snprintf(msg, sizeof(msg), "Queued after HTTP %d", code);
      setStatus(msg);
    } else {
      setStatus("Queued after send failure.");
    }
    return false;
  }

  char msg[GOOGLE_SYNC_STATUS_LEN];
  if (code > 0) snprintf(msg, sizeof(msg), "Sync failed: HTTP %d", code);
  else snprintf(msg, sizeof(msg), "Sync failed: request error.");
  setStatus(msg);
  return false;
}

bool GoogleSync_sendPing(void) {
  GoogleSyncResult ping;
  ping.sync_event = "ping";
  ping.test_id = -1;
  ping.test_key = "";
  ping.step_title = "";
  ping.step_index = 0;
  ping.step_count = 0;
  ping.has_result = true;
  ping.session_id = "";
  ping.student_id = "";
  ping.test_started_ms = 0;
  ping.test_completed_ms = 0;
  ping.report_id = "Ping";
  ping.test_name = "Connectivity test";
  ping.value = "N/A";
  ping.unit = "";
  ping.passed = true;
  ping.clause = "";
  return GoogleSync_sendResult(&ping);
}

void GoogleSync_tick(void) {
  if (AppState_isFieldMode()) return;
  if (!AppState_getTrainingSyncEnabled()) return;
  if (!WifiManager_isConnected()) return;
  unsigned long now = millis();
  if (now < s_nextFlushMs) return;
  s_nextFlushMs = now + 5000UL;

  char endpoint[APP_STATE_TRAINING_SYNC_URL_LEN];
  AppState_getTrainingSyncEndpoint(endpoint, sizeof(endpoint));
  if (!endpoint[0]) return;
  flushQueue(endpoint);
}

void GoogleSync_resetSession(void) {
  s_sessionId[0] = '\0';
}

void GoogleSync_getSessionId(char* buf, unsigned size) {
  if (!buf || size == 0) return;
  ensureSessionId();
  strncpy(buf, s_sessionId, size - 1);
  buf[size - 1] = '\0';
}
