#include "GoogleSync.h"

#include "AppState.h"
#include "OtaUpdate.h"
#include "Standards.h"
#include "WifiManager.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <stdio.h>
#include <string.h>

static char s_lastStatus[GOOGLE_SYNC_STATUS_LEN] = "Training sync idle.";

static void setStatus(const char* text) {
  if (!text) text = "";
  strncpy(s_lastStatus, text, sizeof(s_lastStatus) - 1);
  s_lastStatus[sizeof(s_lastStatus) - 1] = '\0';
}

void GoogleSync_init(void) {
  setStatus("Training sync idle.");
}

void GoogleSync_getDeviceId(char* buf, unsigned size) {
  if (!buf || size == 0) return;
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

bool GoogleSync_sendResult(const GoogleSyncResult* result) {
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
  if (!WifiManager_isConnected()) {
    setStatus("Sync skipped: WiFi disconnected.");
    return false;
  }

  char endpoint[APP_STATE_TRAINING_SYNC_URL_LEN];
  char token[APP_STATE_TRAINING_SYNC_TOKEN_LEN];
  char cubicle[APP_STATE_TRAINING_SYNC_CUBICLE_LEN];
  char deviceId[GOOGLE_SYNC_DEVICE_ID_LEN];
  char rulesVer[16];
  AppState_getTrainingSyncEndpoint(endpoint, sizeof(endpoint));
  AppState_getTrainingSyncToken(token, sizeof(token));
  AppState_getTrainingSyncCubicleId(cubicle, sizeof(cubicle));
  GoogleSync_getDeviceId(deviceId, sizeof(deviceId));
  Standards_getRulesVersion(rulesVer, sizeof(rulesVer));

  if (!endpoint[0]) {
    setStatus("Sync skipped: endpoint empty.");
    return false;
  }

  StaticJsonDocument<768> doc;
  doc["auth_token"] = token;
  doc["device_id"] = deviceId;
  doc["cubicle_id"] = cubicle;
  doc["mode"] = "training";
  doc["report_id"] = result->report_id ? result->report_id : "";
  doc["test_name"] = result->test_name ? result->test_name : "";
  doc["value"] = result->value ? result->value : "";
  doc["unit"] = result->unit ? result->unit : "";
  doc["result"] = result->passed ? "PASS" : "FAIL";
  doc["passed"] = result->passed;
  doc["clause"] = result->clause ? result->clause : "";
  doc["rules_version"] = rulesVer;
  doc["firmware_version"] = OtaUpdate_getCurrentVersion();
  doc["ts_ms"] = (uint32_t)millis();

  String payload;
  serializeJson(doc, payload);
  int code = 0;
  bool ok = postPayload(endpoint, payload, &code);
  if (ok) {
    setStatus("Training sync sent.");
    return true;
  }

  char msg[GOOGLE_SYNC_STATUS_LEN];
  if (code > 0) snprintf(msg, sizeof(msg), "Sync failed: HTTP %d", code);
  else snprintf(msg, sizeof(msg), "Sync failed: request error.");
  setStatus(msg);
  return false;
}

bool GoogleSync_sendPing(void) {
  GoogleSyncResult ping;
  ping.report_id = "Ping";
  ping.test_name = "Connectivity test";
  ping.value = "N/A";
  ping.unit = "";
  ping.passed = true;
  ping.clause = "";
  return GoogleSync_sendResult(&ping);
}
