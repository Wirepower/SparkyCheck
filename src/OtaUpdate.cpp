#include "OtaUpdate.h"
#include "AppState.h"
#include "WifiManager.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <freertos/FreeRTOS.h>
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

/* HTTPUpdate callbacks run on the WiFi stack (often not core 1). TFT must only refresh from the
 * install wait loop on the same core as Arduino loop (typically APP CPU 1) to avoid INT WDT. */
enum { OTA_PHASE_CONNECT = 0, OTA_PHASE_DOWNLOAD = 1, OTA_PHASE_WRITE = 2 };
static SemaphoreHandle_t s_otaUiMutex = nullptr;
static SemaphoreHandle_t s_otaWorkerDoneSem = nullptr;
static volatile uint8_t s_otaUiPhase = OTA_PHASE_CONNECT;
static volatile size_t s_otaUiCur = 0;
static volatile size_t s_otaUiTotal = 0;
static volatile bool s_otaInstallInProgress = false;
static t_httpUpdate_return s_otaWorkerRet = HTTP_UPDATE_FAILED;
static char s_otaWorkerErrBuf[OTA_STATUS_LEN];
static uint8_t s_otaPollLastPhase = 0xFF;
static bool s_installOfferPending = false;

static void otaEnsureInstallSyncPrimitives(void) {
  if (!s_otaUiMutex) s_otaUiMutex = xSemaphoreCreateMutex();
  if (!s_otaWorkerDoneSem) s_otaWorkerDoneSem = xSemaphoreCreateBinary();
}

static void otaWorkerOnStart(void) {
  if (!s_otaUiMutex) return;
  if (xSemaphoreTake(s_otaUiMutex, pdMS_TO_TICKS(500)) != pdTRUE) return;
  s_otaUiPhase = OTA_PHASE_WRITE;
  xSemaphoreGive(s_otaUiMutex);
}

static void otaWorkerOnProgress(size_t cur, size_t total) {
  if (!s_otaUiMutex) return;
  if (xSemaphoreTake(s_otaUiMutex, pdMS_TO_TICKS(200)) != pdTRUE) return;
  s_otaUiCur = cur;
  s_otaUiTotal = total;
  s_otaUiPhase = OTA_PHASE_DOWNLOAD;
  xSemaphoreGive(s_otaUiMutex);
}

void OtaUpdate_setInstallDisplay(SparkyTft* tft) {
  s_installTft = tft;
}

bool OtaUpdate_isInstallOfferPending(void) {
  return s_installOfferPending;
}

void OtaUpdate_dismissInstallOffer(void) {
  s_installOfferPending = false;
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

/** Call only from the task that owns the display (loop / install wait loop on APP CPU). */
static void otaPumpInstallUiOnce(SparkyTft* tft) {
  if (!s_otaInstallInProgress || !tft || !s_otaUiMutex) return;

  size_t cur = 0, total = 0;
  uint8_t phase = OTA_PHASE_CONNECT;
  if (xSemaphoreTake(s_otaUiMutex, 0) != pdTRUE) return;
  cur = s_otaUiCur;
  total = s_otaUiTotal;
  phase = s_otaUiPhase;
  xSemaphoreGive(s_otaUiMutex);

  unsigned long ms = millis();
  const bool phaseChanged = (phase != s_otaPollLastPhase);

  int pct = -1;
  if (total > 0) pct = (int)((uint64_t)cur * (uint64_t)100 / (uint64_t)total);
  if (pct > 100) pct = 100;

  if (!phaseChanged) {
    if (phase == OTA_PHASE_CONNECT && (ms - s_otaProgressLastMs) < 500) return;
    if (phase == OTA_PHASE_DOWNLOAD) {
      if (pct >= 0 && pct < 100) {
        if (pct == s_otaProgressLastPct && (ms - s_otaProgressLastMs) < 200) return;
      } else if (total == 0 && cur > 0 && (ms - s_otaProgressLastMs) < 100) {
        return;
      }
    }
    if (phase == OTA_PHASE_WRITE && (ms - s_otaProgressLastMs) < 100) return;
  }

  s_otaPollLastPhase = phase;
  s_otaProgressLastPct = pct;
  s_otaProgressLastMs = ms;

  const char* ov = (phase == OTA_PHASE_WRITE) ? "Writing to flash..." : nullptr;
  otaDrawInstallProgress(tft, cur, total, ov);
  sparkyDisplayFlush(tft);
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

static void otaInstallWorker(void* arg) {
  (void)arg;
  WiFiClientSecure client;
  configureSecureClient(client);

  HTTPUpdate updater(120000);
  updater.rebootOnUpdate(true);
  updater.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
  if (s_pending.md5[0]) updater.setMD5sum(s_pending.md5);
#else
  if (s_pending.md5[0]) updater.setMD5(s_pending.md5);
#endif

  updater.onStart([]() { otaWorkerOnStart(); });
  updater.onProgress([](size_t c, size_t t) { otaWorkerOnProgress(c, t); });

  /* Let AsyncTCP / admin sockets settle; mbedTLS needs a large contiguous block (see SSL -32512). */
  vTaskDelay(pdMS_TO_TICKS(400));

  s_otaWorkerRet = updater.update(client, s_pending.firmwareUrl, OtaUpdate_getCurrentVersion());
  if (s_otaWorkerRet != HTTP_UPDATE_OK) {
    strCopy(s_otaWorkerErrBuf, sizeof(s_otaWorkerErrBuf), updater.getLastErrorString().c_str());
  } else {
    s_otaWorkerErrBuf[0] = '\0';
  }

  s_otaInstallInProgress = false;
  xSemaphoreGive(s_otaWorkerDoneSem);
  vTaskDelete(nullptr);
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
  s_installOfferPending = false;

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

  s_installOfferPending = false;

  setStatus("Installing update...");
  otaEnsureInstallSyncPrimitives();
  if (!s_otaUiMutex || !s_otaWorkerDoneSem) {
    setStatus("Install failed: OTA sync init.");
    return false;
  }

  s_otaUiPhase = OTA_PHASE_CONNECT;
  s_otaUiCur = 0;
  s_otaUiTotal = 0;
  s_otaProgressLastPct = -1;
  s_otaProgressLastMs = 0;
  s_otaInstallInProgress = true;

  if (s_installTft) {
    otaDrawInstallProgress(s_installTft, 0, 0, nullptr);
    sparkyDisplayFlush(s_installTft);
    s_otaProgressLastMs = millis();
  }
  s_otaPollLastPhase = OTA_PHASE_CONNECT;

#if defined(CONFIG_FREERTOS_UNICORE) && CONFIG_FREERTOS_UNICORE
  const BaseType_t created = xTaskCreate(otaInstallWorker, "ota_inst", 24576, nullptr, 3, nullptr);
#else
  const BaseType_t created =
      xTaskCreatePinnedToCore(otaInstallWorker, "ota_inst", 24576, nullptr, 3, nullptr, 0);
#endif
  if (created != pdPASS) {
    s_otaInstallInProgress = false;
    setStatus("Install failed: OTA worker task.");
    return false;
  }

  for (;;) {
    if (s_installTft) otaPumpInstallUiOnce(s_installTft);
    if (xSemaphoreTake(s_otaWorkerDoneSem, pdMS_TO_TICKS(50)) == pdTRUE) break;
  }

  const t_httpUpdate_return ret = s_otaWorkerRet;

  if (ret == HTTP_UPDATE_OK) {
    setStatus("Update installed. Rebooting...");
    return true;
  }
  if (ret == HTTP_UPDATE_NO_UPDATES) {
    setStatus("Server reported no update.");
    return false;
  }
  char msg[OTA_STATUS_LEN];
  if (s_otaWorkerErrBuf[0])
    snprintf(msg, sizeof(msg), "Install failed: %s", s_otaWorkerErrBuf);
  else
    snprintf(msg, sizeof(msg), "Install failed.");
  setStatus(msg);
  return false;
}

void OtaUpdate_runAutoFlow(void) {
  if (!AppState_getOtaAutoCheckEnabled()) return;
  if (!WifiManager_isConnected()) return;
  OtaCheckStatus st = OtaUpdate_checkNow();
  if (st != OTA_CHECK_UPDATE_AVAILABLE) return;

  /* Never block the device with a full-screen install from the background task. */
  s_installOfferPending = true;
  setStatus("Update available — Firmware updates: Yes to install or Not now.");
}

void OtaUpdate_autoFlowTask(void* arg) {
  (void)arg;
  OtaUpdate_runAutoFlow();
  vTaskDelete(nullptr);
}
