#include "AdminPortal.h"
#include "AppState.h"
#include "WifiManager.h"
#include "VerificationSteps.h"
#include "BatteryStatus.h"
#include "TestLimits.h"
#include "EmailTest.h"
#include "SparkyRtc.h"
#include "SparkyTime.h"
#include "OtaUpdate.h"
#include "SparkyNtp.h"
#include "SparkyTzPresets.h"
#include "Standards.h"

#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <esp_system.h>
#include <esp_heap_caps.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "boot_logo_web_embedded.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <memory>
#include <atomic>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

static AsyncWebServer s_server(80);
static bool s_started = false;
static bool s_authed = false;
static uint32_t s_sessionToken = 0;
static unsigned long s_sessionExpiresMs = 0;
static unsigned long s_nextPortalTickMs = 0;
static bool s_apActive = false;
static std::atomic<bool> s_emailTestPending{false};
static std::atomic<bool> s_emailTestRunning{false};
static std::atomic<int> s_otaHttpPauseDepth{0};
/** AsyncWebServer::begin() after OTA must run from the Arduino loop task; worker-thread begin() can leave :80 dead. */
static std::atomic<bool> s_httpResumePending{false};
/** WiFi reconnect from admin runs off loopTask; rebind :80 on next tick when OTA is not holding the server down. */
static std::atomic<bool> s_pendingHttpRebind{false};
/** Worker-thread pause must not call s_server.end() off loopTask; loop runs end() and signals this sem. */
static std::atomic<bool> s_httpPausePending{false};
static SemaphoreHandle_t s_otaPauseDoneSem = nullptr;

static bool arduinoOnLoopTask(void) {
  TaskHandle_t loop = xTaskGetHandle("loopTask");
  if (!loop) return true;
  return xTaskGetCurrentTaskHandle() == loop;
}
/** Install must run on loop task (same as device UI); set from POST handler, run in AdminPortal_tick. */
static std::atomic<bool> s_webOtaInstallPending{false};
static uint32_t s_emailTestActiveJobId = 0;
static unsigned long s_emailTestStartMs = 0;
static char s_emailTestStatus[180] = "Idle";
static char s_apSsid[32] = "";
static char s_apPass[32] = "";
static char s_flashMsg[120] = "";
static bool s_flashErr = false;
static WifiNetwork s_scanResults[WIFI_MAX_SSIDS];
static int s_scanCount = 0;
/* Full tests.json (many tests/steps) exceeds 128 KiB; keep in PSRAM. */
static constexpr size_t kTestsJsonCap = 524288;
/* ArduinoJson pool must hold nested tree — typically > raw file size for large configs. */
static constexpr size_t kTestsJsonDocCap = 720896;
static char* s_testsJson = nullptr;

static bool ensureTestsJsonBuf(void) {
  if (s_testsJson) return true;
  s_testsJson = (char*)heap_caps_calloc(1, kTestsJsonCap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!s_testsJson) s_testsJson = (char*)heap_caps_calloc(1, kTestsJsonCap, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!s_testsJson) s_testsJson = (char*)calloc(1, kTestsJsonCap);
  if (s_testsJson) s_testsJson[0] = '\0';
  return s_testsJson != nullptr;
}
static String s_uploadTestsJson;
static const char* kTestsPath = "/config/tests.json";
static const char* kTestsPrevPath = "/config/tests_prev.json";
static const char kWebLogoBmpPath[] = "/config/web_logo.bmp";
static bool buildTestsJsonFromActive(char* out, unsigned outSize);
static void upsertRuleForKind(DynamicJsonDocument& doc, const char* kind, const char* op, float val, float valMax);
static bool ensureDefaultRulesInDoc(DynamicJsonDocument& doc);
static bool writeWebLogoBmpToFile(void);
static void handleTestsImportUploadDone(AsyncWebServerRequest* req);
static void handleTestsImportUploadChunk(AsyncWebServerRequest* req, const String& filename, size_t index, uint8_t* data,
                                         size_t len, bool final);

static const unsigned long kSessionTtlMs = 120UL * 60UL * 1000UL; /* 2 h; PIN pages stay usable while configuring */
static const unsigned long kPortalTickMs = 5000UL;

static void tryStartServer(void) {
  /* Start listening before heavy LittleFS/tests activation so AsyncTCP can allocate its task. */
  Serial.printf("[Admin] begin web server (heap=%u)\n", (unsigned)ESP.getFreeHeap());
  s_server.begin();
  Serial.println("[Admin] web server listen on :80");
}

/** WiFi.mode(AP_STA) / softAP changes can drop :80 binding; end+begin from loop task (see OTA pause comment). */
static void adminPortalRebindHttpServer(void) {
  s_server.end();
  delay(80);
  tryStartServer();
}
static const char* kCss =
  "body{font-family:Arial,sans-serif;background:#102030;color:#fff;margin:0;padding:16px;}"
  ".brand{position:fixed;top:10px;right:12px;text-align:center;z-index:2;pointer-events:auto;}"
  ".brand canvas{width:120px;height:auto;border:1px solid #8ecae6;border-radius:8px;background:#0b162a;display:none;}"
  ".brand .fallback{width:120px;height:34px;border:1px solid #8ecae6;border-radius:8px;background:#0b162a;color:#93c5fd;display:flex;align-items:center;justify-content:center;font-size:12px;margin-top:4px;}"
  ".brand .meta{font-size:11px;color:#cbd5e1;margin-top:4px;line-height:1.25;}"
  ".wrap{margin-right:140px;}"
  ".card{background:#1d3557;border:1px solid #8ecae6;border-radius:10px;padding:14px;margin:10px 0;}"
  "h1,h2{margin:6px 0 10px 0;}label{display:block;margin-top:8px;font-size:14px;color:#dbeafe;}"
  "input,select{width:100%;padding:8px;border-radius:6px;border:1px solid #8ecae6;background:#0f172a;color:#fff;}"
  ".row{display:grid;grid-template-columns:1fr 1fr;gap:10px;}"
  ".btn{margin-top:10px;background:#22c55e;color:#fff;padding:9px 12px;border-radius:8px;border:none;cursor:pointer;font-weight:700;}"
  ".btn2{background:#334155;color:#fff;}.small{font-size:12px;color:#cbd5e1;}";

static bool isSessionValid(void) {
  if (!s_authed) return false;
  if ((long)(millis() - s_sessionExpiresMs) > 0) {
    s_authed = false;
    return false;
  }
  return true;
}

static void startFallbackAp(void) {
  if (s_apActive) return;
  char cfgSsid[APP_STATE_ADMIN_AP_SSID_LEN] = "";
  char cfgPass[APP_STATE_ADMIN_AP_PASS_LEN] = "";
  AppState_getAdminApCredentials(cfgSsid, sizeof(cfgSsid), cfgPass, sizeof(cfgPass));
  uint32_t chip = (uint32_t)(ESP.getEfuseMac() & 0xFFFFFF);
  if (cfgSsid[0]) {
    strncpy(s_apSsid, cfgSsid, sizeof(s_apSsid) - 1);
    s_apSsid[sizeof(s_apSsid) - 1] = '\0';
  } else {
    strncpy(s_apSsid, "SparkyCheck", sizeof(s_apSsid) - 1);
    s_apSsid[sizeof(s_apSsid) - 1] = '\0';
  }
  if (cfgPass[0] && strlen(cfgPass) >= 8) {
    strncpy(s_apPass, cfgPass, sizeof(s_apPass) - 1);
    s_apPass[sizeof(s_apPass) - 1] = '\0';
  } else {
    strncpy(s_apPass, "12345678", sizeof(s_apPass) - 1);
    s_apPass[sizeof(s_apPass) - 1] = '\0';
  }
  WiFi.mode(WIFI_AP_STA);
  if (WiFi.softAP(s_apSsid, s_apPass)) {
    s_apActive = true;
    adminPortalRebindHttpServer();
  }
}

static void stopFallbackAp(void) {
  if (!s_apActive) return;
  WiFi.softAPdisconnect(true);
  s_apActive = false;
  s_apSsid[0] = '\0';
  s_apPass[0] = '\0';
  WiFi.mode(WIFI_STA);
  adminPortalRebindHttpServer();
}

static void touchSession(void) {
  s_sessionExpiresMs = millis() + kSessionTtlMs;
}

static bool checkCookieToken(AsyncWebServerRequest* req) {
  if (!req || !isSessionValid()) return false;
  if (!req->hasHeader("Cookie")) return false;
  String cookie = req->header("Cookie");
  char expect[48];
  snprintf(expect, sizeof(expect), "sparky_sid=%lu", (unsigned long)s_sessionToken);
  return cookie.indexOf(expect) >= 0;
}

static bool isAuthorized(AsyncWebServerRequest* req) {
  bool ok = checkCookieToken(req);
  if (ok) touchSession();
  return ok;
}

static String esc(const char* s) {
  if (!s) return String();
  String in(s), out;
  out.reserve(in.length() + 16);
  for (size_t i = 0; i < in.length(); i++) {
    char c = in[i];
    if (c == '&') out += "&amp;";
    else if (c == '<') out += "&lt;";
    else if (c == '>') out += "&gt;";
    else if (c == '"') out += "&quot;";
    else out += c;
  }
  return out;
}

static String webFriendlyTestName(const char* name) {
  String n = name ? String(name) : String("");
  n.replace("SWP D/R", "SWP Disconnect/Reconnect");
  return n;
}

static String htmlPage(const String& title, const String& body) {
  String h;
  h.reserve(7200u + body.length());
  h += "<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>";
  h += "<title>";
  h += title;
  h += "</title><style>";
  h += kCss;
  h += "</style></head><body><div class='brand'><img src='/admin/logo.bmp' alt='SparkyCheck logo' style='width:120px;height:auto;border:1px solid #8ecae6;border-radius:8px;background:#0b162a;display:block'/><div class='meta' id='brandMeta'>Firmware <span style='color:#93c5fd'>";
  {
    const char* fwv = OtaUpdate_getCurrentVersion();
    h += esc(fwv && fwv[0] ? fwv : "?");
  }
  h += "</span><br/>Created by Frank Offer 2026<br/>SparkyCheck Creator<br/><a href='/admin/logo-info' style='color:#93c5fd'>Logo info</a></div></div><div class='wrap'>";
  h += body;
  h += "</div>";
  h += "</body></html>";
  return h;
}

static void sendHtmlNoCache(AsyncWebServerRequest* req, const String& html) {
  AsyncWebServerResponse* resp = req->beginResponse(200, "text/html", html);
  resp->addHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  resp->addHeader("Pragma", "no-cache");
  resp->addHeader("Expires", "0");
  req->send(resp);
}

static void logoEffectiveDims(int* outW, int* outH, size_t* outWords) {
  const size_t words = sizeof(kBootLogoRgb565) / sizeof(kBootLogoRgb565[0]);
  int w = BOOT_LOGO_W;
  int h = BOOT_LOGO_H;
  if ((size_t)(w * h) != words) {
    /* Header mismatch guard: keep payload and dimensions consistent. */
    w = 120;
    h = (int)(words / (size_t)w);
    if (h <= 0) {
      w = (int)words;
      if (w <= 0) w = 1;
      h = 1;
    }
  }
  if (outW) *outW = w;
  if (outH) *outH = h;
  if (outWords) *outWords = words;
}

/* Serve logo from LittleFS: AsyncResponseStream/String paths mishandle embedded NULs in BMP data. */
static bool writeWebLogoBmpToFile(void) {
  int srcW = 0, srcH = 0;
  size_t srcWords = 0;
  logoEffectiveDims(&srcW, &srcH, &srcWords);
  const int outW = 120;
  const int outH = (srcH * outW) / (srcW > 0 ? srcW : 120);
  const int rowBytes = outW * 3;
  const int pad = (4 - (rowBytes % 4)) % 4;
  const int imgBytes = (rowBytes + pad) * outH;
  const int fileSize = 54 + imgBytes;

  if (!LittleFS.exists("/config")) LittleFS.mkdir("/config");
  File f = LittleFS.open(kWebLogoBmpPath, "w");
  if (!f) return false;

  uint8_t hdr[54] = {0};
  hdr[0] = 'B'; hdr[1] = 'M';
  hdr[2] = (uint8_t)(fileSize & 0xFF); hdr[3] = (uint8_t)((fileSize >> 8) & 0xFF);
  hdr[4] = (uint8_t)((fileSize >> 16) & 0xFF); hdr[5] = (uint8_t)((fileSize >> 24) & 0xFF);
  hdr[10] = 54;
  hdr[14] = 40;
  hdr[18] = (uint8_t)(outW & 0xFF); hdr[19] = (uint8_t)((outW >> 8) & 0xFF);
  hdr[22] = (uint8_t)(outH & 0xFF); hdr[23] = (uint8_t)((outH >> 8) & 0xFF);
  hdr[26] = 1; hdr[28] = 24;
  hdr[34] = (uint8_t)(imgBytes & 0xFF); hdr[35] = (uint8_t)((imgBytes >> 8) & 0xFF);
  hdr[36] = (uint8_t)((imgBytes >> 16) & 0xFF); hdr[37] = (uint8_t)((imgBytes >> 24) & 0xFF);
  if (f.write(hdr, sizeof(hdr)) != (int)sizeof(hdr)) {
    f.close();
    return false;
  }

  uint8_t pix[3];
  for (int y = outH - 1; y >= 0; y--) {
    int sy = (y * srcH) / outH;
    for (int x = 0; x < outW; x++) {
      int sx = (x * srcW) / outW;
      size_t idx = (size_t)sy * (size_t)srcW + (size_t)sx;
      if (idx >= srcWords) idx = srcWords ? (srcWords - 1) : 0;
      uint16_t c = (uint16_t)pgm_read_word(&kBootLogoRgb565[idx]);
      uint8_t r5 = (uint8_t)((c >> 11) & 0x1F);
      uint8_t g6 = (uint8_t)((c >> 5) & 0x3F);
      uint8_t b5 = (uint8_t)(c & 0x1F);
      uint8_t r = (uint8_t)((r5 * 255) / 31);
      uint8_t g = (uint8_t)((g6 * 255) / 63);
      uint8_t b = (uint8_t)((b5 * 255) / 31);
      pix[0] = b; pix[1] = g; pix[2] = r;
      if (f.write(pix, 3) != 3) {
        f.close();
        return false;
      }
    }
    for (int p = 0; p < pad; p++) {
      if (f.write((uint8_t)0) != 1) {
        f.close();
        return false;
      }
    }
  }
  f.close();
  return true;
}

static void streamBootLogoBmp(AsyncWebServerRequest* req) {
  if (!LittleFS.exists(kWebLogoBmpPath) && !writeWebLogoBmpToFile()) {
    req->send(500, "text/plain", "Logo file write failed");
    return;
  }
  AsyncWebServerResponse* resp = req->beginResponse(LittleFS, kWebLogoBmpPath, "image/bmp", false);
  resp->addHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  req->send(resp);
}

static void streamBootLogo565(AsyncWebServerRequest* req) {
  int w = 0, h = 0;
  size_t words = 0;
  logoEffectiveDims(&w, &h, &words);
  const size_t bytes = words * sizeof(uint16_t);
  AsyncWebServerResponse* resp = req->beginResponse(200, "application/octet-stream", (const uint8_t*)kBootLogoRgb565, bytes);
  resp->addHeader("Cache-Control", "no-store");
  resp->addHeader("X-Logo-W", String(w));
  resp->addHeader("X-Logo-H", String(h));
  req->send(resp);
}

static String loginPage(const char* msg) {
  String b;
  b += "<h1>SparkyCheck Admin</h1><div class='card'><p>Enter PIN (same as device settings PIN).</p>";
  if (msg && msg[0]) {
    b += "<p style='color:#fca5a5;'>";
    b += esc(msg);
    b += "</p>";
  }
  b += "<form method='post' action='/admin/login'><label>PIN</label><input type='password' name='pin' maxlength='10' />";
  b += "<button class='btn' type='submit'>Login</button></form></div>";
  return htmlPage("SparkyCheck Admin Login", b);
}

static String boolSelected(bool v) { return v ? " selected" : ""; }

static String settingsPage(void) {
  const bool fieldMode = AppState_isFieldMode();
  char smtpServer[APP_STATE_EMAIL_STR_LEN] = "", smtpPort[APP_STATE_EMAIL_STR_LEN] = "";
  char smtpUser[APP_STATE_EMAIL_STR_LEN] = "", smtpPass[APP_STATE_EMAIL_STR_LEN] = "", reportTo[APP_STATE_EMAIL_STR_LEN] = "";
  char endpoint[APP_STATE_TRAINING_SYNC_URL_LEN] = "", token[APP_STATE_TRAINING_SYNC_TOKEN_LEN] = "";
  char cubicle[APP_STATE_TRAINING_SYNC_CUBICLE_LEN] = "", devId[APP_STATE_DEVICE_ID_LEN] = "";
  char adminApSsid[APP_STATE_ADMIN_AP_SSID_LEN] = "", adminApPass[APP_STATE_ADMIN_AP_PASS_LEN] = "";
  char otaManifestUrl[APP_STATE_OTA_URL_LEN] = "";
  char ssid[WIFI_SSID_LEN] = "", ip[20] = "";
  String wifiMac = WiFi.macAddress();
  if (!wifiMac.length()) wifiMac = "Unavailable";
  AppState_getSmtpServer(smtpServer, sizeof(smtpServer));
  AppState_getSmtpPort(smtpPort, sizeof(smtpPort));
  AppState_getSmtpUser(smtpUser, sizeof(smtpUser));
  AppState_getSmtpPass(smtpPass, sizeof(smtpPass));
  AppState_getReportToEmail(reportTo, sizeof(reportTo));
  AppState_getTrainingSyncEndpoint(endpoint, sizeof(endpoint));
  AppState_getTrainingSyncToken(token, sizeof(token));
  AppState_getTrainingSyncCubicleId(cubicle, sizeof(cubicle));
  AppState_getDeviceIdOverride(devId, sizeof(devId));
  AppState_getAdminApCredentials(adminApSsid, sizeof(adminApSsid), adminApPass, sizeof(adminApPass));
  AppState_getOtaManifestUrl(otaManifestUrl, sizeof(otaManifestUrl));
  WifiManager_getConnectedSsid(ssid, sizeof(ssid));
  WifiManager_getIpString(ip, sizeof(ip));
  if (s_apActive) {
    if (s_apSsid[0]) {
      strncpy(adminApSsid, s_apSsid, sizeof(adminApSsid) - 1);
      adminApSsid[sizeof(adminApSsid) - 1] = '\0';
    }
    if (s_apPass[0]) {
      strncpy(adminApPass, s_apPass, sizeof(adminApPass) - 1);
      adminApPass[sizeof(adminApPass) - 1] = '\0';
    }
  }

  String b;
  b.reserve(11000);
  b += "<h1>SparkyCheck Admin</h1>";
  {
    char rulesTag[40];
    Standards_getRulesVersion(rulesTag, sizeof(rulesTag));
    b += "<p class='small'>Firmware <strong>";
    b += esc(OtaUpdate_getCurrentVersion());
    b += "</strong> · embedded rules <strong>";
    b += esc(rulesTag);
    b += "</strong></p>";
  }
  if (s_flashMsg[0]) {
    b += "<div class='card' style='border-color:";
    b += s_flashErr ? "#f87171" : "#4ade80";
    b += ";'><p style='margin:0;'>";
    b += esc(s_flashMsg);
    b += "</p></div>";
    s_flashMsg[0] = '\0';
    s_flashErr = false;
  }
  b += "<p class='small'>Connected SSID: ";
  b += esc(ssid[0] ? ssid : "(not connected)");
  b += " | IP: ";
  b += esc(ip[0] ? ip : "(n/a)");
  int batteryPct = 0;
  if (BatteryStatus_getPercent(&batteryPct)) {
    b += " | Battery: ";
    b += String(batteryPct);
    b += "%";
  } else {
    b += " | Battery: (not configured)";
  }
  if (s_apActive) {
    b += " | Hotspot: ";
    b += esc(s_apSsid);
  }
  b += "</p><div style='display:flex;gap:10px;align-items:center;flex-wrap:wrap'>";
  b += "<a href='/admin/logout' style='color:#93c5fd'>Logout</a>";
  b += "<a href='/admin/files' style='color:#93c5fd'>Browse & download reports</a>";
  b += "<form method='get' action='/admin/tests-page' style='margin:0'><button class='btn' style='margin-top:0' type='submit' onclick='window.location.href=\"/admin/tests-page\";return false;'>Edit tests / questions</button></form>";
  b += "</div>";

  b += "<div class='card'><h2>Wi-Fi Connection</h2>";
  b += "<p class='small'>Works in hotspot fallback mode too. Scan for SSIDs below or enter one manually.</p>";
  b += "<form method='post' action='/admin/wifi/scan'><button class='btn btn2' type='submit'>Scan Wi-Fi</button></form>";
  b += "<form method='post' action='/admin/wifi/disconnect'><button class='btn btn2' type='submit'>Disconnect Wi-Fi</button></form>";
  if (s_scanCount > 0) {
    b += "<p class='small'>Scan results:</p>";
    b += "<table style='width:100%;border-collapse:collapse;'><tr><th style='text-align:left'>SSID</th><th>RSSI</th><th>Connect</th></tr>";
    for (int i = 0; i < s_scanCount; i++) {
      b += "<tr><td>";
      b += esc(s_scanResults[i].ssid);
      b += "</td><td style='text-align:right'>";
      b += String((int)s_scanResults[i].rssi);
      b += " dBm</td><td>";
      b += "<form method='post' action='/admin/wifi/connect' style='display:flex;gap:6px;align-items:center;'>";
      b += "<input type='hidden' name='ssid' value='" + esc(s_scanResults[i].ssid) + "'/>";
      b += "<input type='password' name='pass' placeholder='password (blank=open)'/>";
      b += "<button class='btn' style='margin-top:0' type='submit'>Connect</button></form></td></tr>";
    }
    b += "</table>";
  }
  b += "<p class='small' style='margin-top:10px'>Manual connect:</p>";
  b += "<form method='post' action='/admin/wifi/connect'>";
  b += "<div class='row'><div><label>SSID</label><input name='ssid' value=''/></div>";
  b += "<div><label>Password</label><input type='password' name='pass' value=''/></div></div>";
  b += "<button class='btn' type='submit'>Connect Manually</button></form></div>";

  b += "<div class='card mode-both'><h2>Firmware updates (OTA)</h2>";
  b += "<p class='small'>Uses the manifest URL below (HTTPS JSON). Device must be on Wi-Fi.</p>";
  b += "<p class='small' style='color:#fecaca'>During install the display may <strong>flicker or flash</strong> — this is normal and does not mean the update failed. The device <strong>reboots</strong> when the download finishes.</p>";
  b += "<form method='post' action='/admin/save'><input type='hidden' name='section' value='ota'/>";
  b += "<label>Manifest URL</label><input name='ota_manifest_url' value='" + esc(otaManifestUrl) + "' placeholder='https://.../manifest-stable.json'/>";
  b += "<p class='small'>Leave blank to use the firmware default from build, if any.</p>";
  b += "<label>Auto-check for updates</label><select name='ota_auto_check'><option value='1'";
  b += boolSelected(AppState_getOtaAutoCheckEnabled());
  b += ">On</option><option value='0'";
  b += boolSelected(!AppState_getOtaAutoCheckEnabled());
  b += ">Off</option></select>";
  b += "<p class='small'>When on, the device checks the manifest shortly after boot (Wi‑Fi required). You still install from this page or the device.</p>";
  b += "<button class='btn btn2' type='submit'>Save OTA settings</button></form>";
  b += "<form method='post' action='/admin/ota/check' style='margin-top:10px;display:inline-block;margin-right:8px'><button class='btn' type='submit'>Check for updates</button></form>";
  b += "<form method='post' action='/admin/ota/install' style='margin-top:10px;display:inline-block' onsubmit=\"return confirm('Install the pending firmware now?\\n\\nThe screen may flicker during the update — this is normal.\\nThe device will reboot when finished.');\"><button class='btn' type='submit' style='background:#b45309'";
  if (!OtaUpdate_hasPendingUpdate()) b += " disabled title='Run Check first when an update is available'";
  b += ">Install pending update</button></form>";
  b += "<p class='small' id='otaWebStatusLine'>OTA status: (loading...)</p>";
  b += "<script>(async function(){const el=document.getElementById('otaWebStatusLine');if(!el)return;async function poll(){try{const r=await fetch('/admin/ota-status',{cache:'no-store',credentials:'same-origin'});if(!r.ok)return;const j=await r.json();let t='OTA status: '+(j.last_status||'');if(j.check_busy)t+=' [checking...]';if(j.has_pending)t+=' - Pending v'+(j.pending_version||'');el.textContent=t;}catch(e){el.textContent='OTA status: (poll error)';}}poll();setInterval(poll,2500);})();</script>";
  b += "</div>";

  b += "<div class='card mode-both'><h2>Device Identification</h2>";
  b += "<p class='small'>Used across reports, email subjects/body, and cloud records in both Field and Training modes.</p>";
  b += "<form method='post' action='/admin/save'><input type='hidden' name='section' value='identity'/>";
  b += "<div class='row'><div><label>Cubicle ID</label><input name='sync_cubicle' value='" + esc(cubicle) + "'/><p class='small'>Example: CUB-07</p></div>";
  b += "<div><label>Device ID</label><input name='device_id' value='" + esc(devId) + "'/><p class='small'>Example: S1024</p></div></div>";
  b += "<label>Wi-Fi MAC Address (auto)</label><input value='" + esc(wifiMac.c_str()) + "' readonly/>";
  b += "<p class='small'>Auto-detected hardware MAC. Read-only for traceability.</p>";
  b += "<button class='btn' type='submit'>Save Device Identification</button></form></div>";

  b += "<div class='card'><h2>Mode</h2>";
  b += "<p class='small'>Mode controls which settings are shown below and how report-recipient labels are interpreted.</p>";
  b += "<p class='small'>Training: teacher-oriented + training sync controls. Field: recipient-focused + essential settings only.</p>";
  b += "<form id='modeForm' method='post' action='/admin/save'><input type='hidden' name='section' value='mode'/>";
  b += "<label>Current mode</label><select id='modeSelect' name='mode'><option value='0'";
  b += boolSelected(AppState_getMode() == APP_MODE_TRAINING);
  b += ">Training</option><option value='1'";
  b += boolSelected(AppState_getMode() == APP_MODE_FIELD);
  b += ">Field</option></select><p class='small' id='modeSaveHint'>Auto-saves when changed.</p></form></div>";

  {
    char nowFmt[56];
    SparkyTime_formatPreferred(nowFmt, sizeof(nowFmt));
    time_t nowt = time(nullptr);
    time_t wall = SparkyTime_utcToWallTime(nowt);
    struct tm lt;
    int df = 1, mf = 1, yf = 2026, hf = 0, minf = 0, sf = 0;
    if (gmtime_r(&wall, &lt)) {
      df = lt.tm_mday;
      mf = lt.tm_mon + 1;
      yf = lt.tm_year + 1900;
      hf = lt.tm_hour;
      minf = lt.tm_min;
      sf = lt.tm_sec;
    }
    b += "<div class='card mode-both'><h2>Date &amp; time</h2>";
    b += "<p class='small'>Uses the PCF85063 RTC when present (I2C on GPIO8/9). RTC_INT is GPIO6 on the schematic; this firmware reads the chip over I2C only.</p>";
    b += "<p class='small'>Current device time: <strong>";
    b += esc(nowFmt);
    b += "</strong></p>";
    b += "<form method='post' action='/admin/save'><input type='hidden' name='section' value='clock'/>";
    b += "<div class='row'><div><label>Day (dd)</label><input type='number' name='clock_day' min='1' max='31' value='";
    b += String(df);
    b += "'/></div><div><label>Month (mm)</label><input type='number' name='clock_month' min='1' max='12' value='";
    b += String(mf);
    b += "'/></div><div><label>Year (yyyy)</label><input type='number' name='clock_year' min='2000' max='2099' value='";
    b += String(yf);
    b += "'/></div></div>";
    b += "<div class='row'><div><label>Hour (0–23)</label><input type='number' name='clock_hour' min='0' max='23' value='";
    b += String(hf);
    b += "'/></div><div><label>Minute</label><input type='number' name='clock_min' min='0' max='59' value='";
    b += String(minf);
    b += "'/></div><div><label>Second</label><input type='number' name='clock_sec' min='0' max='59' value='";
    b += String(sf);
    b += "'/></div></div>";
    b += "<label>12-hour clock (AM/PM in timestamps)</label><select name='clock_12'><option value='1'";
    b += boolSelected(AppState_getClock12Hour());
    b += ">On</option><option value='0'";
    b += boolSelected(!AppState_getClock12Hour());
    b += ">Off (24-hour)</option></select>";
    {
      const unsigned tzSel = SparkyTzPresets_indexForOffset(AppState_getClockTzOffsetMinutes());
      const bool customTz = (tzSel >= SparkyTzPresets_count());
      b += "<label style='margin-top:10px;display:block'>Timezone</label><select name='clock_tz_preset'>";
      b += "<option value='custom'";
      if (customTz) b += " selected";
      {
        char oth[72];
        if (customTz) {
          char curUtc[24];
          SparkyTzPresets_formatUtcOffset(AppState_getClockTzOffsetMinutes(), curUtc, sizeof(curUtc));
          snprintf(oth, sizeof(oth), "Other (%s) — not in city list", curUtc);
        } else {
          strncpy(oth, "Other — keep current offset", sizeof(oth) - 1);
          oth[sizeof(oth) - 1] = '\0';
        }
        b += ">";
        b += esc(oth);
        b += "</option>";
      }
      for (unsigned i = 0; i < SparkyTzPresets_count(); i++) {
        const SparkyTzPreset* p = SparkyTzPresets_get(i);
        if (!p) continue;
        char utcLbl[24];
        SparkyTzPresets_formatUtcOffset(p->offsetMinutes, utcLbl, sizeof(utcLbl));
        b += "<option value='" + String(i) + "'";
        if (!customTz && i == tzSel) b += " selected";
        b += ">" + esc(p->label) + " (" + esc(utcLbl) + ")</option>";
      }
      b += "</select>";
      b += "<p class='small'>Choose the city closest to you (shown with <code>UTC+…</code>). <em>Sync to PC time</em> below sets the offset from your browser; if it does not match a city, <strong>Other</strong> is selected. Turn <strong>Extra +1 h</strong> on for daylight saving in NSW, VIC, TAS, ACT, SA, and NZ — off for QLD, NT, and WA.</p>";
    }
    b += "<div class='row' style='margin-top:10px'><div><label>Extra +1 h (daylight saving)</label><select name='clock_dst'><option value='0'";
    b += boolSelected(!AppState_getClockDstExtraHour());
    b += ">Off</option><option value='1'";
    b += boolSelected(AppState_getClockDstExtraHour());
    b += ">On</option></select><p class='small'>Adds one hour on top of the offset (if sync already includes DST, leave Off).</p></div></div>";
    b += "<p class='small'>Timestamps in emails and reports use dd/mm/yyyy with AM/PM when 12-hour is on.</p>";
    b += "<button class='btn' type='submit'>Save date &amp; time</button></form>";
    {
      char ntp1[APP_STATE_NTP_SERVER_LEN], ntp2[APP_STATE_NTP_SERVER_LEN];
      AppState_getNtpServer1(ntp1, sizeof(ntp1));
      AppState_getNtpServer2(ntp2, sizeof(ntp2));
      b += "<h3 style='margin-top:18px'>NTP (network time)</h3>";
      b += "<p class='small'>When Wi‑Fi is connected, the device can sync UTC from these servers. Wall time still uses the timezone preset, <strong>Extra +1 h</strong>, and 12/24h settings above.</p>";
      b += "<form method='post' action='/admin/save'><input type='hidden' name='section' value='ntp'/>";
      b += "<label>Enable NTP when on Wi‑Fi</label><select name='ntp_en'><option value='1'";
      b += boolSelected(AppState_getNtpEnabled());
      b += ">On</option><option value='0'";
      b += boolSelected(!AppState_getNtpEnabled());
      b += ">Off</option></select>";
      b += "<label>NTP server 1</label><input name='ntp_s1' maxlength='63' value='" + esc(ntp1) + "' placeholder='pool.ntp.org'/>";
      b += "<label>NTP server 2</label><input name='ntp_s2' maxlength='63' value='" + esc(ntp2) + "' placeholder='time.google.com'/>";
      b += "<p class='small'>Leave blank for defaults (pool.ntp.org and time.google.com).</p>";
      b += "<button class='btn btn2' type='submit'>Save NTP settings</button></form>";
    }
    b += "<p class='small' style='margin-top:12px'>Sync sends the PC’s current instant (UTC) and your browser’s offset. Form fields below match the on-device <em>wall</em> clock (offset + optional extra DST). If the hour looks wrong, turn <strong>Extra +1 h DST</strong> off when your zone already includes daylight time in the offset.</p>";
    b += "<button class='btn btn2' type='button' id='syncPcTimeBtn'>Sync to PC time</button>";
    b += "<p class='small' id='syncPcTimeMsg'></p>";
    b += "<script>(function(){const b=document.getElementById('syncPcTimeBtn');const m=document.getElementById('syncPcTimeMsg');if(!b)return;function dstVal(){const s=document.querySelector('select[name=clock_dst]');return s&&s.value==='1'?'1':'0';}b.addEventListener('click',async function(){m.textContent='Setting…';b.disabled=true;try{const u=Math.floor(Date.now()/1000);const tzo=-(new Date().getTimezoneOffset());const body='unix='+encodeURIComponent(String(u))+'&tzo='+encodeURIComponent(String(tzo))+'&dst='+encodeURIComponent(dstVal());const r=await fetch('/admin/clock/sync-pc',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:body,credentials:'same-origin',redirect:'manual'});if(r.type==='opaqueredirect'||r.status===302||r.status===301){window.location.href='/admin';return;}if(r.ok)window.location.href='/admin';else m.textContent='Failed ('+r.status+').';}catch(e){m.textContent='Network error.';}finally{b.disabled=false;}});})();</script></div>";
  }

  b += "<div class='card'><h2>Email / SMTP</h2><form method='post' action='/admin/save'>";
  b += "<input type='hidden' name='section' value='email'/><label>SMTP server</label><input name='smtp_server' value='" + esc(smtpServer) + "'/><p class='small'>Example: smtp.office365.com</p>";
  b += "<div class='row'><div><label>Port</label><input name='smtp_port' value='" + esc(smtpPort) + "'/><p class='small'>Example: 587</p></div>";
  b += "<div><label>Sender user</label><input name='smtp_user' value='" + esc(smtpUser) + "'/><p class='small'>Example: device@yourdomain.com</p></div></div>";
  b += "<label>SMTP password</label><input type='password' name='smtp_pass' value='" + esc(smtpPass) + "'/><p class='small'>Mailbox/app password for sender account.</p>";
  b += "<label>";
  b += fieldMode ? "Report recipient email" : "Teacher email";
  b += "</label><input name='report_to' value='" + esc(reportTo) + "'/><p class='small'>Example: ";
  b += fieldMode ? "supervisor@company.com" : "teacher@tafe.edu.au";
  b += "</p>";
  b += "<button class='btn' type='submit'>Save Email</button></form>";
  b += "<form method='post' action='/admin/email-test'><button class='btn' type='submit'>Send test email</button></form>";
  b += "<p class='small'>Sends a generic test message using current saved SMTP settings.</p>";
  b += "<p class='small' id='emailTestStatusLine'>Email test status: ";
  b += esc(s_emailTestStatus);
  b += "</p>";
  b += "<script>(async function(){const el=document.getElementById('emailTestStatusLine');if(!el)return;async function poll(){try{const r=await fetch('/admin/email-test-status',{cache:'no-store',credentials:'same-origin'});if(!r.ok)return;const j=await r.json();el.textContent='Email test status: '+(j.status||'Unknown');}catch(e){}};poll();setInterval(poll,2000);})();</script>";
  b += "</div>";

  if (!fieldMode) {
    b += "<div class='card mode-training'><h2>Training Sync / SharePoint</h2><form method='post' action='/admin/save'>";
    b += "<input type='hidden' name='section' value='sync'/>";
    b += "<div class='row'><div><label>Enable training sync</label><select name='sync_enabled'><option value='0'";
    b += boolSelected(!AppState_getTrainingSyncEnabled());
    b += ">Off</option><option value='1'";
    b += boolSelected(AppState_getTrainingSyncEnabled());
    b += ">On</option></select></div>";
    b += "<div><label>Enable email reports</label><select name='email_reports'><option value='0'";
    b += boolSelected(!AppState_getEmailReportEnabled());
    b += ">Off</option><option value='1'";
    b += boolSelected(AppState_getEmailReportEnabled());
    b += ">On</option></select></div></div>";
  } else {
    b += "<div class='card mode-field'><h2>SharePoint / Cloud Endpoint</h2><form method='post' action='/admin/save'>";
    b += "<input type='hidden' name='section' value='sync'/>";
    b += "<input type='hidden' name='sync_enabled' value='1'/>";
    b += "<input type='hidden' name='email_reports' value='1'/>";
  }
  b += "<label>Target</label><select name='sync_target'>";
  b += "<option value='0'";
  b += boolSelected(AppState_getTrainingSyncTarget() == TRAINING_SYNC_TARGET_AUTO);
  b += ">Auto</option><option value='1'";
  b += boolSelected(AppState_getTrainingSyncTarget() == TRAINING_SYNC_TARGET_GOOGLE);
  b += ">Google</option><option value='2'";
  b += boolSelected(AppState_getTrainingSyncTarget() == TRAINING_SYNC_TARGET_SHAREPOINT);
  b += ">SharePoint</option></select>";
  b += "<p class='small'>Auto selects backend. SharePoint uses endpoint + token below.</p>";
  b += "<label>Endpoint URL</label><input name='sync_endpoint' value='" + esc(endpoint) + "'/><p class='small'>Example: https://graph.microsoft.com/v1.0/sites/&lt;site&gt;/lists/&lt;list&gt;/items</p>";
  b += "<label>Auth token / API key</label><input type='password' name='sync_token' value='" + esc(token) + "'/><p class='small'>Bearer token or API key for endpoint.</p>";
  b += "<button class='btn' type='submit'>Save Sync</button></form></div>";

  b += "<div class='card mode-both'><h2>Admin Hotspot (fallback)</h2><form method='post' action='/admin/save'>";
  b += "<input type='hidden' name='section' value='admin_ap'/>";
  b += "<label>Hotspot SSID</label><input name='admin_ap_ssid' value='" + esc(adminApSsid) + "'/><p class='small'>Current/next AP name shown on device Wi-Fi screen.</p>";
  b += "<label>Hotspot password</label><input type='password' name='admin_ap_pass' value='" + esc(adminApPass) + "'/><p class='small'>At least 8 chars.</p>";
  b += "<p class='small'>Password must be at least 8 characters. Used only when normal Wi-Fi is offline.</p>";
  b += "<button class='btn' type='submit'>Save Hotspot</button></form></div>";

  b += "<div class='card mode-both'><h2>Change Admin PIN</h2><form method='post' action='/admin/save'>";
  b += "<input type='hidden' name='section' value='admin_pin'/>";
  b += "<div class='row'><div><label>Current PIN</label><input type='password' name='current_pin' maxlength='10'/></div>";
  b += "<div><label>New PIN</label><input type='password' name='new_pin' maxlength='10'/></div></div>";
  b += "<div class='row'><div><label>Confirm PIN</label><input type='password' name='confirm_pin' maxlength='10'/></div><div></div></div>";
  b += "<p class='small'>PIN must be numeric and at least 4 digits.</p>";
  b += "<button class='btn' type='submit'>Update PIN</button></form></div>";

  b += "<script>(function(){const sel=document.getElementById('modeSelect');const hint=document.getElementById('modeSaveHint');if(!sel)return;let saving=false;function apply(){const m=sel.value||'0';document.querySelectorAll('.mode-training').forEach(el=>el.style.display=(m==='0')?'block':'none');document.querySelectorAll('.mode-field').forEach(el=>el.style.display=(m==='1')?'block':'none');document.querySelectorAll('.mode-both').forEach(el=>el.style.display='block');}async function saveMode(){if(saving)return;saving=true;sel.disabled=true;if(hint)hint.textContent='Saving mode...';try{const body='section=mode&mode='+encodeURIComponent(sel.value||'0');const r=await fetch('/admin/save',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});if(!r.ok)throw new Error('HTTP '+r.status);if(hint)hint.textContent='Mode saved. Reloading...';window.location.reload();}catch(e){if(hint)hint.textContent='Save failed. Please retry.';sel.disabled=false;saving=false;}}sel.addEventListener('change',function(){apply();saveMode();});apply();})();</script>";
  return htmlPage("SparkyCheck Admin", b);
}

/** Encode query param for report filenames (keeps admin links working for odd characters). */
static String uriEncodePathParam(const char* s) {
  String o;
  if (!s) return o;
  for (; *s; ++s) {
    unsigned char c = (unsigned char)*s;
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.')
      o += (char)c;
    else {
      char hx[6];
      snprintf(hx, sizeof(hx), "%%%02X", c);
      o += hx;
    }
  }
  return o;
}

static bool isSafeReportName(const String& s) {
  if (s.length() < 1 || s.length() > 64) return false;
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
    if (!ok) return false;
  }
  if (s.indexOf("..") >= 0) return false;
  return true;
}

/* LittleFS file.name() may be "foo.html", "/reports/foo.html", or "reports/foo.html". */
static String reportBasenameFromFsName(const String& name) {
  String n = name;
  if (n.startsWith("/reports/")) return n.substring(9);
  if (n.startsWith("reports/")) return n.substring(8);
  return n;
}

static bool isReportListEntry(const String& rawName) {
  String base = reportBasenameFromFsName(rawName);
  if (!base.length() || base.indexOf('/') >= 0) return false;
  if (!(base.endsWith(".html") || base.endsWith(".csv"))) return false;
  return isSafeReportName(base);
}

static String filesPage(void) {
  String b;
  b.reserve(10000);
  b += "<h1>Report Files</h1>";
  if (s_flashMsg[0]) {
    b += "<div class='card' style='border-color:";
    b += s_flashErr ? "#f87171" : "#4ade80";
    b += ";'><p style='margin:0;'>";
    b += esc(s_flashMsg);
    b += "</p></div>";
    s_flashMsg[0] = '\0';
    s_flashErr = false;
  }
  b += "<p><a href='/admin' style='color:#93c5fd'>Back to admin</a></p>";
  b += "<div class='card'><h2>/reports</h2>";
  if (!LittleFS.exists("/reports")) LittleFS.mkdir("/reports");
  File root = LittleFS.open("/reports");
  if (!root || !root.isDirectory()) {
    b += "<p class='small'>Could not open reports folder (LittleFS).</p></div>";
    return htmlPage("SparkyCheck Reports", b);
  }
  b += "<form method='post' action='/admin/files/delete_bulk' id='reportsBulkForm'>";
  b += "<table style='width:100%;border-collapse:collapse;'><tr>"
       "<th style='text-align:center;width:2.5rem'><input type='checkbox' title='Select all' aria-label='Select all' id='reportsSelAll'/></th>"
       "<th style='text-align:left'>File</th>"
       "<th style='text-align:left'>Local time</th><th style='text-align:right'>Unix (UTC s)</th>"
       "<th style='text-align:right'>Size</th>"
       "<th style='text-align:right'>Actions</th></tr>";
  File f = root.openNextFile();
  int count = 0;
  while (f) {
    String name = f.name();
    if (isReportListEntry(name)) {
      String base = reportBasenameFromFsName(name);
      String enc = uriEncodePathParam(base.c_str());
      String localStr = "&mdash;";
      String unixStr = "&mdash;";
      time_t mw = f.getLastWrite();
      if (mw > (time_t)100000) {
        char tb[56];
        SparkyTime_formatAt(mw, tb, sizeof(tb));
        localStr = tb;
        unixStr = String((unsigned long)mw);
      } else if (mw > 0) {
        unixStr = String((unsigned long)mw);
        localStr = "(weak mtime)";
      }
      b += "<tr><td style='text-align:center'><input type='checkbox' class='reportsFileCb' name='f' value='";
      b += esc(base.c_str());
      b += "' aria-label='Select file'/></td><td>";
      b += esc(base.c_str());
      b += "</td><td style='white-space:nowrap;font-size:0.85rem'>";
      b += localStr;
      b += "</td><td style='text-align:right;font-family:monospace;font-size:0.85rem'>";
      b += unixStr;
      b += "</td><td style='text-align:right'>";
      b += String((unsigned long)f.size());
      b += " B</td><td style='text-align:right;white-space:nowrap'>";
      b += "<a style='color:#93c5fd' href='/admin/files/download?name=";
      b += enc;
      b += "' target='_blank' rel='noopener noreferrer' download>Download</a> &nbsp; ";
      b += "<a style='color:#fca5a5' href='/admin/files/delete?name=";
      b += enc;
      b += "' onclick=\"return confirm('Delete file?')\">Delete</a>";
      b += "</td></tr>";
      count++;
    }
    f = root.openNextFile();
  }
  b += "</table>";
  b += "<p style='margin-top:10px'><button class='btn btn2' type='submit' onclick=\"return confirm('Delete all selected files?')\">Delete selected</button></p>";
  b += "</form>";
  b += "<script>(function(){var a=document.getElementById('reportsSelAll');if(!a)return;var c=function(){var boxes=document.querySelectorAll('.reportsFileCb');for(var i=0;i<boxes.length;i++)boxes[i].checked=a.checked;};a.addEventListener('change',c);})();</script>";
  if (!count) b += "<p class='small'>(No report files yet)</p>";
  b += "</div>";
  return htmlPage("SparkyCheck Reports", b);
}

static void ensureTestsJsonLoaded(void) {
  if (!ensureTestsJsonBuf() || !s_testsJson) return;
  if (!s_testsJson[0]) {
    if (!VerificationSteps_getConfigJson(s_testsJson, kTestsJsonCap)) {
      VerificationSteps_useFactoryDefaults();
      if (!VerificationSteps_getConfigJson(s_testsJson, kTestsJsonCap)) {
        if (!buildTestsJsonFromActive(s_testsJson, kTestsJsonCap)) {
          strncpy(s_testsJson, "{\"tests\":[],\"rules\":[]}", kTestsJsonCap - 1);
        }
      }
    }
    s_testsJson[kTestsJsonCap - 1] = '\0';
  }
  /* Always merge default rules when missing/empty — do not skip when buffer was filled earlier. */
  DynamicJsonDocument doc(kTestsJsonDocCap);
  DeserializationError derr = deserializeJson(doc, s_testsJson);
  if (derr) {
    Serial.printf("[Admin] ensureTestsJsonLoaded parse error: %s\n", derr.c_str());
    return;
  }
  if (doc.overflowed()) {
    Serial.println("[Admin] ensureTestsJsonLoaded: JSON too large for pool (raise kTestsJsonDocCap)");
    return;
  }
  if (ensureDefaultRulesInDoc(doc)) {
    String fixed;
    serializeJsonPretty(doc, fixed);
    if (fixed.length() >= kTestsJsonCap) {
      Serial.printf("[Admin] merged tests.json length %u exceeds kTestsJsonCap\n", (unsigned)fixed.length());
      return;
    }
    strncpy(s_testsJson, fixed.c_str(), kTestsJsonCap - 1);
    s_testsJson[kTestsJsonCap - 1] = '\0';
    if (!LittleFS.exists("/config")) LittleFS.mkdir("/config");
    File fw = LittleFS.open(kTestsPath, "w");
    if (fw) {
      fw.print(fixed);
      fw.close();
    }
  }
}

/** True if tests.json on flash is too large to hold in s_testsJson (must stream). */
static bool testsJsonOnFsExceedsBuffer(void) {
  if (!LittleFS.exists(kTestsPath)) return false;
  File f = LittleFS.open(kTestsPath, "r");
  if (!f || f.isDirectory()) {
    if (f) f.close();
    return false;
  }
  const size_t sz = f.size();
  f.close();
  return sz >= kTestsJsonCap;
}

/** Read /config/tests.json into s_testsJson when it fits (caller runs ensureTestsJsonLoaded after). */
static void reloadTestsJsonFileIntoBuffer(void) {
  if (!ensureTestsJsonBuf() || !s_testsJson) return;
  if (!LittleFS.exists(kTestsPath)) return;
  File f = LittleFS.open(kTestsPath, "r");
  if (!f || f.isDirectory()) {
    if (f) f.close();
    return;
  }
  const size_t sz = f.size();
  if (sz == 0 || sz >= kTestsJsonCap) {
    f.close();
    return;
  }
  const size_t n = f.read((uint8_t*)s_testsJson, kTestsJsonCap - 1);
  f.close();
  s_testsJson[n] = '\0';
}

static void stripUtf8BomInPlace(char* buf) {
  if (!buf || !buf[0]) return;
  if ((uint8_t)buf[0] == 0xEF && (uint8_t)buf[1] == 0xBB && (uint8_t)buf[2] == 0xBF) {
    memmove(buf, buf + 3, strlen(buf + 3) + 1);
  }
}

static void sendTestsJsonLiveResponse(AsyncWebServerRequest* req) {
  if (!isAuthorized(req)) {
    req->send(403, "application/json", "{\"error\":\"not authorized\"}");
    return;
  }
  if (!ensureTestsJsonBuf() || !s_testsJson) {
    req->send(500, "application/json", "{\"error\":\"tests buffer unavailable\"}");
    return;
  }
  /* LittleFS inline beginResponse has caused TCP resets / “Failed to fetch” on some ESP32 builds;
   * serve from RAM after reloading the file into s_testsJson when it fits. */
  if (testsJsonOnFsExceedsBuffer()) {
    AsyncWebServerResponse* resp = req->beginResponse(LittleFS, String(kTestsPath), "application/json", false);
    if (resp) {
      resp->addHeader("Cache-Control", "no-store");
      req->send(resp);
      return;
    }
  }
  reloadTestsJsonFileIntoBuffer();
  stripUtf8BomInPlace(s_testsJson);
  ensureTestsJsonLoaded();
  stripUtf8BomInPlace(s_testsJson);
  if (!s_testsJson[0]) {
    req->send(500, "application/json", "{\"error\":\"tests buffer empty\"}");
    return;
  }
  const size_t jsonLen = strlen(s_testsJson);
  if (jsonLen < 2 || s_testsJson[0] != '{') {
    Serial.printf("[Admin] tests-json-live: buffer not object JSON (len=%u first=%02x)\n", (unsigned)jsonLen,
                  jsonLen ? (unsigned)(uint8_t)s_testsJson[0] : 0u);
    req->send(500, "application/json", "{\"error\":\"tests json invalid or empty in RAM\"}");
    return;
  }
  AsyncWebServerResponse* resp = req->beginResponse(200, "application/json", s_testsJson);
  if (!resp) {
    req->send(500, "text/plain", "out of memory building response");
    return;
  }
  resp->addHeader("Cache-Control", "no-store");
  req->send(resp);
}

static bool docHasRuleKind(DynamicJsonDocument& doc, const char* kind) {
  if (!kind || !kind[0]) return false;
  JsonArray rules = doc["rules"].as<JsonArray>();
  if (rules.isNull()) return false;
  for (JsonObject r : rules) {
    if (strcmp(r["kind"] | "", kind) == 0) return true;
  }
  return false;
}

static bool ensureDefaultRulesInDoc(DynamicJsonDocument& doc) {
  if (doc.containsKey("rules") && !doc["rules"].is<JsonArray>()) {
    doc.remove("rules");
  }
  JsonArray rules = doc["rules"].as<JsonArray>();
  if (rules.isNull()) {
    doc.createNestedArray("rules");
  }
  bool changed = false;
  auto addIfMissing = [&](const char* k, const char* op, float v1, float v2) {
    if (!docHasRuleKind(doc, k)) {
      upsertRuleForKind(doc, k, op, v1, v2);
      changed = true;
    }
  };
  addIfMissing("continuity_ohm", "<=", TestLimits_continuityMaxOhms(), 0.0f);
  addIfMissing("ir_mohm", ">=", TestLimits_insulationMinMOhms(), 0.0f);
  addIfMissing("ir_mohm_sheathed", ">=", TestLimits_insulationMinMOhmsSheathedHeating(), 0.0f);
  addIfMissing("efli_ohm", "<=", TestLimits_efliMaxOhms(), 0.0f);
  addIfMissing("rcd_required_max_ms", "<=", TestLimits_rcdTripTimeMaxMs(), 0.0f);
  addIfMissing("rcd_ms", "<=", TestLimits_rcdTripTimeMaxMs(), 0.0f);
  return changed;
}

static bool activateAndPersistTestsJson(const String& json, String* outErr) {
  String jsonFixed = json;
  DynamicJsonDocument doc(kTestsJsonDocCap);
  if (deserializeJson(doc, jsonFixed) == DeserializationError::Ok) {
    if (ensureDefaultRulesInDoc(doc)) {
      jsonFixed = "";
      serializeJsonPretty(doc, jsonFixed);
    }
  }
  char err[200] = "";
  if (!VerificationSteps_activateConfigJson(jsonFixed.c_str(), err, sizeof(err))) {
    if (outErr) *outErr = err[0] ? String(err) : String("invalid config");
    return false;
  }
  if (!LittleFS.exists("/config")) LittleFS.mkdir("/config");
  if (LittleFS.exists(kTestsPath)) {
    LittleFS.remove(kTestsPrevPath);
    LittleFS.rename(kTestsPath, kTestsPrevPath);
  }
  File fa = LittleFS.open(kTestsPath, "w");
  if (fa) { fa.print(jsonFixed); fa.close(); }
  strncpy(s_testsJson, jsonFixed.c_str(), kTestsJsonCap - 1);
  s_testsJson[kTestsJsonCap - 1] = '\0';
  if (outErr) outErr->remove(0);
  return true;
}

static void renumberTests(JsonArray tests) {
  int i = 0;
  for (JsonObject t : tests) t["id"] = i++;
}

static const char* stepTypeStr(VerifyStepType t) {
  switch (t) {
    case STEP_INFO: return "info";
    case STEP_SAFETY: return "safety";
    case STEP_VERIFY_YESNO: return "verify_yesno";
    case STEP_RESULT_ENTRY: return "result_entry";
    default: return "info";
  }
}

static const char* resultKindStr(VerifyResultKind k) {
  switch (k) {
    case RESULT_CONTINUITY_OHM: return "continuity_ohm";
    case RESULT_IR_MOHM: return "ir_mohm";
    case RESULT_IR_MOHM_SHEATHED: return "ir_mohm_sheathed";
    case RESULT_RCD_REQUIRED_MAX_MS: return "rcd_required_max_ms";
    case RESULT_RCD_MS: return "rcd_ms";
    case RESULT_EFLI_OHM: return "efli_ohm";
    case RESULT_NONE:
    default: return "none";
  }
}

static String jsonEsc(const char* s) {
  if (!s) return "";
  String in(s), out;
  out.reserve(in.length() + 16);
  for (size_t i = 0; i < in.length(); i++) {
    char c = in[i];
    if (c == '\\') out += "\\\\";
    else if (c == '"') out += "\\\"";
    else if (c == '\n') out += "\\n";
    else if (c == '\r') out += "\\r";
    else if (c == '\t') out += "\\t";
    else out += c;
  }
  return out;
}

static bool buildTestsJsonFromActive(char* out, unsigned outSize) {
  if (!out || outSize < 512) return false;
  String j;
  j.reserve(65536);
  j += "{\"tests\":[";
  const int tc = VerificationSteps_getActiveTestCount();
  for (int i = 0; i < tc; i++) {
    if (i) j += ",";
    j += "{\"id\":";
    j += String(i);
    j += ",\"name\":\"";
    j += jsonEsc(VerificationSteps_getTestName((VerifyTestId)i));
    j += "\",\"steps\":[";
    const int sc = VerificationSteps_getStepCount((VerifyTestId)i);
    for (int s = 0; s < sc; s++) {
      if (s) j += ",";
      VerifyStep st{};
      VerificationSteps_getStep((VerifyTestId)i, s, &st);
      j += "{\"type\":\"";
      j += stepTypeStr(st.type);
      j += "\",\"title\":\"";
      j += jsonEsc(st.title ? st.title : "");
      j += "\",\"instruction\":\"";
      j += jsonEsc(st.instruction ? st.instruction : "");
      j += "\",\"clause\":\"";
      j += jsonEsc(st.clause ? st.clause : "");
      j += "\",\"resultKind\":\"";
      j += resultKindStr(st.resultKind);
      j += "\",\"resultLabel\":\"";
      j += jsonEsc(st.resultLabel ? st.resultLabel : "");
      j += "\",\"unit\":\"";
      j += jsonEsc(st.unit ? st.unit : "");
      j += "\"}";
    }
    j += "]}";
  }
  j += "],\"rules\":";
  {
    DynamicJsonDocument ruleDoc(6144);
    JsonArray rules = ruleDoc.createNestedArray("rules");
    VerificationSteps_appendRulesJsonArray(rules);
    serializeJson(rules, j);
  }
  j += "}";
  if (j.length() + 1 >= outSize) return false;
  strncpy(out, j.c_str(), outSize - 1);
  out[outSize - 1] = '\0';
  return true;
}

static bool getFactoryTestsJson(String* outJson) {
  if (!outJson) return false;
  outJson->remove(0);
  VerificationSteps_useFactoryDefaults();
  std::unique_ptr<char[]> buf(new char[kTestsJsonCap]);
  if (!buf) return false;
  if (VerificationSteps_getConfigJson(buf.get(), kTestsJsonCap) || buildTestsJsonFromActive(buf.get(), kTestsJsonCap)) {
    *outJson = String(buf.get());
    return outJson->length() > 0;
  }
  *outJson = "{\"tests\":[],\"rules\":[]}";
  return true;
}

static String postValue(AsyncWebServerRequest* req, const char* key);

static const char* stepTypeFriendly(const char* type) {
  if (!type) return "Info message";
  if (strcmp(type, "info") == 0) return "Info message";
  if (strcmp(type, "safety") == 0) return "Safety check";
  if (strcmp(type, "verify_yesno") == 0) return "Question (Yes/No)";
  if (strcmp(type, "result_entry") == 0) return "Enter reading/value";
  return type;
}

static const char* ruleOpOrDefault(const char* op) {
  if (!op || !op[0]) return "<=";
  if (strcmp(op, "<") == 0 || strcmp(op, ">") == 0 || strcmp(op, "<=") == 0 ||
      strcmp(op, ">=") == 0 || strcmp(op, "==") == 0 || strcmp(op, "between") == 0) return op;
  return "<=";
}

static bool getRuleForKind(DynamicJsonDocument& doc, const char* kind, String* outOp, float* outVal, float* outValMax) {
  if (!kind || !kind[0] || strcmp(kind, "none") == 0) return false;
  JsonArray rules = doc["rules"].as<JsonArray>();
  if (rules.isNull()) return false;
  for (JsonObject r : rules) {
    const char* k = r["kind"] | "";
    if (strcmp(k, kind) == 0) {
      const char* op = ruleOpOrDefault(r["op"] | "<=");
      if (outOp) *outOp = String(op);
      if (strcmp(op, "between") == 0) {
        if (outVal) *outVal = r["min"] | 0.0f;
        if (outValMax) *outValMax = r["max"] | 0.0f;
      } else {
        if (outVal) *outVal = r["value"] | 0.0f;
        if (outValMax) *outValMax = 0.0f;
      }
      return true;
    }
  }
  return false;
}

static void upsertRuleForKind(DynamicJsonDocument& doc, const char* kind, const char* op, float val, float valMax) {
  if (!kind || !kind[0] || strcmp(kind, "none") == 0) return;
  JsonArray rules = doc["rules"].as<JsonArray>();
  if (rules.isNull()) rules = doc.createNestedArray("rules");
  const char* safeOp = ruleOpOrDefault(op);
  for (JsonObject r : rules) {
    const char* k = r["kind"] | "";
    if (strcmp(k, kind) == 0) {
      r["op"] = safeOp;
      if (strcmp(safeOp, "between") == 0) {
        r["min"] = val;
        r["max"] = valMax;
        r.remove("value");
      } else {
        r["value"] = val;
        r.remove("min");
        r.remove("max");
      }
      return;
    }
  }
  JsonObject nr = rules.createNestedObject();
  nr["kind"] = kind;
  nr["op"] = safeOp;
  if (strcmp(safeOp, "between") == 0) {
    nr["min"] = val;
    nr["max"] = valMax;
  } else {
    nr["value"] = val;
  }
}

static const char* defaultRuleUnitForKind(const char* kind) {
  if (!kind) return "";
  if (strcmp(kind, "ir_mohm") == 0 || strcmp(kind, "ir_mohm_sheathed") == 0) return "MOhm";
  if (strcmp(kind, "rcd_required_max_ms") == 0 || strcmp(kind, "rcd_ms") == 0) return "ms";
  if (strcmp(kind, "continuity_ohm") == 0 || strcmp(kind, "efli_ohm") == 0) return "ohm";
  return "";
}

static float ruleUnitFactorToCanonical(const char* kind, const char* unit) {
  if (!kind || !unit) return 1.0f;
  if (strcmp(kind, "ir_mohm") == 0 || strcmp(kind, "ir_mohm_sheathed") == 0) {
    if (strcmp(unit, "ohm") == 0) return 0.000001f;
    if (strcmp(unit, "kOhm") == 0) return 0.001f;
    if (strcmp(unit, "MOhm") == 0) return 1.0f;
    if (strcmp(unit, "GOhm") == 0) return 1000.0f;
    return 1.0f;
  }
  if (strcmp(kind, "rcd_required_max_ms") == 0 || strcmp(kind, "rcd_ms") == 0) {
    if (strcmp(unit, "ms") == 0) return 1.0f;
    if (strcmp(unit, "s") == 0) return 1000.0f;
    return 1.0f;
  }
  if (strcmp(kind, "continuity_ohm") == 0 || strcmp(kind, "efli_ohm") == 0) {
    if (strcmp(unit, "ohm") == 0) return 1.0f;
    if (strcmp(unit, "kOhm") == 0) return 1000.0f;
    if (strcmp(unit, "MOhm") == 0) return 1000000.0f;
    return 1.0f;
  }
  return 1.0f;
}

static bool defaultRuleForKind(const char* kind, String* outOp, float* outVal) {
  if (!kind) return false;
  if (strcmp(kind, "continuity_ohm") == 0) {
    if (outOp) *outOp = "<=";
    if (outVal) *outVal = TestLimits_continuityMaxOhms();
    return true;
  }
  if (strcmp(kind, "ir_mohm") == 0) {
    if (outOp) *outOp = ">=";
    if (outVal) *outVal = TestLimits_insulationMinMOhms();
    return true;
  }
  if (strcmp(kind, "ir_mohm_sheathed") == 0) {
    if (outOp) *outOp = ">=";
    if (outVal) *outVal = TestLimits_insulationMinMOhmsSheathedHeating();
    return true;
  }
  if (strcmp(kind, "rcd_ms") == 0) {
    if (outOp) *outOp = "<=";
    if (outVal) *outVal = TestLimits_rcdTripTimeMaxMs();
    return true;
  }
  if (strcmp(kind, "efli_ohm") == 0) {
    if (outOp) *outOp = "<=";
    if (outVal) *outVal = TestLimits_efliMaxOhms();
    return true;
  }
  return false;
}

static String trimmedValue(AsyncWebServerRequest* req, const char* key) {
  String v = postValue(req, key);
  v.trim();
  return v;
}

static String testsPage(AsyncWebServerRequest* req) {
  // Refresh from persisted single-file config so editor reflects live current content.
  if (LittleFS.exists(kTestsPath)) {
    File f = LittleFS.open(kTestsPath, "r");
    if (f) {
      String fromFs = f.readString();
      f.close();
      if (fromFs.length() > 0 && fromFs.length() < kTestsJsonCap) {
        strncpy(s_testsJson, fromFs.c_str(), kTestsJsonCap - 1);
        s_testsJson[kTestsJsonCap - 1] = '\0';
        /* If rules are empty/missing, repair and persist so the editor shows rule controls. */
        DynamicJsonDocument docFix(kTestsJsonDocCap);
        DeserializationError dfix = deserializeJson(docFix, s_testsJson);
        if (!dfix && !docFix.overflowed() && ensureDefaultRulesInDoc(docFix)) {
          String fixed;
          serializeJsonPretty(docFix, fixed);
          if (fixed.length() < kTestsJsonCap) {
            strncpy(s_testsJson, fixed.c_str(), kTestsJsonCap - 1);
            s_testsJson[kTestsJsonCap - 1] = '\0';
            if (!LittleFS.exists("/config")) LittleFS.mkdir("/config");
            File fw = LittleFS.open(kTestsPath, "w");
            if (fw) { fw.print(fixed); fw.close(); }
          }
        }
      }
    }
  }
  ensureTestsJsonLoaded();
  DynamicJsonDocument doc(kTestsJsonDocCap);
  auto de = deserializeJson(doc, s_testsJson);
  if (!de && doc.overflowed()) {
    de = DeserializationError::NoMemory;
  }
  if (de) {
    /* Repair rules inside the currently loaded JSON (even if the JSON came in large/truncated). */
    if (ensureDefaultRulesInDoc(doc)) {
      String fixed;
      serializeJsonPretty(doc, fixed);
      strncpy(s_testsJson, fixed.c_str(), kTestsJsonCap - 1);
      s_testsJson[kTestsJsonCap - 1] = '\0';
      if (!LittleFS.exists("/config")) LittleFS.mkdir("/config");
      File fw = LittleFS.open(kTestsPath, "w");
      if (fw) { fw.print(fixed); fw.close(); }
    }
    VerificationSteps_useFactoryDefaults();
    if (!VerificationSteps_getConfigJson(s_testsJson, kTestsJsonCap))
      buildTestsJsonFromActive(s_testsJson, kTestsJsonCap);
    doc.clear();
    deserializeJson(doc, s_testsJson);
  }
  if (!doc.overflowed() && !doc.isNull() && ensureDefaultRulesInDoc(doc)) {
    String fixed;
    serializeJsonPretty(doc, fixed);
    if (fixed.length() < kTestsJsonCap) {
      strncpy(s_testsJson, fixed.c_str(), kTestsJsonCap - 1);
      s_testsJson[kTestsJsonCap - 1] = '\0';
      if (!LittleFS.exists("/config")) LittleFS.mkdir("/config");
      File fw = LittleFS.open(kTestsPath, "w");
      if (fw) { fw.print(fixed); fw.close(); }
    }
  }
  JsonArray tests = doc["tests"].as<JsonArray>();
  if (tests.isNull() || tests.size() == 0) {
    VerificationSteps_useFactoryDefaults();
    if (!VerificationSteps_getConfigJson(s_testsJson, kTestsJsonCap))
      buildTestsJsonFromActive(s_testsJson, kTestsJsonCap);
    doc.clear();
    deserializeJson(doc, s_testsJson);
    tests = doc["tests"].as<JsonArray>();
  }
  String b;
  /* Per-step easy editor: ~1.5-4 KiB/step (escaped text); max VERIFY_MAX_STEPS_PER_TEST steps + cards/JSON UI. Live JSON is fetched (not inlined). */
  b.reserve(589824);
  b += "<h1>Tests & Questions</h1><p><a href='/admin' style='color:#93c5fd'>Back to admin</a></p>";
  b += "<div class='card'><h2>Easy editor</h2>";
  b += "<p class='small'>Use this page to build and maintain tests. You do not need JSON or IT knowledge.</p>";
  int selectedTest = 0;
  if (req && req->hasParam("test")) selectedTest = req->getParam("test")->value().toInt();
  const int testCount = tests.isNull() ? 0 : (int)tests.size();
  if (selectedTest < 0) selectedTest = 0;
  if (selectedTest >= testCount && testCount > 0) selectedTest = testCount - 1;
  if (!tests.isNull() && tests.size() > 0) {
    b += "<label>Choose test to edit</label><select id='testPicker' onchange='location.href=\"/admin/tests-page?test=\"+this.value'>";
    int optIdx = 0;
    for (JsonObject t : tests) {
      b += "<option value='";
      b += String(optIdx);
      b += "'";
      if (optIdx == selectedTest) b += " selected";
      b += ">";
      b += esc(webFriendlyTestName((t["name"] | "")).c_str());
      b += "</option>";
      optIdx++;
    }
    b += "</select>";
  }
  b += "<ol class='small'><li>Create a new test (top form).</li><li>Select a test and add one or more steps inside that test.</li><li>Reorder tests if needed.</li><li>Delete only when sure.</li></ol>";
  b += "<form method='post' action='/admin/tests/create' style='display:grid;grid-template-columns:1fr auto;gap:8px;align-items:end'>";
  b += "<div><label>New test name (required)</label><input name='name' value='' placeholder='Example: PAT and polarity check'/></div>";
  b += "<button class='btn' style='margin-top:0' type='submit'>Create new test</button></form>";
  b += "<p class='small'>Create new test = adds a brand-new test. Add step to this test = adds a step inside the selected test below.</p>";
  if (!tests.isNull() && tests.size() > 0) {
    b += "<table style='width:100%;border-collapse:collapse;'><tr><th style='text-align:left'></th><th>Steps</th><th>Actions</th></tr>";
    int idx = 0;
    for (JsonObject t : tests) {
      if (idx != selectedTest) { idx++; continue; }
      const char* nm = t["name"] | "";
      int sc = t["steps"].as<JsonArray>().size();
      b += "<tr class='test-card' data-test-id='";
      b += String(idx);
      b += "'><td>";
      b += esc(webFriendlyTestName(nm).c_str());
      b += "</td><td style='text-align:right'>";
      b += String(sc);
      b += "</td><td>";
      b += "<form method='post' action='/admin/tests/rename' style='display:flex;gap:6px;align-items:center;flex-wrap:wrap;'>";
      b += "<input type='hidden' name='id' value='"; b += String(idx); b += "'/>";
      b += "<input name='name' value='"; b += esc(nm); b += "' placeholder='Test name'/>";
      b += "<button class='btn' style='margin-top:0' type='submit'>Save name</button></form>";
      b += "<form method='post' action='/admin/tests/add_step' style='display:flex;gap:6px;align-items:center;flex-wrap:wrap;margin-top:6px;'>";
      b += "<input type='hidden' name='id' value='"; b += String(idx); b += "'/>";
      b += "<select name='type'><option value='info'>Info message</option><option value='safety'>Safety check</option><option value='verify_yesno'>Question (Yes/No)</option><option value='result_entry'>Enter reading/value</option></select>";
      b += "<input name='title' placeholder='Step title (required)'/>";
      b += "<input name='instruction' placeholder='Instruction shown to user (required)'/>";
      b += "<button class='btn' style='margin-top:0' type='submit'>Add step to this test</button></form>";
      b += "<div class='small' style='margin-top:4px'>Tip: Add at least one step per test.</div>";
      JsonArray steps = t["steps"].as<JsonArray>();
      const int stepCount = (!steps.isNull()) ? (int)steps.size() : 0;
      if (!steps.isNull() && stepCount > VERIFY_MAX_STEPS_PER_TEST) {
        b += "<p class='small' style='margin-top:8px;padding:10px;border:1px solid #b45309;border-radius:8px;color:#fed7aa'>This test has ";
        b += String(stepCount);
        b += " steps; firmware loads at most ";
        b += String((int)VERIFY_MAX_STEPS_PER_TEST);
        b += " per test. Remove extra steps in <strong>Show JSON editor</strong> below or delete the test.</p>";
      }
      if (!steps.isNull() && steps.size() > 0) {
        b += "<div style='margin-top:8px;padding:8px;border:1px solid #334155;border-radius:8px;max-height:min(80vh,720px);overflow-y:auto;overflow-x:hidden'>";
        b += "<div class='small' style='font-weight:600;margin-bottom:6px'>Steps in this test</div>";
        int stepIdx = 0;
        for (JsonObject st : steps) {
          if (stepIdx >= VERIFY_MAX_STEPS_PER_TEST) break;
          const char* stType = st["type"] | "info";
          const char* stTitle = st["title"] | "";
          const char* stInst = st["instruction"] | "";
          const char* stExpectedYesNo = st["expectedYesNo"] | "yes";
          b += "<div style='padding:8px;border:1px solid #1f2937;border-radius:8px;margin-bottom:6px'>";
          b += "<div class='small' style='margin-bottom:4px'>Step ";
          b += String(stepIdx + 1);
          b += " of ";
          b += String((int)steps.size());
          b += "</div>";
          b += "<form method='post' action='/admin/tests/step_update' style='display:flex;gap:6px;align-items:center;flex-wrap:wrap;'>";
          b += "<input type='hidden' name='test_id' value='"; b += String(idx); b += "'/>";
          b += "<input type='hidden' name='step_id' value='"; b += String(stepIdx); b += "'/>";
          b += "<select name='type'>";
          b += "<option value='info'"; if (strcmp(stType, "info") == 0) b += " selected"; b += ">Info message</option>";
          b += "<option value='safety'"; if (strcmp(stType, "safety") == 0) b += " selected"; b += ">Safety check</option>";
          b += "<option value='verify_yesno'"; if (strcmp(stType, "verify_yesno") == 0) b += " selected"; b += ">Question (Yes/No)</option>";
          b += "<option value='result_entry'"; if (strcmp(stType, "result_entry") == 0) b += " selected"; b += ">Enter reading/value</option>";
          b += "</select>";
          b += "<select name='expected_yesno'>";
          b += "<option value='yes'"; if (strcmp(stExpectedYesNo, "yes") == 0) b += " selected"; b += ">Expected answer: Yes</option>";
          b += "<option value='no'"; if (strcmp(stExpectedYesNo, "no") == 0) b += " selected"; b += ">Expected answer: No</option>";
          b += "<option value='branch'"; if (strcmp(stExpectedYesNo, "branch") == 0) b += " selected"; b += ">Branch only (no pass/fail)</option>";
          b += "</select>";
          b += "<input name='title' value='"; b += esc(stTitle); b += "' placeholder='Step title'/>";
          b += "<input name='instruction' value='"; b += esc(stInst); b += "' placeholder='Instruction'/>";
          const char* stRkRaw = st["resultKind"] | "none";
          const char* stRk = (strcmp(stRkRaw, "rcd_required_max_ms") == 0) ? "rcd_ms" : stRkRaw;
          String ruleOp = "<=";
          float ruleVal = 0.0f;
          float ruleValMax = 0.0f;
          bool hasRule = getRuleForKind(doc, stRk, &ruleOp, &ruleVal, &ruleValMax);
          if (!hasRule) {
            hasRule = defaultRuleForKind(stRk, &ruleOp, &ruleVal);
            ruleValMax = 0.0f;
          }
          const char* defaultUnit = defaultRuleUnitForKind(stRk);
          b += "<select name='result_kind'>";
          b += "<option value='none'"; if (strcmp(stRk, "none") == 0) b += " selected"; b += ">No expected pass value</option>";
          b += "<option value='continuity_ohm'"; if (strcmp(stRk, "continuity_ohm") == 0) b += " selected"; b += ">Continuity (ohm)</option>";
          b += "<option value='ir_mohm'"; if (strcmp(stRk, "ir_mohm") == 0) b += " selected"; b += ">Insulation resistance (MOhm)</option>";
          b += "<option value='ir_mohm_sheathed'"; if (strcmp(stRk, "ir_mohm_sheathed") == 0) b += " selected"; b += ">Insulation sheathed (MOhm)</option>";
          b += "<option value='rcd_ms'"; if (strcmp(stRk, "rcd_ms") == 0) b += " selected"; b += ">RCD trip time (ms)</option>";
          b += "<option value='efli_ohm'"; if (strcmp(stRk, "efli_ohm") == 0) b += " selected"; b += ">EFLI (ohm)</option>";
          b += "</select>";
          b += "<select name='rule_op'>";
          b += "<option value='<'"; if (ruleOp == "<") b += " selected"; b += ">&lt;</option>";
          b += "<option value='<='"; if (ruleOp == "<=") b += " selected"; b += ">&lt;=</option>";
          b += "<option value='>'"; if (ruleOp == ">") b += " selected"; b += ">&gt;</option>";
          b += "<option value='>='"; if (ruleOp == ">=") b += " selected"; b += ">&gt;=</option>";
          b += "<option value='=='"; if (ruleOp == "==") b += " selected"; b += ">==</option>";
          b += "<option value='between'"; if (ruleOp == "between") b += " selected"; b += ">between</option>";
          b += "</select>";
          b += "<input name='rule_value' value='";
          b += hasRule ? String(ruleVal, 3) : String("");
          b += "' placeholder='Expected value or min'/>";
          b += "<input name='rule_value_max' value='";
          b += (hasRule && ruleOp == "between") ? String(ruleValMax, 3) : String("");
          b += "' placeholder='Max value (for between)'/>";
          b += "<select name='rule_unit'>";
          b += "<option value=''>canonical</option>";
          b += "<option value='ohm'"; if (strcmp(defaultUnit, "ohm") == 0) b += " selected"; b += ">ohm</option>";
          b += "<option value='kOhm'"; if (strcmp(defaultUnit, "kOhm") == 0) b += " selected"; b += ">kOhm</option>";
          b += "<option value='MOhm'"; if (strcmp(defaultUnit, "MOhm") == 0) b += " selected"; b += ">MOhm</option>";
          b += "<option value='GOhm'"; if (strcmp(defaultUnit, "GOhm") == 0) b += " selected"; b += ">GOhm</option>";
          b += "<option value='ms'"; if (strcmp(defaultUnit, "ms") == 0) b += " selected"; b += ">ms</option>";
          b += "<option value='s'"; if (strcmp(defaultUnit, "s") == 0) b += " selected"; b += ">s</option>";
          b += "</select>";
          b += "<button class='btn' style='margin-top:0' type='submit'>Save step</button></form>";
          b += "<div class='small' style='margin-top:4px'>Type: ";
          b += esc(stepTypeFriendly(stType));
          b += "</div>";
          b += "<form method='post' action='/admin/tests/step_reorder' style='display:inline'><input type='hidden' name='test_id' value='"; b += String(idx); b += "'/><input type='hidden' name='step_id' value='"; b += String(stepIdx); b += "'/><input type='hidden' name='dir' value='up'/><button class='btn btn2' style='margin-top:6px' type='submit'>Step up</button></form> ";
          b += "<form method='post' action='/admin/tests/step_reorder' style='display:inline'><input type='hidden' name='test_id' value='"; b += String(idx); b += "'/><input type='hidden' name='step_id' value='"; b += String(stepIdx); b += "'/><input type='hidden' name='dir' value='down'/><button class='btn btn2' style='margin-top:6px' type='submit'>Step down</button></form> ";
          b += "<form method='post' action='/admin/tests/step_move' style='display:inline-flex;gap:4px;align-items:center;margin-top:6px;'>";
          b += "<input type='hidden' name='test_id' value='"; b += String(idx); b += "'/><input type='hidden' name='step_id' value='"; b += String(stepIdx); b += "'/>";
          b += "<input name='to' type='number' min='1' max='"; b += String((int)steps.size()); b += "' value='"; b += String(stepIdx + 1); b += "' style='width:68px;padding:4px'/>";
          b += "<button class='btn btn2' style='margin-top:0' type='submit'>Move</button></form> ";
          b += "<form method='post' action='/admin/tests/step_delete' style='display:inline'><input type='hidden' name='test_id' value='"; b += String(idx); b += "'/><input type='hidden' name='step_id' value='"; b += String(stepIdx); b += "'/><button class='btn btn2' style='margin-top:6px;background:#7f1d1d' type='submit' onclick='return confirm(\"Delete this step?\")'>Delete Step</button></form>";
          b += "</div>";
          stepIdx++;
        }
        b += "<div class='small' style='margin-top:8px'>Step up/down = move one place. Move = jump to the number entered. Delete Step = remove this step only.</div>";
        b += "</div>";
      }
      b += "<form method='post' action='/admin/tests/reorder' style='display:inline'><input type='hidden' name='id' value='"; b += String(idx); b += "'/><input type='hidden' name='dir' value='up'/><button class='btn btn2' style='margin-top:6px' type='submit'>Move Test Up</button></form> ";
      b += "<form method='post' action='/admin/tests/reorder' style='display:inline'><input type='hidden' name='id' value='"; b += String(idx); b += "'/><input type='hidden' name='dir' value='down'/><button class='btn btn2' style='margin-top:6px' type='submit'>Move Test Down</button></form> ";
      b += "<form method='post' action='/admin/tests/delete' style='display:inline'><input type='hidden' name='id' value='"; b += String(idx); b += "'/><button class='btn btn2' style='margin-top:6px;background:#7f1d1d' type='submit' onclick='return confirm(\"Delete this test?\")'>Delete Entire Test</button></form>";
      b += "<div class='small' style='margin-top:4px'>Move Test Up/Down = reorder this whole test in the main test list. Delete Entire Test = remove this test and all its steps.</div>";
      b += "</td></tr>";
      break;
    }
    b += "</table>";
  }
  b += "</div>";
  b += "<div class='card'><h2>Advanced editor (live tests.json)</h2><p class='small'>This is the full JSON currently loaded on the device. Edit directly, then click Validate + Activate.</p>";
  b += "<details><summary style='cursor:pointer;color:#93c5fd'>Show JSON editor</summary>";
  b += "<p class='small'>Edit JSON for test names, step order, questions, clauses, result kinds, and comparator rules.</p>";
  b += "<p class='small'>Supports up to ";
  b += String((int)VERIFY_TEST_CAPACITY);
  b += " tests.</p>";
  b += "<p class='small'>Comparator rules support: &lt;, &gt;, &lt;=, &gt;=, ==, between (min/max)</p>";
  b += "<details style='margin:8px 0'><summary style='cursor:pointer;color:#93c5fd'>Show example JSON template</summary>";
  b += "<p class='small'>Tip: Keep test IDs sequential (0,1,2...). Each test must have at least one step.</p>";
  b += "<textarea readonly style='width:100%;min-height:260px;border-radius:8px;background:#0f172a;color:#cfe8ff;border:1px solid #8ecae6;'>"
       "{\n"
       "  \"tests\": [\n"
       "    {\n"
       "      \"id\": 0,\n"
       "      \"name\": \"Earth continuity\",\n"
       "      \"steps\": [\n"
       "        {\n"
       "          \"type\": \"safety\",\n"
       "          \"title\": \"Safety\",\n"
       "          \"instruction\": \"Isolate circuit before testing.\",\n"
       "          \"clause\": \"AS/NZS 3000 Clause 8.3\",\n"
       "          \"resultKind\": \"none\",\n"
       "          \"resultLabel\": \"\",\n"
       "          \"unit\": \"\"\n"
       "        },\n"
       "        {\n"
       "          \"type\": \"result_entry\",\n"
       "          \"title\": \"Continuity reading\",\n"
       "          \"instruction\": \"Enter measured resistance.\",\n"
       "          \"clause\": \"AS/NZS 3000 Clause 8.3.5\",\n"
       "          \"resultKind\": \"continuity_ohm\",\n"
       "          \"resultLabel\": \"Continuity\",\n"
       "          \"unit\": \"ohm\"\n"
       "        }\n"
       "      ]\n"
       "    }\n"
       "  ],\n"
       "  \"rules\": [\n"
       "    { \"kind\": \"continuity_ohm\", \"op\": \"<=\", \"value\": 0.5 }\n"
       "  ]\n"
       "}\n"
       "</textarea></details>";
  b += "<form method='post' action='/admin/tests/activate'><label>Live JSON config (current tests.json)</label>";
  b += "<textarea id='liveJsonEditor' name='json' style='width:100%;min-height:380px;border-radius:8px;background:#0f172a;color:#fff;border:1px solid #8ecae6;' placeholder='Loading JSON from device…'></textarea>";
  b += "<script>(function(){var e=document.getElementById('liveJsonEditor');var btn=document.getElementById('liveJsonActivateBtn');if(!e)return;"
       "var url=(window.location.origin||'')+'/admin/tests-json-live';"
       "function load(n){e.value='Loading…';if(btn)btn.disabled=true;"
       "fetch(url,{credentials:'same-origin',cache:'no-store',mode:'same-origin',redirect:'follow'})"
       ".then(function(r){return r.text().then(function(t){"
       "if(!r.ok)throw new Error(t||('HTTP '+r.status));"
       "var ct=(r.headers.get('content-type')||'').toLowerCase();"
       "var body=(t||'').replace(/^\\uFEFF/,'');var s=body.trim();"
       "if(!s.length)throw new Error('Empty body from device.');"
       "if(ct.indexOf('text/html')>=0||s.charAt(0)==='<')throw new Error('Got a web page instead of JSON (session expired?). Close this tab, open Admin, log in with PIN, then open Tests again.');"
       "var c=s.charAt(0);if(c!='{'&&c!='[')throw new Error('Body does not look like JSON (first char: '+c+').');"
       "try{JSON.parse(s);}catch(x){throw new Error('Invalid JSON: '+x.message);}"
       "e.value=body;if(btn)btn.disabled=false;"
       "});})"
       ".catch(function(err){if(n<2)setTimeout(function(){load(n+1);},500);"
       "else{e.value='// Failed to load live tests.json: '+(err&&err.message?err.message:String(err))+'. Reload page or use Download tests.json.\\n';if(btn)btn.disabled=false;}});}"
       "load(0);})();</script>";
  b += "<button class='btn' type='submit' id='liveJsonActivateBtn' disabled>Validate + Activate</button></form>";
  b += "</details>";
  b += "<form method='post' action='/admin/tests/rollback'><button class='btn btn2' type='submit'>Undo to previous saved version</button></form>";
  b += "<form method='get' action='/tests-download-live'><button class='btn btn2' type='submit'>Download tests.json</button></form>";
  b += "<form method='post' action='/tests-import-upload' enctype='multipart/form-data'>";
  b += "<label>Import tests.json from this computer (saved to device flash, not SD card)</label>";
  b += "<input type='file' name='tests_file' accept='.json,application/json' required/>";
  b += "<button class='btn btn2' type='submit'>Choose file & import</button></form>";
  b += "<p class='small'>“Choose file” uploads from your PC/browser to the ESP32 LittleFS file system (same as editing on the device). It does not read the microSD slot.</p>";
  b += "<form method='post' action='/admin/tests/factory'><button class='btn btn2' type='submit' onclick='return confirm(\"Restore factory tests and remove custom tests?\")'>Restore factory defaults</button></form>";
  b += "</div>";
  return htmlPage("SparkyCheck Tests", b);
}

static String postValue(AsyncWebServerRequest* req, const char* key) {
  if (!req || !key) return "";
  if (!req->hasParam(key, true)) return "";
  return req->getParam(key, true)->value();
}

static void adminEmailTestTask(void* arg) {
  const uint32_t jobId = (uint32_t)(uintptr_t)arg;
  (void)jobId;
  snprintf(s_emailTestStatus, sizeof(s_emailTestStatus), "Sending...");
  char err[160] = "";
  const bool ok = EmailTest_sendNow(err, sizeof(err));
  if (ok) {
    snprintf(s_flashMsg, sizeof(s_flashMsg), "Test email sent.");
    snprintf(s_emailTestStatus, sizeof(s_emailTestStatus), "Sent successfully.");
    s_flashErr = false;
  } else {
    snprintf(s_flashMsg, sizeof(s_flashMsg), "Test email failed: %s", err[0] ? err : "unknown");
    snprintf(s_emailTestStatus, sizeof(s_emailTestStatus), "Failed: %s", err[0] ? err : "unknown");
    s_flashErr = true;
  }
  std::atomic_thread_fence(std::memory_order_release);
  s_emailTestRunning.store(false, std::memory_order_relaxed);
  s_emailTestPending.store(false, std::memory_order_relaxed);
  vTaskDelete(nullptr);
}

static void handleTestsImportUploadDone(AsyncWebServerRequest* req) {
  if (!isAuthorized(req)) {
    req->send(403, "text/plain", "Forbidden");
    return;
  }
  if (s_uploadTestsJson.length() == 0) {
    strncpy(s_flashMsg, "Import failed: no file uploaded.", sizeof(s_flashMsg) - 1);
    s_flashMsg[sizeof(s_flashMsg) - 1] = '\0';
    s_flashErr = true;
    req->redirect("/admin/tests-page");
    return;
  }
  String err;
  if (!activateAndPersistTestsJson(s_uploadTestsJson, &err)) {
    snprintf(s_flashMsg, sizeof(s_flashMsg), "Import failed: %s", err.length() ? err.c_str() : "invalid JSON");
    s_flashErr = true;
    s_uploadTestsJson = "";
    req->redirect("/admin/tests-page");
    return;
  }
  strncpy(s_flashMsg, "Imported tests.json from uploaded file.", sizeof(s_flashMsg) - 1);
  s_flashMsg[sizeof(s_flashMsg) - 1] = '\0';
  s_flashErr = false;
  s_uploadTestsJson = "";
  req->redirect("/admin/tests-page");
}

static void handleTestsImportUploadChunk(AsyncWebServerRequest* req, const String& filename, size_t index, uint8_t* data,
                                         size_t len, bool final) {
  (void)final;
  (void)filename;
  if (!isAuthorized(req)) return;
  if (index == 0) {
    s_uploadTestsJson = "";
  }
  if (s_uploadTestsJson.length() + len > kTestsJsonCap - 1) {
    s_uploadTestsJson = "";
    return;
  }
  for (size_t i = 0; i < len; i++) s_uploadTestsJson += (char)data[i];
}

void AdminPortal_init(void) {
  if (s_started) return;
  if (!ensureTestsJsonBuf()) {
    Serial.println("[Admin] FATAL: tests JSON buffer (PSRAM) alloc failed");
    return;
  }

  s_server.on("/admin/login", HTTP_GET, [](AsyncWebServerRequest* req) {
    sendHtmlNoCache(req, loginPage(""));
  });
  s_server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
    req->redirect("/admin");
  });

  s_server.on("/admin/logo.bmp", HTTP_GET, [](AsyncWebServerRequest* req) {
    streamBootLogoBmp(req);
  });

  s_server.on("/admin/logo565", HTTP_GET, [](AsyncWebServerRequest* req) {
    streamBootLogo565(req);
  });

  s_server.on("/admin/logo-info", HTTP_GET, [](AsyncWebServerRequest* req) {
    int w = 0, h = 0;
    size_t words = 0;
    logoEffectiveDims(&w, &h, &words);
    String j = "{";
    j += "\"bootW\":" + String((int)BOOT_LOGO_W) + ",";
    j += "\"bootH\":" + String((int)BOOT_LOGO_H) + ",";
    j += "\"effectiveW\":" + String(w) + ",";
    j += "\"effectiveH\":" + String(h) + ",";
    j += "\"arrayWords\":" + String((unsigned long)words) + ",";
    j += "\"bytes565\":" + String((unsigned long)(words * 2u));
    j += "}";
    AsyncWebServerResponse* resp = req->beginResponse(200, "application/json", j);
    resp->addHeader("Cache-Control", "no-store");
    req->send(resp);
  });

  s_server.on("/admin/login", HTTP_POST, [](AsyncWebServerRequest* req) {
    String pinStr = postValue(req, "pin");
    uint32_t pin = (uint32_t)strtoul(pinStr.c_str(), nullptr, 10);
    if (!AppState_checkPin(pin)) {
      req->send(200, "text/html", loginPage("Invalid PIN."));
      return;
    }
    s_authed = true;
    s_sessionToken = (uint32_t)(esp_random() ^ micros());
    touchSession();
    AsyncWebServerResponse* resp = req->beginResponse(302);
    char cookie[96];
    snprintf(cookie, sizeof(cookie), "sparky_sid=%lu; Path=/; HttpOnly", (unsigned long)s_sessionToken);
    resp->addHeader("Set-Cookie", cookie);
    resp->addHeader("Location", "/admin");
    req->send(resp);
  });

  s_server.on("/admin/logout", HTTP_GET, [](AsyncWebServerRequest* req) {
    s_authed = false;
    s_sessionToken = 0;
    AsyncWebServerResponse* resp = req->beginResponse(302);
    resp->addHeader("Set-Cookie", "sparky_sid=0; Path=/; Max-Age=0");
    resp->addHeader("Location", "/admin/login");
    req->send(resp);
  });

  /* Register download/delete before /admin/files/ — prefix handler matches /admin/files/download otherwise. */
  s_server.on("/admin/files/download", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (!isAuthorized(req)) { req->send(403, "text/plain", "Forbidden"); return; }
    String name;
    if (req->hasParam("name", false)) name = req->getParam("name", false)->value();
    else if (req->hasParam("name", true)) name = req->getParam("name", true)->value();
    if (!isSafeReportName(name)) { req->send(400, "text/plain", "Invalid name"); return; }
    String path = "/reports/" + name;
    if (!LittleFS.exists(path)) { req->send(404, "text/plain", "Not found"); return; }
    File f = LittleFS.open(path, "r");
    if (!f || f.isDirectory()) {
      if (f) f.close();
      req->send(500, "text/plain", "Could not open file");
      return;
    }
    size_t sz = f.size();
    f.close();
    if (sz > 1024UL * 1024UL) {
      req->send(413, "text/plain", "File too large");
      return;
    }
    /* Stream from LittleFS (String/buffer responses often fail or OOM on device). */
    AsyncWebServerResponse* resp = req->beginResponse(LittleFS, path, "application/octet-stream", true);
    if (resp) {
      resp->addHeader("Content-Disposition", String("attachment; filename=\"") + name + "\"");
      resp->addHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
      req->send(resp);
      return;
    }
    /* Some ESPAsyncWebServer builds: direct send from FS. */
    req->send(LittleFS, path, "application/octet-stream", true);
  });

  s_server.on("/admin/files/delete", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (!isAuthorized(req)) { req->send(403, "text/plain", "Forbidden"); return; }
    String name = req->hasParam("name") ? req->getParam("name")->value() : "";
    if (!isSafeReportName(name)) { req->send(400, "text/plain", "Invalid name"); return; }
    String path = "/reports/" + name;
    if (!LittleFS.exists(path)) { req->send(404, "text/plain", "Not found"); return; }
    LittleFS.remove(path);
    req->redirect("/admin/files");
  });

  s_server.on("/admin/files/delete_bulk", HTTP_POST, [](AsyncWebServerRequest* req) {
    if (!isAuthorized(req)) { req->send(403, "text/plain", "Forbidden"); return; }
    int deleted = 0;
    const int n = req->params();
    for (int i = 0; i < n; i++) {
      const AsyncWebParameter* p = req->getParam(i);
      if (!p || !p->isPost()) continue;
      if (p->name() != "f") continue;
      String name = p->value();
      if (!isSafeReportName(name)) continue;
      String path = "/reports/" + name;
      if (LittleFS.exists(path) && LittleFS.remove(path)) deleted++;
    }
    if (deleted > 0) {
      snprintf(s_flashMsg, sizeof(s_flashMsg), "Deleted %d report file(s).", deleted);
      s_flashMsg[sizeof(s_flashMsg) - 1] = '\0';
      s_flashErr = false;
    } else {
      strncpy(s_flashMsg, "No files deleted (none selected or invalid names).", sizeof(s_flashMsg) - 1);
      s_flashMsg[sizeof(s_flashMsg) - 1] = '\0';
      s_flashErr = true;
    }
    req->redirect("/admin/files");
  });

  s_server.on("/admin/clock/sync-pc", HTTP_POST, [](AsyncWebServerRequest* req) {
    if (!isAuthorized(req)) { req->send(403, "text/plain", "Forbidden"); return; }
    const String su = postValue(req, "unix");
    const unsigned long u = strtoul(su.c_str(), nullptr, 10);
    /* Reject garbage; allow roughly 2020–2099 UTC. */
    if (u < 1577836800UL || u > 4102444800UL) {
      strncpy(s_flashMsg, "Invalid time from browser.", sizeof(s_flashMsg) - 1);
      s_flashMsg[sizeof(s_flashMsg) - 1] = '\0';
      s_flashErr = true;
      req->redirect("/admin");
      return;
    }
    {
      const String tzoS = postValue(req, "tzo");
      int tzo = tzoS.toInt();
      if (tzo >= -900 && tzo <= 900) AppState_setClockTzOffsetMinutes((int16_t)tzo);
      if (req->hasParam("dst", true)) AppState_setClockDstExtraHour(postValue(req, "dst").toInt() != 0);
    }
    struct timeval tv;
    tv.tv_sec = (time_t)u;
    tv.tv_usec = 0;
    if (settimeofday(&tv, nullptr) == 0) {
      AppState_saveWallClockUtc((time_t)u);
      SparkyRtc_writeFromSystemClock();
      strncpy(s_flashMsg, "Clock synced (UTC) and browser timezone offset saved.", sizeof(s_flashMsg) - 1);
      s_flashMsg[sizeof(s_flashMsg) - 1] = '\0';
      s_flashErr = false;
    } else {
      strncpy(s_flashMsg, "Could not set system clock from PC time.", sizeof(s_flashMsg) - 1);
      s_flashMsg[sizeof(s_flashMsg) - 1] = '\0';
      s_flashErr = true;
    }
    req->redirect("/admin");
  });

  s_server.on("/admin/files", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (!isAuthorized(req)) { req->redirect("/admin/login"); return; }
    sendHtmlNoCache(req, filesPage());
  });
  s_server.on("/admin/files/", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (!isAuthorized(req)) { req->redirect("/admin/login"); return; }
    sendHtmlNoCache(req, filesPage());
  });
  s_server.on("/admin/reports", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (!isAuthorized(req)) { req->redirect("/admin/login"); return; }
    sendHtmlNoCache(req, filesPage());
  });

  s_server.on("/admin/tests", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (!isAuthorized(req)) { req->redirect("/admin/login"); return; }
    Serial.println("[Admin] /admin/tests GET");
    sendHtmlNoCache(req, testsPage(req));
  });

  s_server.on("/admin/tests-page", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (!isAuthorized(req)) { req->redirect("/admin/login"); return; }
    Serial.println("[Admin] /admin/tests-page GET");
    sendHtmlNoCache(req, testsPage(req));
  });

  s_server.on("/admin/tests/", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (!isAuthorized(req)) { req->redirect("/admin/login"); return; }
    req->redirect("/admin/tests-page");
  });

  s_server.on("/admin/tests/activate", HTTP_POST, [](AsyncWebServerRequest* req) {
    if (!isAuthorized(req)) { req->send(403, "text/plain", "Forbidden"); return; }
    String json = postValue(req, "json");
    String err;
    if (!activateAndPersistTestsJson(json, &err)) {
      snprintf(s_flashMsg, sizeof(s_flashMsg), "Tests config invalid: %s", err.length() ? err.c_str() : "unknown");
      s_flashErr = true;
      strncpy(s_testsJson, json.c_str(), kTestsJsonCap - 1);
      s_testsJson[kTestsJsonCap - 1] = '\0';
      req->redirect("/admin/tests");
      return;
    }
    strncpy(s_flashMsg, "Tests config activated.", sizeof(s_flashMsg) - 1);
    s_flashMsg[sizeof(s_flashMsg) - 1] = '\0';
    s_flashErr = false;
    req->redirect("/admin/tests");
  });

  s_server.on("/admin/tests/rollback", HTTP_POST, [](AsyncWebServerRequest* req) {
    if (!isAuthorized(req)) { req->send(403, "text/plain", "Forbidden"); return; }
    if (!LittleFS.exists(kTestsPrevPath)) {
      strncpy(s_flashMsg, "No previous tests config found.", sizeof(s_flashMsg) - 1);
      s_flashMsg[sizeof(s_flashMsg) - 1] = '\0';
      s_flashErr = true;
      req->redirect("/admin/tests");
      return;
    }
    File fp = LittleFS.open(kTestsPrevPath, "r");
    String json = fp ? fp.readString() : "";
    if (fp) fp.close();
    String err;
    if (!activateAndPersistTestsJson(json, &err)) {
      snprintf(s_flashMsg, sizeof(s_flashMsg), "Rollback failed: %s", err.length() ? err.c_str() : "invalid");
      s_flashErr = true;
      req->redirect("/admin/tests");
      return;
    }
    strncpy(s_flashMsg, "Rolled back to previous tests config.", sizeof(s_flashMsg) - 1);
    s_flashMsg[sizeof(s_flashMsg) - 1] = '\0';
    s_flashErr = false;
    req->redirect("/admin/tests");
  });

  s_server.on("/admin/tests/factory", HTTP_POST, [](AsyncWebServerRequest* req) {
    if (!isAuthorized(req)) { req->send(403, "text/plain", "Forbidden"); return; }
    if (!LittleFS.exists("/config")) LittleFS.mkdir("/config");
    LittleFS.remove(kTestsPath);
    LittleFS.remove(kTestsPrevPath);
    String factoryJson;
    getFactoryTestsJson(&factoryJson);
    char err[120] = "";
    VerificationSteps_activateConfigJson(factoryJson.c_str(), err, sizeof(err));
    File fa = LittleFS.open(kTestsPath, "w");
    if (fa) { fa.print(factoryJson); fa.close(); }
    strncpy(s_testsJson, factoryJson.c_str(), kTestsJsonCap - 1);
    s_testsJson[kTestsJsonCap - 1] = '\0';
    strncpy(s_flashMsg, "Factory defaults restored (built-in tests).", sizeof(s_flashMsg) - 1);
    s_flashMsg[sizeof(s_flashMsg) - 1] = '\0';
    s_flashErr = false;
    req->redirect("/admin/tests-page");
  });

  s_server.on("/tests-download-live", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (!isAuthorized(req)) { req->send(403, "text/plain", "Forbidden"); return; }
    ensureTestsJsonLoaded();
    if (!s_testsJson) {
      req->send(500, "text/plain", "tests buffer unavailable");
      return;
    }
    AsyncWebServerResponse* resp = req->beginResponse(200, "application/json", s_testsJson);
    resp->addHeader("Content-Disposition", "attachment; filename=\"tests.json\"");
    resp->addHeader("Cache-Control", "no-store");
    req->send(resp);
  });

  s_server.on("/tests-json-live", HTTP_GET, [](AsyncWebServerRequest* req) { sendTestsJsonLiveResponse(req); });
  s_server.on("/admin/tests-json-live", HTTP_GET, [](AsyncWebServerRequest* req) { sendTestsJsonLiveResponse(req); });
  s_server.on("/admin/tests-download", HTTP_GET, [](AsyncWebServerRequest* req) {
    req->redirect("/tests-download-live");
  });

  s_server.on("/tests-import-upload", HTTP_POST, handleTestsImportUploadDone, handleTestsImportUploadChunk);
  s_server.on("/admin/tests-import-upload", HTTP_POST, handleTestsImportUploadDone, handleTestsImportUploadChunk);
  s_server.on("/admin/tests/import/upload", HTTP_POST, handleTestsImportUploadDone, handleTestsImportUploadChunk);

  s_server.on("/admin/tests/create", HTTP_POST, [](AsyncWebServerRequest* req) {
    if (!isAuthorized(req)) { req->send(403, "text/plain", "Forbidden"); return; }
    ensureTestsJsonLoaded();
    DynamicJsonDocument doc(kTestsJsonDocCap);
    deserializeJson(doc, s_testsJson);
    JsonArray tests = doc["tests"].as<JsonArray>();
    if (tests.isNull()) tests = doc.createNestedArray("tests");
    if ((int)tests.size() >= VERIFY_TEST_CAPACITY) {
      strncpy(s_flashMsg, "Max tests reached.", sizeof(s_flashMsg) - 1);
      s_flashMsg[sizeof(s_flashMsg) - 1] = '\0';
      s_flashErr = true;
      req->redirect("/admin/tests");
      return;
    }
    String name = trimmedValue(req, "name");
    if (!name.length()) {
      strncpy(s_flashMsg, "Please enter a test name.", sizeof(s_flashMsg) - 1);
      s_flashMsg[sizeof(s_flashMsg) - 1] = '\0';
      s_flashErr = true;
      req->redirect("/admin/tests");
      return;
    }
    JsonObject t = tests.createNestedObject();
    t["id"] = (int)tests.size() - 1;
    t["name"] = name;
    JsonArray steps = t.createNestedArray("steps");
    JsonObject s = steps.createNestedObject();
    s["type"] = "info";
    s["title"] = "New step";
    s["instruction"] = "Add instruction";
    s["clause"] = "";
    s["resultKind"] = "none";
    s["resultLabel"] = "";
    s["unit"] = "";
    renumberTests(tests);
    String json; serializeJsonPretty(doc, json);
    String err;
    if (!activateAndPersistTestsJson(json, &err)) {
      snprintf(s_flashMsg, sizeof(s_flashMsg), "Create failed: %s", err.c_str());
      s_flashErr = true;
    } else {
      strncpy(s_flashMsg, "Test created.", sizeof(s_flashMsg) - 1);
      s_flashMsg[sizeof(s_flashMsg) - 1] = '\0';
      s_flashErr = false;
    }
    req->redirect("/admin/tests");
  });

  s_server.on("/admin/tests/rename", HTTP_POST, [](AsyncWebServerRequest* req) {
    if (!isAuthorized(req)) { req->send(403, "text/plain", "Forbidden"); return; }
    ensureTestsJsonLoaded();
    DynamicJsonDocument doc(kTestsJsonDocCap);
    deserializeJson(doc, s_testsJson);
    JsonArray tests = doc["tests"].as<JsonArray>();
    int id = postValue(req, "id").toInt();
    String name = trimmedValue(req, "name");
    if (id < 0 || id >= (int)tests.size()) {
      strncpy(s_flashMsg, "Invalid test selection.", sizeof(s_flashMsg) - 1);
      s_flashMsg[sizeof(s_flashMsg) - 1] = '\0';
      s_flashErr = true;
      req->redirect("/admin/tests");
      return;
    }
    if (!name.length()) {
      strncpy(s_flashMsg, "Test name cannot be blank.", sizeof(s_flashMsg) - 1);
      s_flashMsg[sizeof(s_flashMsg) - 1] = '\0';
      s_flashErr = true;
      req->redirect("/admin/tests");
      return;
    }
    tests[id]["name"] = name;
    String json; serializeJsonPretty(doc, json);
    String err;
    if (!activateAndPersistTestsJson(json, &err)) {
      snprintf(s_flashMsg, sizeof(s_flashMsg), "Rename failed: %s", err.c_str());
      s_flashErr = true;
    } else {
      strncpy(s_flashMsg, "Test renamed.", sizeof(s_flashMsg) - 1);
      s_flashMsg[sizeof(s_flashMsg) - 1] = '\0';
      s_flashErr = false;
    }
    req->redirect("/admin/tests");
  });

  s_server.on("/admin/tests/add_step", HTTP_POST, [](AsyncWebServerRequest* req) {
    if (!isAuthorized(req)) { req->send(403, "text/plain", "Forbidden"); return; }
    ensureTestsJsonLoaded();
    DynamicJsonDocument doc(kTestsJsonDocCap);
    deserializeJson(doc, s_testsJson);
    JsonArray tests = doc["tests"].as<JsonArray>();
    int id = postValue(req, "id").toInt();
    if (id < 0 || id >= (int)tests.size()) {
      strncpy(s_flashMsg, "Invalid test selection.", sizeof(s_flashMsg) - 1);
      s_flashMsg[sizeof(s_flashMsg) - 1] = '\0';
      s_flashErr = true;
      req->redirect("/admin/tests");
      return;
    }
    String type = trimmedValue(req, "type");
    String expectedYesNo = trimmedValue(req, "expected_yesno");
    String title = trimmedValue(req, "title");
    String instruction = trimmedValue(req, "instruction");
    if (!title.length() || !instruction.length()) {
      strncpy(s_flashMsg, "Step title and instruction are required.", sizeof(s_flashMsg) - 1);
      s_flashMsg[sizeof(s_flashMsg) - 1] = '\0';
      s_flashErr = true;
      req->redirect("/admin/tests");
      return;
    }
    if (type != "info" && type != "safety" && type != "verify_yesno" && type != "result_entry") type = "info";
    JsonArray steps = tests[id]["steps"].as<JsonArray>();
    JsonObject s = steps.createNestedObject();
    s["type"] = type;
    s["title"] = title;
    s["instruction"] = instruction;
    s["clause"] = "";
    s["resultKind"] = "none";
    s["resultLabel"] = "";
    s["unit"] = "";
    String json; serializeJsonPretty(doc, json);
    String err;
    if (!activateAndPersistTestsJson(json, &err)) {
      snprintf(s_flashMsg, sizeof(s_flashMsg), "Add step failed: %s", err.c_str());
      s_flashErr = true;
    } else {
      strncpy(s_flashMsg, "Step added.", sizeof(s_flashMsg) - 1);
      s_flashMsg[sizeof(s_flashMsg) - 1] = '\0';
      s_flashErr = false;
    }
    req->redirect("/admin/tests");
  });

  s_server.on("/admin/tests/reorder", HTTP_POST, [](AsyncWebServerRequest* req) {
    if (!isAuthorized(req)) { req->send(403, "text/plain", "Forbidden"); return; }
    ensureTestsJsonLoaded();
    DynamicJsonDocument doc(kTestsJsonDocCap);
    deserializeJson(doc, s_testsJson);
    JsonArray tests = doc["tests"].as<JsonArray>();
    int id = postValue(req, "id").toInt();
    String dir = postValue(req, "dir");
    int other = (dir == "up") ? id - 1 : id + 1;
    if (id >= 0 && other >= 0 && id < (int)tests.size() && other < (int)tests.size()) {
      JsonObject a = tests[id];
      JsonObject b = tests[other];
      DynamicJsonDocument tmp(8192);
      tmp.set(a);
      a.set(b);
      b.set(tmp.as<JsonVariant>());
      renumberTests(tests);
    }
    String json; serializeJsonPretty(doc, json);
    String err;
    if (!activateAndPersistTestsJson(json, &err)) {
      snprintf(s_flashMsg, sizeof(s_flashMsg), "Reorder failed: %s", err.c_str());
      s_flashErr = true;
    } else {
      strncpy(s_flashMsg, "Tests reordered.", sizeof(s_flashMsg) - 1);
      s_flashMsg[sizeof(s_flashMsg) - 1] = '\0';
      s_flashErr = false;
    }
    req->redirect("/admin/tests");
  });

  s_server.on("/admin/tests/delete", HTTP_POST, [](AsyncWebServerRequest* req) {
    if (!isAuthorized(req)) { req->send(403, "text/plain", "Forbidden"); return; }
    ensureTestsJsonLoaded();
    DynamicJsonDocument doc(kTestsJsonDocCap);
    deserializeJson(doc, s_testsJson);
    JsonArray tests = doc["tests"].as<JsonArray>();
    int id = postValue(req, "id").toInt();
    if (id >= 0 && id < (int)tests.size()) tests.remove(id);
    renumberTests(tests);
    String json; serializeJsonPretty(doc, json);
    String err;
    if (!activateAndPersistTestsJson(json, &err)) {
      snprintf(s_flashMsg, sizeof(s_flashMsg), "Delete failed: %s", err.c_str());
      s_flashErr = true;
    } else {
      strncpy(s_flashMsg, "Test deleted.", sizeof(s_flashMsg) - 1);
      s_flashMsg[sizeof(s_flashMsg) - 1] = '\0';
      s_flashErr = false;
    }
    req->redirect("/admin/tests");
  });

  s_server.on("/admin/tests/step_update", HTTP_POST, [](AsyncWebServerRequest* req) {
    if (!isAuthorized(req)) { req->send(403, "text/plain", "Forbidden"); return; }
    ensureTestsJsonLoaded();
    DynamicJsonDocument doc(kTestsJsonDocCap);
    deserializeJson(doc, s_testsJson);
    JsonArray tests = doc["tests"].as<JsonArray>();
    int testId = postValue(req, "test_id").toInt();
    int stepId = postValue(req, "step_id").toInt();
    if (testId < 0 || testId >= (int)tests.size()) {
      strncpy(s_flashMsg, "Invalid test selection.", sizeof(s_flashMsg) - 1);
      s_flashMsg[sizeof(s_flashMsg) - 1] = '\0';
      s_flashErr = true;
      req->redirect("/admin/tests");
      return;
    }
    JsonArray steps = tests[testId]["steps"].as<JsonArray>();
    if (stepId < 0 || stepId >= (int)steps.size()) {
      strncpy(s_flashMsg, "Invalid step selection.", sizeof(s_flashMsg) - 1);
      s_flashMsg[sizeof(s_flashMsg) - 1] = '\0';
      s_flashErr = true;
      req->redirect("/admin/tests");
      return;
    }
    String type = trimmedValue(req, "type");
    String expectedYesNo = trimmedValue(req, "expected_yesno");
    String title = trimmedValue(req, "title");
    String instruction = trimmedValue(req, "instruction");
    String resultKind = trimmedValue(req, "result_kind");
    String ruleOp = trimmedValue(req, "rule_op");
    String ruleValue = trimmedValue(req, "rule_value");
    String ruleValueMax = trimmedValue(req, "rule_value_max");
    String ruleUnit = trimmedValue(req, "rule_unit");
    if (!title.length() || !instruction.length()) {
      strncpy(s_flashMsg, "Step title and instruction are required.", sizeof(s_flashMsg) - 1);
      s_flashMsg[sizeof(s_flashMsg) - 1] = '\0';
      s_flashErr = true;
      req->redirect("/admin/tests");
      return;
    }
    if (type != "info" && type != "safety" && type != "verify_yesno" && type != "result_entry") type = "info";
    if (expectedYesNo != "yes" && expectedYesNo != "no" && expectedYesNo != "branch") expectedYesNo = "yes";
    JsonObject st = steps[stepId];
    st["type"] = type;
    if (type == "verify_yesno") st["expectedYesNo"] = expectedYesNo;
    else st.remove("expectedYesNo");
    st["title"] = title;
    st["instruction"] = instruction;
    if (resultKind == "rcd_required_max_ms") resultKind = "rcd_ms";
    if (resultKind != "continuity_ohm" && resultKind != "ir_mohm" && resultKind != "ir_mohm_sheathed" &&
        resultKind != "rcd_ms" && resultKind != "efli_ohm" && resultKind != "none") {
      resultKind = "none";
    }
    st["resultKind"] = resultKind;
    if (resultKind != "none" && ruleValue.length()) {
      float factor = ruleUnitFactorToCanonical(resultKind.c_str(), ruleUnit.length() ? ruleUnit.c_str() : defaultRuleUnitForKind(resultKind.c_str()));
      float v1 = ruleValue.toFloat() * factor;
      float v2 = (ruleValueMax.length() ? ruleValueMax.toFloat() : ruleValue.toFloat()) * factor;
      upsertRuleForKind(doc, resultKind.c_str(), ruleOp.c_str(), v1, v2);
    }
    String json; serializeJsonPretty(doc, json);
    String err;
    if (!activateAndPersistTestsJson(json, &err)) {
      snprintf(s_flashMsg, sizeof(s_flashMsg), "Save step failed: %s", err.c_str());
      s_flashErr = true;
    } else {
      strncpy(s_flashMsg, "Step updated.", sizeof(s_flashMsg) - 1);
      s_flashMsg[sizeof(s_flashMsg) - 1] = '\0';
      s_flashErr = false;
    }
    req->redirect("/admin/tests");
  });

  s_server.on("/admin/tests/step_reorder", HTTP_POST, [](AsyncWebServerRequest* req) {
    if (!isAuthorized(req)) { req->send(403, "text/plain", "Forbidden"); return; }
    ensureTestsJsonLoaded();
    DynamicJsonDocument doc(kTestsJsonDocCap);
    deserializeJson(doc, s_testsJson);
    JsonArray tests = doc["tests"].as<JsonArray>();
    int testId = postValue(req, "test_id").toInt();
    int stepId = postValue(req, "step_id").toInt();
    String dir = postValue(req, "dir");
    if (testId < 0 || testId >= (int)tests.size()) {
      strncpy(s_flashMsg, "Invalid test selection.", sizeof(s_flashMsg) - 1);
      s_flashMsg[sizeof(s_flashMsg) - 1] = '\0';
      s_flashErr = true;
      req->redirect("/admin/tests");
      return;
    }
    JsonArray steps = tests[testId]["steps"].as<JsonArray>();
    int other = (dir == "up") ? stepId - 1 : stepId + 1;
    if (stepId >= 0 && other >= 0 && stepId < (int)steps.size() && other < (int)steps.size()) {
      JsonObject a = steps[stepId];
      JsonObject b = steps[other];
      DynamicJsonDocument tmp(4096);
      tmp.set(a);
      a.set(b);
      b.set(tmp.as<JsonVariant>());
    }
    String json; serializeJsonPretty(doc, json);
    String err;
    if (!activateAndPersistTestsJson(json, &err)) {
      snprintf(s_flashMsg, sizeof(s_flashMsg), "Move step failed: %s", err.c_str());
      s_flashErr = true;
    } else {
      strncpy(s_flashMsg, "Step moved.", sizeof(s_flashMsg) - 1);
      s_flashMsg[sizeof(s_flashMsg) - 1] = '\0';
      s_flashErr = false;
    }
    req->redirect("/admin/tests");
  });

  s_server.on("/admin/tests/step_delete", HTTP_POST, [](AsyncWebServerRequest* req) {
    if (!isAuthorized(req)) { req->send(403, "text/plain", "Forbidden"); return; }
    ensureTestsJsonLoaded();
    DynamicJsonDocument doc(kTestsJsonDocCap);
    deserializeJson(doc, s_testsJson);
    JsonArray tests = doc["tests"].as<JsonArray>();
    int testId = postValue(req, "test_id").toInt();
    int stepId = postValue(req, "step_id").toInt();
    if (testId < 0 || testId >= (int)tests.size()) {
      strncpy(s_flashMsg, "Invalid test selection.", sizeof(s_flashMsg) - 1);
      s_flashMsg[sizeof(s_flashMsg) - 1] = '\0';
      s_flashErr = true;
      req->redirect("/admin/tests");
      return;
    }
    JsonArray steps = tests[testId]["steps"].as<JsonArray>();
    if (stepId < 0 || stepId >= (int)steps.size()) {
      strncpy(s_flashMsg, "Invalid step selection.", sizeof(s_flashMsg) - 1);
      s_flashMsg[sizeof(s_flashMsg) - 1] = '\0';
      s_flashErr = true;
      req->redirect("/admin/tests");
      return;
    }
    steps.remove(stepId);
    String json; serializeJsonPretty(doc, json);
    String err;
    if (!activateAndPersistTestsJson(json, &err)) {
      snprintf(s_flashMsg, sizeof(s_flashMsg), "Delete step failed: %s", err.c_str());
      s_flashErr = true;
    } else {
      strncpy(s_flashMsg, "Step deleted.", sizeof(s_flashMsg) - 1);
      s_flashMsg[sizeof(s_flashMsg) - 1] = '\0';
      s_flashErr = false;
    }
    req->redirect("/admin/tests");
  });

  s_server.on("/admin/tests/step_move", HTTP_POST, [](AsyncWebServerRequest* req) {
    if (!isAuthorized(req)) { req->send(403, "text/plain", "Forbidden"); return; }
    ensureTestsJsonLoaded();
    DynamicJsonDocument doc(kTestsJsonDocCap);
    deserializeJson(doc, s_testsJson);
    JsonArray tests = doc["tests"].as<JsonArray>();
    int testId = postValue(req, "test_id").toInt();
    int stepId = postValue(req, "step_id").toInt();
    int to = postValue(req, "to").toInt() - 1;
    if (testId < 0 || testId >= (int)tests.size()) { req->redirect("/admin/tests-page"); return; }
    JsonArray steps = tests[testId]["steps"].as<JsonArray>();
    if (stepId < 0 || stepId >= (int)steps.size()) { req->redirect("/admin/tests-page"); return; }
    if (to < 0) to = 0;
    if (to >= (int)steps.size()) to = (int)steps.size() - 1;
    if (to != stepId) {
      DynamicJsonDocument tmp(4096);
      tmp.set(steps[stepId]);
      steps.remove(stepId);
      steps.createNestedObject();
      for (int i = (int)steps.size() - 1; i > to; --i) steps[i].set(steps[i - 1]);
      steps[to].set(tmp.as<JsonVariant>());
    }
    String json; serializeJsonPretty(doc, json);
    String err;
    if (!activateAndPersistTestsJson(json, &err)) {
      snprintf(s_flashMsg, sizeof(s_flashMsg), "Move step failed: %s", err.c_str());
      s_flashErr = true;
    } else {
      strncpy(s_flashMsg, "Step moved.", sizeof(s_flashMsg) - 1);
      s_flashMsg[sizeof(s_flashMsg) - 1] = '\0';
      s_flashErr = false;
    }
    req->redirect("/admin/tests-page");
  });

  s_server.on("/admin/wifi/scan", HTTP_POST, [](AsyncWebServerRequest* req) {
    if (!isAuthorized(req)) { req->send(403, "text/plain", "Forbidden"); return; }
    s_scanCount = WifiManager_scan(s_scanResults, WIFI_MAX_SSIDS);
    snprintf(s_flashMsg, sizeof(s_flashMsg), "Wi-Fi scan complete (%d network%s).", s_scanCount, s_scanCount == 1 ? "" : "s");
    s_flashErr = false;
    req->redirect("/admin");
  });

  s_server.on("/admin/wifi/connect", HTTP_POST, [](AsyncWebServerRequest* req) {
    if (!isAuthorized(req)) { req->send(403, "text/plain", "Forbidden"); return; }
    String ssid = postValue(req, "ssid");
    String pass = postValue(req, "pass");
    if (!ssid.length()) {
      strncpy(s_flashMsg, "SSID is required.", sizeof(s_flashMsg) - 1);
      s_flashMsg[sizeof(s_flashMsg) - 1] = '\0';
      s_flashErr = true;
      req->redirect("/admin");
      return;
    }
    bool ok = WifiManager_connect(ssid.c_str(), pass.length() ? pass.c_str() : "");
    if (ok) {
      s_pendingHttpRebind.store(true);
      strncpy(s_flashMsg, "Wi-Fi connected from admin portal.", sizeof(s_flashMsg) - 1);
      s_flashMsg[sizeof(s_flashMsg) - 1] = '\0';
      s_flashErr = false;
    } else {
      strncpy(s_flashMsg, "Wi-Fi connect failed. Check SSID/password.", sizeof(s_flashMsg) - 1);
      s_flashMsg[sizeof(s_flashMsg) - 1] = '\0';
      s_flashErr = true;
    }
    req->redirect("/admin");
  });

  s_server.on("/admin/wifi/disconnect", HTTP_POST, [](AsyncWebServerRequest* req) {
    if (!isAuthorized(req)) { req->send(403, "text/plain", "Forbidden"); return; }
    WifiManager_disconnect(false);
    strncpy(s_flashMsg, "Wi-Fi disconnected.", sizeof(s_flashMsg) - 1);
    s_flashMsg[sizeof(s_flashMsg) - 1] = '\0';
    s_flashErr = false;
    req->redirect("/admin");
  });

  s_server.on("/admin/save", HTTP_POST, [](AsyncWebServerRequest* req) {
    if (!isAuthorized(req)) {
      req->send(403, "text/plain", "Forbidden");
      return;
    }
    String section = postValue(req, "section");
    if (section == "mode") {
      int mode = postValue(req, "mode").toInt();
      AppState_setMode(mode == 1 ? APP_MODE_FIELD : APP_MODE_TRAINING);
    } else if (section == "clock") {
      int d = postValue(req, "clock_day").toInt();
      int mo = postValue(req, "clock_month").toInt();
      int y = postValue(req, "clock_year").toInt();
      int hh = postValue(req, "clock_hour").toInt();
      int mi = postValue(req, "clock_min").toInt();
      int se = postValue(req, "clock_sec").toInt();
      AppState_setClock12Hour(postValue(req, "clock_12").toInt() != 0);
      {
        String preset = postValue(req, "clock_tz_preset");
        if (preset != "custom" && preset.length() > 0) {
          int pi = preset.toInt();
          if (pi >= 0 && (unsigned)pi < SparkyTzPresets_count()) {
            const SparkyTzPreset* zp = SparkyTzPresets_get((unsigned)pi);
            if (zp) AppState_setClockTzOffsetMinutes(zp->offsetMinutes);
          }
        }
        /* preset == custom: leave offset unchanged (e.g. from PC sync). */
      }
      AppState_setClockDstExtraHour(postValue(req, "clock_dst").toInt() != 0);
      if (d >= 1 && d <= 31 && mo >= 1 && mo <= 12 && y >= 2000 && y <= 2099 && hh >= 0 && hh <= 23 && mi >= 0 && mi <= 59 &&
          se >= 0 && se <= 59) {
        if (SparkyTime_setSystemUtcFromWallFields(d, mo, y, hh, mi, se, true)) {
          strncpy(s_flashMsg, "Date and time saved.", sizeof(s_flashMsg) - 1);
          s_flashMsg[sizeof(s_flashMsg) - 1] = '\0';
          s_flashErr = false;
        } else {
          strncpy(s_flashMsg, "Could not set system clock.", sizeof(s_flashMsg) - 1);
          s_flashMsg[sizeof(s_flashMsg) - 1] = '\0';
          s_flashErr = true;
        }
      } else {
        strncpy(s_flashMsg, "Invalid date or time values.", sizeof(s_flashMsg) - 1);
        s_flashMsg[sizeof(s_flashMsg) - 1] = '\0';
        s_flashErr = true;
      }
    } else if (section == "ntp") {
      AppState_setNtpEnabled(postValue(req, "ntp_en").toInt() == 1);
      AppState_setNtpServer1(postValue(req, "ntp_s1").c_str());
      AppState_setNtpServer2(postValue(req, "ntp_s2").c_str());
      SparkyNtp_requestRestart();
      strncpy(s_flashMsg, "NTP settings saved.", sizeof(s_flashMsg) - 1);
      s_flashMsg[sizeof(s_flashMsg) - 1] = '\0';
      s_flashErr = false;
    } else if (section == "email") {
      String smtpServer = postValue(req, "smtp_server");
      String smtpPort = postValue(req, "smtp_port");
      String smtpUser = postValue(req, "smtp_user");
      String smtpPass = postValue(req, "smtp_pass");
      String reportTo = postValue(req, "report_to");
      AppState_setSmtpServer(smtpServer.c_str());
      AppState_setSmtpPort(smtpPort.c_str());
      AppState_setSmtpUser(smtpUser.c_str());
      AppState_setSmtpPass(smtpPass.c_str());
      AppState_setReportToEmail(reportTo.c_str());
    } else if (section == "sync") {
      AppState_setTrainingSyncEnabled(postValue(req, "sync_enabled").toInt() == 1);
      AppState_setEmailReportEnabled(postValue(req, "email_reports").toInt() == 1);
      int target = postValue(req, "sync_target").toInt();
      if (target < 0 || target > 2) target = 0;
      AppState_setTrainingSyncTarget((TrainingSyncTarget)target);
      String endpoint = postValue(req, "sync_endpoint");
      String token = postValue(req, "sync_token");
      AppState_setTrainingSyncEndpoint(endpoint.c_str());
      AppState_setTrainingSyncToken(token.c_str());
    } else if (section == "identity") {
      String cubicle = postValue(req, "sync_cubicle");
      String deviceId = postValue(req, "device_id");
      AppState_setTrainingSyncCubicleId(cubicle.c_str());
      AppState_setDeviceIdOverride(deviceId.c_str());
    } else if (section == "admin_ap") {
      String apSsid = postValue(req, "admin_ap_ssid");
      String apPass = postValue(req, "admin_ap_pass");
      if (apPass.length() >= 8) {
        AppState_setAdminApCredentials(apSsid.c_str(), apPass.c_str());
        strncpy(s_flashMsg, "Admin hotspot updated.", sizeof(s_flashMsg) - 1);
        s_flashMsg[sizeof(s_flashMsg) - 1] = '\0';
      } else {
        strncpy(s_flashMsg, "Hotspot password must be at least 8 characters.", sizeof(s_flashMsg) - 1);
        s_flashMsg[sizeof(s_flashMsg) - 1] = '\0';
        s_flashErr = true;
      }
    } else if (section == "ota") {
      String url = postValue(req, "ota_manifest_url");
      AppState_setOtaManifestUrl(url.c_str());
      AppState_setOtaAutoCheckEnabled(postValue(req, "ota_auto_check").toInt() == 1);
      strncpy(s_flashMsg, "OTA settings saved.", sizeof(s_flashMsg) - 1);
      s_flashMsg[sizeof(s_flashMsg) - 1] = '\0';
      s_flashErr = false;
    } else if (section == "admin_pin") {
      String currentPin = postValue(req, "current_pin");
      String newPin = postValue(req, "new_pin");
      String confirmPin = postValue(req, "confirm_pin");
      bool numeric = true;
      for (size_t i = 0; i < newPin.length(); i++) {
        char c = newPin[i];
        if (c < '0' || c > '9') { numeric = false; break; }
      }
      uint32_t cur = (uint32_t)strtoul(currentPin.c_str(), nullptr, 10);
      if (!AppState_checkPin(cur)) {
        strncpy(s_flashMsg, "Current PIN is incorrect.", sizeof(s_flashMsg) - 1);
        s_flashMsg[sizeof(s_flashMsg) - 1] = '\0';
        s_flashErr = true;
      } else if (newPin.length() < 4 || !numeric) {
        strncpy(s_flashMsg, "PIN must be numeric and at least 4 digits.", sizeof(s_flashMsg) - 1);
        s_flashMsg[sizeof(s_flashMsg) - 1] = '\0';
        s_flashErr = true;
      } else if (newPin != confirmPin) {
        strncpy(s_flashMsg, "PIN confirm does not match.", sizeof(s_flashMsg) - 1);
        s_flashMsg[sizeof(s_flashMsg) - 1] = '\0';
        s_flashErr = true;
      } else {
        uint32_t pin = (uint32_t)strtoul(newPin.c_str(), nullptr, 10);
        AppState_setPin(pin);
        strncpy(s_flashMsg, "Admin PIN updated.", sizeof(s_flashMsg) - 1);
        s_flashMsg[sizeof(s_flashMsg) - 1] = '\0';
      }
    }
    AsyncWebServerResponse* resp = req->beginResponse(302);
    resp->addHeader("Location", "/admin");
    req->send(resp);
  });

  s_server.on("/admin/email-test", HTTP_POST, [](AsyncWebServerRequest* req) {
    if (!isAuthorized(req)) {
      req->send(403, "text/plain", "Forbidden");
      return;
    }
    if (s_emailTestPending.load() || s_emailTestRunning.load()) {
      snprintf(s_flashMsg, sizeof(s_flashMsg), "Test email already queued...");
      snprintf(s_emailTestStatus, sizeof(s_emailTestStatus), "Queued...");
      s_flashErr = false;
    } else {
      s_emailTestPending.store(true);
      s_emailTestRunning.store(true);
      s_emailTestStartMs = millis();
      s_emailTestActiveJobId++;
      snprintf(s_flashMsg, sizeof(s_flashMsg), "Test email queued.");
      snprintf(s_emailTestStatus, sizeof(s_emailTestStatus), "Queued...");
      s_flashErr = false;
      if (xTaskCreate(adminEmailTestTask, "email_test_task", 12288, (void*)(uintptr_t)s_emailTestActiveJobId, 1, nullptr) != pdPASS) {
        s_emailTestRunning.store(false);
        s_emailTestPending.store(false);
        snprintf(s_flashMsg, sizeof(s_flashMsg), "Failed to start email task.");
        snprintf(s_emailTestStatus, sizeof(s_emailTestStatus), "Failed: could not start task.");
        s_flashErr = true;
      }
    }
    AsyncWebServerResponse* resp = req->beginResponse(302);
    resp->addHeader("Location", "/admin");
    req->send(resp);
  });

  s_server.on("/admin/email-test-status", HTTP_GET, [](AsyncWebServerRequest* req) {
    /* Always return JSON so the polling UI can recover even if session cookies drop. */
    if (!isAuthorized(req)) { req->send(200, "application/json", "{\"pending\":false,\"status\":\"Not authorized\"}"); return; }
    String j = "{\"pending\":";
    j += s_emailTestPending.load() ? "true" : "false";
    j += ",\"status\":\"";
    j += jsonEsc(s_emailTestStatus);
    j += "\"}";
    AsyncWebServerResponse* resp = req->beginResponse(200, "application/json", j);
    resp->addHeader("Cache-Control", "no-store");
    req->send(resp);
  });

  s_server.on("/admin/ota-status", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (!isAuthorized(req)) {
      req->send(200, "application/json", "{\"authorized\":false,\"last_status\":\"Not authorized\"}");
      return;
    }
    char st[OTA_STATUS_LEN], pend[OTA_VERSION_LEN];
    OtaUpdate_getLastStatus(st, sizeof(st));
    OtaUpdate_getPendingVersion(pend, sizeof(pend));
    String j = "{\"authorized\":true,\"wifi\":";
    j += WifiManager_isConnected() ? "true" : "false";
    j += ",\"configured\":";
    j += OtaUpdate_isConfigured() ? "true" : "false";
    j += ",\"check_busy\":";
    j += OtaUpdate_isManifestCheckBusy() ? "true" : "false";
    j += ",\"current\":\"";
    j += jsonEsc(OtaUpdate_getCurrentVersion());
    j += "\",\"has_pending\":";
    j += OtaUpdate_hasPendingUpdate() ? "true" : "false";
    j += ",\"pending_version\":\"";
    j += jsonEsc(pend);
    j += "\",\"last_status\":\"";
    j += jsonEsc(st);
    j += "\"}";
    AsyncWebServerResponse* resp = req->beginResponse(200, "application/json", j);
    resp->addHeader("Cache-Control", "no-store");
    req->send(resp);
  });

  s_server.on("/admin/ota/check", HTTP_POST, [](AsyncWebServerRequest* req) {
    if (!isAuthorized(req)) {
      req->send(403, "text/plain", "Forbidden");
      return;
    }
    if (!WifiManager_isConnected()) {
      strncpy(s_flashMsg, "OTA check needs Wi-Fi.", sizeof(s_flashMsg) - 1);
      s_flashMsg[sizeof(s_flashMsg) - 1] = '\0';
      s_flashErr = true;
    } else if (!OtaUpdate_isConfigured()) {
      strncpy(s_flashMsg, "Set a manifest URL under OTA settings first.", sizeof(s_flashMsg) - 1);
      s_flashMsg[sizeof(s_flashMsg) - 1] = '\0';
      s_flashErr = true;
    } else if (OtaUpdate_isManifestCheckBusy()) {
      strncpy(s_flashMsg, "OTA check already running.", sizeof(s_flashMsg) - 1);
      s_flashMsg[sizeof(s_flashMsg) - 1] = '\0';
      s_flashErr = false;
    } else if (!OtaUpdate_startManifestCheckFromUi()) {
      strncpy(s_flashMsg, "Could not start OTA check.", sizeof(s_flashMsg) - 1);
      s_flashMsg[sizeof(s_flashMsg) - 1] = '\0';
      s_flashErr = true;
    } else {
      strncpy(s_flashMsg, "Checking for updates...", sizeof(s_flashMsg) - 1);
      s_flashMsg[sizeof(s_flashMsg) - 1] = '\0';
      s_flashErr = false;
    }
    AsyncWebServerResponse* resp = req->beginResponse(302);
    resp->addHeader("Location", "/admin");
    req->send(resp);
  });

  s_server.on("/admin/ota/install", HTTP_POST, [](AsyncWebServerRequest* req) {
    if (!isAuthorized(req)) {
      req->send(403, "text/plain", "Forbidden");
      return;
    }
    if (!OtaUpdate_hasPendingUpdate()) {
      strncpy(s_flashMsg, "No pending firmware to install. Run Check for updates first.", sizeof(s_flashMsg) - 1);
      s_flashMsg[sizeof(s_flashMsg) - 1] = '\0';
      s_flashErr = true;
    } else if (!WifiManager_isConnected()) {
      strncpy(s_flashMsg, "Install needs Wi-Fi.", sizeof(s_flashMsg) - 1);
      s_flashMsg[sizeof(s_flashMsg) - 1] = '\0';
      s_flashErr = true;
    } else {
      s_webOtaInstallPending.store(true);
      strncpy(s_flashMsg, "Install queued - screen shows progress; device reboots when done.", sizeof(s_flashMsg) - 1);
      s_flashMsg[sizeof(s_flashMsg) - 1] = '\0';
      s_flashErr = false;
    }
    AsyncWebServerResponse* resp = req->beginResponse(302);
    resp->addHeader("Location", "/admin");
    req->send(resp);
  });

  /* Register last: some stacks treat this as a prefix of /admin/... and would otherwise show settings for every admin URL. */
  s_server.on("/admin", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (req->url() == "/admin/tests" || req->url() == "/admin/tests/" || req->url() == "/admin/tests-page") {
      if (!isAuthorized(req)) { req->redirect("/admin/login"); return; }
      Serial.println("[Admin] /admin/tests-page GET via /admin handler");
      sendHtmlNoCache(req, testsPage(req));
      return;
    }
    if (!isAuthorized(req)) {
      sendHtmlNoCache(req, loginPage(""));
      return;
    }
    sendHtmlNoCache(req, settingsPage());
  });

  s_server.onNotFound([](AsyncWebServerRequest* req) {
    req->redirect("/admin");
  });

  tryStartServer();

  // Build baseline JSON from embedded defaults (firmware baseline).
  String factoryJson;
  getFactoryTestsJson(&factoryJson);

  // Single-file model: load /config/tests.json if present, else bootstrap from baseline.
  if (LittleFS.exists(kTestsPath)) {
    File fa = LittleFS.open(kTestsPath, "r");
    if (fa) {
      String json = fa.readString();
      fa.close();
      DynamicJsonDocument d(kTestsJsonDocCap);
      if (deserializeJson(d, json) == DeserializationError::Ok && ensureDefaultRulesInDoc(d)) {
        json = "";
        serializeJsonPretty(d, json);
        File fw = LittleFS.open(kTestsPath, "w");
        if (fw) { fw.print(json); fw.close(); }
      }
      char err[120] = "";
      if (!VerificationSteps_activateConfigJson(json.c_str(), err, sizeof(err))) {
        VerificationSteps_activateConfigJson(factoryJson.c_str(), err, sizeof(err));
        File fw = LittleFS.open(kTestsPath, "w");
        if (fw) { fw.print(factoryJson); fw.close(); }
        strncpy(s_testsJson, factoryJson.c_str(), kTestsJsonCap - 1);
        s_testsJson[kTestsJsonCap - 1] = '\0';
      } else {
        strncpy(s_testsJson, json.c_str(), kTestsJsonCap - 1);
        s_testsJson[kTestsJsonCap - 1] = '\0';
      }
    }
  } else {
    char err[120] = "";
    VerificationSteps_activateConfigJson(factoryJson.c_str(), err, sizeof(err));
    File fw = LittleFS.open(kTestsPath, "w");
    if (fw) { fw.print(factoryJson); fw.close(); }
    strncpy(s_testsJson, factoryJson.c_str(), kTestsJsonCap - 1);
    s_testsJson[kTestsJsonCap - 1] = '\0';
  }

  if (WifiManager_isConnected()) {
    char ip[20] = "";
    if (WifiManager_getIpString(ip, sizeof(ip)))
      Serial.printf("[Admin] Open http://%s/admin (STA)\n", ip);
  } else {
    Serial.println("[Admin] STA offline — use SoftAP SparkyCheck / connect then http://192.168.4.1/admin");
  }
  Serial.printf("[Admin] init done (heap=%u)\n", (unsigned)ESP.getFreeHeap());

  (void)writeWebLogoBmpToFile();

  s_started = true;
}

static void runPendingHttpPause(void) {
  if (!s_httpPausePending.exchange(false)) return;
  s_server.end();
  delay(120);
  Serial.printf("[Admin] http paused for OTA (loop, heap=%u)\n", (unsigned)ESP.getFreeHeap());
  if (s_otaPauseDoneSem) (void)xSemaphoreGive(s_otaPauseDoneSem);
}

void AdminPortal_pauseForOta(void) {
  const int prev = s_otaHttpPauseDepth.fetch_add(1);
  /* Outer pause owns server stop; nested callers must not bump depth or resume never runs (stuck :80 dead). */
  if (prev != 0) {
    (void)s_otaHttpPauseDepth.fetch_sub(1);
    return;
  }
  if (arduinoOnLoopTask()) {
    s_server.end();
    delay(120);
    Serial.printf("[Admin] http paused for OTA (heap=%u)\n", (unsigned)ESP.getFreeHeap());
    return;
  }
  if (!s_otaPauseDoneSem) s_otaPauseDoneSem = xSemaphoreCreateBinary();
  if (!s_otaPauseDoneSem) {
    s_server.end();
    delay(120);
    Serial.printf("[Admin] http paused for OTA (no sem, sync fallback, heap=%u)\n", (unsigned)ESP.getFreeHeap());
    return;
  }
  s_httpPausePending.store(true);
  Serial.printf("[Admin] http pause deferred to loop (heap=%u)\n", (unsigned)ESP.getFreeHeap());
  (void)xSemaphoreTake(s_otaPauseDoneSem, portMAX_DELAY);
}

void AdminPortal_resumeAfterOta(void) {
  const int after = s_otaHttpPauseDepth.fetch_sub(1) - 1;
  if (after != 0) return;
  Serial.printf("[Admin] http resume scheduled (heap=%u)\n", (unsigned)ESP.getFreeHeap());
  s_httpResumePending.store(true);
}

static void runPendingHttpResume(void) {
  if (!s_httpResumePending.exchange(false)) return;
  Serial.printf("[Admin] http resume on loop (heap=%u)\n", (unsigned)ESP.getFreeHeap());
  tryStartServer();
}

void AdminPortal_tick(void) {
  if (s_emailTestRunning.load()) {
    const unsigned long nowMs = millis();
    const unsigned long elapsed = nowMs - s_emailTestStartMs;
    const unsigned long kEmailTimeoutMs = 45000UL;
    if (elapsed > kEmailTimeoutMs) {
      /* Mark this job as timed out and allow user retries immediately. */
      s_emailTestActiveJobId++;
      s_emailTestRunning.store(false);
      s_emailTestPending.store(false);
      snprintf(s_flashMsg, sizeof(s_flashMsg), "Test email timed out (>45s). Check SMTP credentials/App Password.");
      snprintf(s_emailTestStatus, sizeof(s_emailTestStatus), "Failed: timeout (>45s).");
      s_flashErr = true;
    }
  }
  runPendingHttpPause();
  runPendingHttpResume();
  if (s_pendingHttpRebind.load() && s_otaHttpPauseDepth.load() == 0) {
    (void)s_pendingHttpRebind.exchange(false);
    adminPortalRebindHttpServer();
  }
  if (s_webOtaInstallPending.exchange(false)) {
    if (OtaUpdate_hasPendingUpdate()) OtaUpdate_installPending();
  }
  unsigned long now = millis();
  if ((long)(now - s_nextPortalTickMs) < 0) return;
  s_nextPortalTickMs = now + kPortalTickMs;
  if (WifiManager_isConnected()) stopFallbackAp();
  else startFallbackAp();
}

bool AdminPortal_isApActive(void) {
  return s_apActive;
}

bool AdminPortal_getApSsid(char* buf, unsigned size) {
  if (!buf || size == 0 || !s_apActive) return false;
  strncpy(buf, s_apSsid, size - 1);
  buf[size - 1] = '\0';
  return true;
}

bool AdminPortal_getApPass(char* buf, unsigned size) {
  if (!buf || size == 0 || !s_apActive) return false;
  strncpy(buf, s_apPass, size - 1);
  buf[size - 1] = '\0';
  return true;
}

bool AdminPortal_getApIp(char* buf, unsigned size) {
  if (!buf || size == 0 || !s_apActive) return false;
  IPAddress ip = WiFi.softAPIP();
  snprintf(buf, size, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
  return true;
}

