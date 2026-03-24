#include "OtaUpdate.h"
#include "AppState.h"
#include "WifiManager.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <WiFiClientSecure.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "dev"
#endif

#ifndef OTA_MANIFEST_URL
#define OTA_MANIFEST_URL ""
#endif

#ifndef OTA_CHANNEL
#define OTA_CHANNEL "stable"
#endif

#ifndef OTA_TLS_INSECURE
#define OTA_TLS_INSECURE 1
#endif

#ifndef OTA_TLS_ROOT_CA
#define OTA_TLS_ROOT_CA ""
#endif

static OtaManifest s_pending;
static bool s_hasPending = false;
static char s_lastStatus[OTA_STATUS_LEN] = "No OTA check yet.";

static void setStatus(const char* text) {
  if (!text) text = "";
  strncpy(s_lastStatus, text, sizeof(s_lastStatus) - 1);
  s_lastStatus[sizeof(s_lastStatus) - 1] = '\0';
}

static void strCopy(char* dst, unsigned size, const char* src) {
  if (!dst || size == 0) return;
  if (!src) src = "";
  strncpy(dst, src, size - 1);
  dst[size - 1] = '\0';
}

static int nextNumericToken(const char** p) {
  while (**p && !isdigit((unsigned char)**p)) (*p)++;
  if (!**p) return -1;
  int v = 0;
  while (isdigit((unsigned char)**p)) {
    v = v * 10 + (**p - '0');
    (*p)++;
  }
  return v;
}

/* Compare mixed version strings by numeric tokens: 1.2.10 > 1.2.3, 2026-06 > 2026-03 */
static int compareVersions(const char* a, const char* b) {
  const char* pa = a ? a : "";
  const char* pb = b ? b : "";
  for (int i = 0; i < 6; i++) {
    int va = nextNumericToken(&pa);
    int vb = nextNumericToken(&pb);
    if (va < 0 && vb < 0) return 0;
    if (va < 0) va = 0;
    if (vb < 0) vb = 0;
    if (va < vb) return -1;
    if (va > vb) return 1;
  }
  return 0;
}

static uint8_t rolloutBucket(void) {
  uint64_t mac = ESP.getEfuseMac();
  return (uint8_t)(mac % 100ULL);
}

static void configureSecureClient(WiFiClientSecure& client) {
#if OTA_TLS_INSECURE
  client.setInsecure();
#else
  client.setCACert(OTA_TLS_ROOT_CA);
#endif
  client.setTimeout(12000);
}

void OtaUpdate_init(void) {
  s_hasPending = false;
  memset(&s_pending, 0, sizeof(s_pending));
  setStatus("No OTA check yet.");
}

const char* OtaUpdate_getCurrentVersion(void) {
  return FIRMWARE_VERSION;
}

bool OtaUpdate_isConfigured(void) {
  char url[APP_STATE_OTA_URL_LEN];
  OtaUpdate_getManifestUrl(url, sizeof(url));
  return url[0] != '\0';
}

void OtaUpdate_getManifestUrl(char* buf, unsigned size) {
  if (!buf || size == 0) return;
  AppState_getOtaManifestUrl(buf, size);
  if (!buf[0]) strCopy(buf, size, OTA_MANIFEST_URL);
}

void OtaUpdate_getLastStatus(char* buf, unsigned size) {
  strCopy(buf, size, s_lastStatus);
}

static bool fetchManifestJson(char* jsonOut, unsigned jsonSize) {
  char url[OTA_URL_LEN];
  OtaUpdate_getManifestUrl(url, sizeof(url));
  if (!url[0]) {
    setStatus("OTA manifest URL is empty.");
    return false;
  }

  WiFiClientSecure client;
  configureSecureClient(client);

  HTTPClient http;
  if (!http.begin(client, url)) {
    setStatus("Unable to start manifest request.");
    return false;
  }
  http.setTimeout(12000);

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    char msg[OTA_STATUS_LEN];
    snprintf(msg, sizeof(msg), "Manifest HTTP error: %d", code);
    setStatus(msg);
    http.end();
    return false;
  }

  String body = http.getString();
  http.end();
  if (body.length() == 0) {
    setStatus("Manifest response is empty.");
    return false;
  }
  strCopy(jsonOut, jsonSize, body.c_str());
  return true;
}

static bool parseManifest(const char* json, OtaManifest* out) {
  if (!json || !out) return false;
  StaticJsonDocument<1024> doc;
  DeserializationError err = deserializeJson(doc, json);
  if (err) {
    setStatus("Manifest JSON parse failed.");
    return false;
  }

  const char* version = doc["version"] | "";
  const char* fw = doc["firmware_url"] | "";
  const char* md5 = doc["md5"] | "";
  const char* notes = doc["notes"] | "";
  const char* channel = doc["channel"] | "stable";
  int rollout = doc["rollout_pct"] | 100;
  bool force = doc["force"] | false;

  if (!version[0] || !fw[0]) {
    setStatus("Manifest missing version or firmware_url.");
    return false;
  }
  if (rollout < 1) rollout = 1;
  if (rollout > 100) rollout = 100;
  if (strcmp(channel, OTA_CHANNEL) != 0) {
    setStatus("Manifest channel does not match device channel.");
    return false;
  }

  memset(out, 0, sizeof(*out));
  strCopy(out->version, sizeof(out->version), version);
  strCopy(out->firmwareUrl, sizeof(out->firmwareUrl), fw);
  strCopy(out->md5, sizeof(out->md5), md5);
  strCopy(out->notes, sizeof(out->notes), notes);
  out->rolloutPercent = rollout;
  out->force = force;
  return true;
}

OtaCheckStatus OtaUpdate_checkNow(void) {
  s_hasPending = false;
  memset(&s_pending, 0, sizeof(s_pending));

  if (!WifiManager_isConnected()) {
    setStatus("OTA check skipped: WiFi not connected.");
    return OTA_CHECK_ERROR;
  }
  if (!OtaUpdate_isConfigured()) {
    setStatus("OTA check skipped: manifest URL not configured.");
    return OTA_CHECK_ERROR;
  }

  static char jsonBuf[1200];
  if (!fetchManifestJson(jsonBuf, sizeof(jsonBuf))) return OTA_CHECK_ERROR;

  OtaManifest candidate;
  if (!parseManifest(jsonBuf, &candidate)) return OTA_CHECK_ERROR;

  int cmp = compareVersions(OtaUpdate_getCurrentVersion(), candidate.version);
  if (cmp >= 0) {
    setStatus("No update available.");
    return OTA_CHECK_NO_UPDATE;
  }

  if (rolloutBucket() >= candidate.rolloutPercent) {
    setStatus("Update exists but not in this rollout bucket.");
    return OTA_CHECK_NO_UPDATE;
  }

  s_pending = candidate;
  s_hasPending = true;
  char msg[OTA_STATUS_LEN];
  snprintf(msg, sizeof(msg), "Update available: %s", s_pending.version);
  setStatus(msg);
  return OTA_CHECK_UPDATE_AVAILABLE;
}

bool OtaUpdate_hasPendingUpdate(void) {
  return s_hasPending;
}

void OtaUpdate_getPendingVersion(char* buf, unsigned size) {
  if (!buf || size == 0) return;
  if (!s_hasPending) {
    buf[0] = '\0';
    return;
  }
  strCopy(buf, size, s_pending.version);
}

bool OtaUpdate_installPending(void) {
  if (!s_hasPending || !s_pending.firmwareUrl[0]) {
    setStatus("No pending update to install.");
    return false;
  }
  if (!WifiManager_isConnected()) {
    setStatus("Install failed: WiFi not connected.");
    return false;
  }

  setStatus("Installing update...");
  WiFiClientSecure client;
  configureSecureClient(client);

  HTTPUpdate updater;
  updater.rebootOnUpdate(true);
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
  if (s_pending.md5[0]) updater.setMD5sum(s_pending.md5);
#else
  if (s_pending.md5[0]) updater.setMD5(s_pending.md5);
#endif
  t_httpUpdate_return ret = updater.update(client, s_pending.firmwareUrl, OtaUpdate_getCurrentVersion());

  if (ret == HTTP_UPDATE_OK) {
    setStatus("Update installed. Rebooting...");
    return true;
  }
  if (ret == HTTP_UPDATE_NO_UPDATES) {
    setStatus("Server reported no update.");
    return false;
  }
  char msg[OTA_STATUS_LEN];
  snprintf(msg, sizeof(msg), "Install failed: %s", updater.getLastErrorString().c_str());
  setStatus(msg);
  return false;
}

void OtaUpdate_runAutoFlow(void) {
  if (!AppState_getOtaAutoCheckEnabled()) return;
  OtaCheckStatus st = OtaUpdate_checkNow();
  if (st != OTA_CHECK_UPDATE_AVAILABLE) return;

  bool doInstall = AppState_getOtaAutoInstallEnabled() || s_pending.force;
  if (doInstall) OtaUpdate_installPending();
}
