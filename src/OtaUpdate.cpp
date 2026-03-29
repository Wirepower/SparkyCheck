#include "OtaUpdate.h"
#include "AppState.h"
#include "WifiManager.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
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

static SparkyTft* s_installTft = nullptr;
static unsigned long s_otaProgressLastMs = 0;
static int s_otaProgressLastPct = -1;

/* Live "working" clock while HTTPUpdate blocks in TLS/GET (no onProgress yet). */
static SemaphoreHandle_t s_otaUiMutex = nullptr;
static volatile bool s_otaPulseAbort = false;
static volatile bool s_otaPulseTaskDone = true;
static TaskHandle_t s_otaPulseHandle = nullptr;
static unsigned long s_otaConnectStartMs = 0;

void OtaUpdate_setInstallDisplay(SparkyTft* tft) {
  s_installTft = tft;
}

/**
 * statusOverride: non-null replaces the line under the bar (e.g. while HTTP GET + TLS still running;
 * onProgress is not called until after the response headers are received).
 */
static void otaDrawInstallProgress(SparkyTft* tft, size_t cur, size_t total, const char* statusOverride) {
  if (!tft) return;
  const int w = tft->width();
  const int h = tft->height();
  const int marginX = 40;
  const int barY = h / 2 - 4;
  const int barH = 32;
  const int barW = w - 2 * marginX;
  const uint16_t kTrack = 0x2965;
  const uint16_t kFill = 0x07E0;

  tft->fillScreen(TFT_BLACK);
  tft->setTextWrap(false);
  tft->setTextColor(TFT_WHITE, TFT_BLACK);
  tft->setTextSize(2);
  const char* headline = "Firmware update";
  int tw = (int)strlen(headline) * 12;
  tft->setCursor((w - tw) / 2, h / 4);
  tft->print(headline);

  char sub[72];
  if (s_pending.version[0]) {
    snprintf(sub, sizeof(sub), "Installing v%s", s_pending.version);
  } else {
    strncpy(sub, "Installing update", sizeof(sub) - 1);
    sub[sizeof(sub) - 1] = '\0';
  }
  tft->setTextSize(1);
  tw = (int)strlen(sub) * 6;
  if (tw > w - 24) {
    snprintf(sub, sizeof(sub), "v%s", s_pending.version);
    tw = (int)strlen(sub) * 6;
  }
  tft->setCursor((w - tw) / 2, h / 4 + 26);
  tft->print(sub);

  /* While size unknown (TLS/GET or unknown Content-Length): explain possible wait. */
  if (cur == 0 && total == 0) {
    tft->setTextColor(0xBDF7, TFT_BLACK);
    const char* waitHint = "HTTPS link can take 1-3 min on Wi-Fi";
    tw = (int)strlen(waitHint) * 6;
    if (tw > w - 20) {
      waitHint = "Wait: TLS + download starting";
      tw = (int)strlen(waitHint) * 6;
    }
    tft->setCursor((w - tw) / 2, h / 4 + 38);
    tft->print(waitHint);
  }

  tft->setTextColor(TFT_WHITE, TFT_BLACK);
  tft->drawRoundRect(marginX, barY, barW, barH, 8, TFT_WHITE);
  const int inner = barW - 4;
  const int innerH = barH - 4;
  tft->fillRect(marginX + 2, barY + 2, inner, innerH, kTrack);

  if (total > 0) {
    int fillW = (int)((uint64_t)cur * (uint64_t)inner / (uint64_t)total);
    if (fillW > inner) fillW = inner;
    if (fillW > 0) tft->fillRect(marginX + 2, barY + 2, fillW, innerH, kFill);
  } else if (cur > 0) {
    int chunk = inner / 5;
    if (chunk < 12) chunk = 12;
    int travel = inner - chunk;
    if (travel < 1) travel = 1;
    int pos = (int)((millis() / 100) % (travel * 2));
    if (pos > travel) pos = 2 * travel - pos;
    tft->fillRect(marginX + 2 + pos, barY + 2, chunk, innerH, kFill);
  }

  tft->setTextSize(2);
  char pctb[20];
  if (statusOverride && statusOverride[0]) {
    strncpy(pctb, statusOverride, sizeof(pctb) - 1);
    pctb[sizeof(pctb) - 1] = '\0';
  } else if (total > 0) {
    int pct = (int)((uint64_t)cur * 100 / (uint64_t)total);
    if (pct > 100) pct = 100;
    snprintf(pctb, sizeof(pctb), "%d%%", pct);
  } else if (cur == 0) {
    strncpy(pctb, "Connecting...", sizeof(pctb) - 1);
    pctb[sizeof(pctb) - 1] = '\0';
  } else {
    strncpy(pctb, "Downloading", sizeof(pctb) - 1);
    pctb[sizeof(pctb) - 1] = '\0';
  }
  tw = (int)strlen(pctb) * 12;
  if (tw > w - 16) {
    tft->setTextSize(1);
    tw = (int)strlen(pctb) * 6;
  }
  tft->setTextColor(TFT_WHITE, TFT_BLACK);
  tft->setCursor((w - tw) / 2, barY + barH + 16);
  tft->print(pctb);

  tft->setTextSize(1);
  const char* hint = "Do not power off";
  tw = (int)strlen(hint) * 6;
  tft->setCursor((w - tw) / 2, h - 40);
  tft->print(hint);
}

static void otaUiMutexEnsure(void) {
  if (s_otaUiMutex) return;
  s_otaUiMutex = xSemaphoreCreateMutex();
}

static void otaPulseStopAndJoin(void) {
  s_otaPulseAbort = true;
  const unsigned long t0 = millis();
  while (!s_otaPulseTaskDone && (millis() - t0) < 5000UL) {
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

static void otaConnectingPulseTask(void* arg) {
  (void)arg;
  otaUiMutexEnsure();
  s_otaPulseTaskDone = false;
  for (;;) {
    if (s_otaPulseAbort) break;
    if (s_installTft && s_otaUiMutex) {
      if (xSemaphoreTake(s_otaUiMutex, pdMS_TO_TICKS(300)) == pdTRUE) {
        const unsigned sec = (unsigned)((millis() - s_otaConnectStartMs) / 1000UL);
        char line[36];
        const unsigned m = sec / 60U;
        const unsigned s = sec % 60U;
        if (m > 0)
          snprintf(line, sizeof(line), "Working %um %02us", m, s);
        else
          snprintf(line, sizeof(line), "Working... %us", sec);
        otaDrawInstallProgress(s_installTft, 0, 0, line);
        sparkyDisplayFlush(s_installTft);
        xSemaphoreGive(s_otaUiMutex);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(400));
  }
  s_otaPulseTaskDone = true;
  s_otaPulseHandle = nullptr;
  vTaskDelete(nullptr);
}

static void otaOnProgress(size_t cur, size_t total) {
  if (!s_installTft) return;
  int pct = -1;
  if (total > 0) pct = (int)((uint64_t)cur * 100 / (uint64_t)total);
  if (pct > 100) pct = 100;

  unsigned long ms = millis();
  if (pct >= 0 && pct < 100) {
    if (pct == s_otaProgressLastPct && (ms - s_otaProgressLastMs) < 200) return;
  } else if (total == 0 && cur > 0 && (ms - s_otaProgressLastMs) < 100) return;

  s_otaProgressLastPct = pct;
  s_otaProgressLastMs = ms;
  if (s_otaUiMutex) {
    if (xSemaphoreTake(s_otaUiMutex, pdMS_TO_TICKS(200)) != pdTRUE) return;
  }
  otaDrawInstallProgress(s_installTft, cur, total, nullptr);
  sparkyDisplayFlush(s_installTft);
  if (s_otaUiMutex) xSemaphoreGive(s_otaUiMutex);
}

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

/** Host part only of http(s)://host[:port]/path — for DNS preflight so we skip TLS when DNS is down (less log noise). */
static bool manifestHostFromUrl(const char* url, char* host, unsigned hostLen) {
  if (!url || !host || hostLen < 2) return false;
  host[0] = '\0';
  const char* p = strstr(url, "://");
  if (p)
    p += 3;
  else
    p = url;
  while (*p == '/') p++;
  unsigned i = 0;
  while (*p && *p != '/' && *p != '?' && *p != '#' && i + 1 < hostLen) {
    if (*p == ':') break; /* port */
    host[i++] = (char)*p++;
  }
  host[i] = '\0';
  return host[0] != '\0';
}

static bool dnsResolveManifest(const char* host) {
  IPAddress ip(0u, 0u, 0u, 0u);
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
  if (!WiFi.hostByName(host, ip)) return false;
#else
  if (WiFi.hostByName(host, ip) != 1) return false;
#endif
  return ip != IPAddress(0u, 0u, 0u, 0u);
}

static bool fetchManifestJson(char* jsonOut, unsigned jsonSize) {
  char url[OTA_URL_LEN];
  OtaUpdate_getManifestUrl(url, sizeof(url));
  if (!url[0]) {
    setStatus("OTA manifest URL is empty.");
    return false;
  }

  char host[96];
  if (!manifestHostFromUrl(url, host, sizeof(host))) {
    setStatus("OTA manifest URL has no host.");
    return false;
  }
  if (!dnsResolveManifest(host)) {
    setStatus("OTA skipped: cannot reach manifest host (check Wi‑Fi / DNS / internet).");
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

  /* GitHub release URLs respond with 302 to objects.githubusercontent.com; HTTPUpdate defaults to no redirects. */
  HTTPUpdate updater(120000);
  updater.rebootOnUpdate(true);
  updater.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
  if (s_pending.md5[0]) updater.setMD5sum(s_pending.md5);
#else
  if (s_pending.md5[0]) updater.setMD5(s_pending.md5);
#endif

  s_otaProgressLastPct = -1;
  s_otaProgressLastMs = 0;
  if (s_installTft) {
    otaUiMutexEnsure();
    s_otaPulseAbort = false;
    s_otaConnectStartMs = millis();
    s_otaPulseTaskDone = false;
    const BaseType_t pt = xTaskCreate(otaConnectingPulseTask, "ota_conn_ui", 8192, nullptr, 1, &s_otaPulseHandle);
    if (pt != pdPASS) {
      s_otaPulseTaskDone = true;
      s_otaPulseHandle = nullptr;
      otaDrawInstallProgress(s_installTft, 0, 0, nullptr);
      sparkyDisplayFlush(s_installTft);
    }
    updater.onStart([]() {
      s_otaPulseAbort = true;
      const unsigned long t0 = millis();
      while (!s_otaPulseTaskDone && (millis() - t0) < 5000UL) {
        vTaskDelay(pdMS_TO_TICKS(5));
      }
      if (!s_installTft) return;
      if (s_otaUiMutex && xSemaphoreTake(s_otaUiMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        otaDrawInstallProgress(s_installTft, 0, 0, "Writing to flash...");
        sparkyDisplayFlush(s_installTft);
        xSemaphoreGive(s_otaUiMutex);
      }
    });
    updater.onProgress([](size_t c, size_t t) { otaOnProgress(c, t); });
  }

  t_httpUpdate_return ret = updater.update(client, s_pending.firmwareUrl, OtaUpdate_getCurrentVersion());

  if (s_installTft) otaPulseStopAndJoin();

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
  if (!WifiManager_isConnected()) return;
  OtaCheckStatus st = OtaUpdate_checkNow();
  if (st != OTA_CHECK_UPDATE_AVAILABLE) return;

  bool doInstall = AppState_getOtaAutoInstallEnabled() || s_pending.force;
  if (doInstall) OtaUpdate_installPending();
}

void OtaUpdate_autoFlowTask(void* arg) {
  (void)arg;
  OtaUpdate_runAutoFlow();
  vTaskDelete(nullptr);
}
