#include "AppState.h"
#include "Standards.h"
#include <Preferences.h>
#include <sys/time.h>
#include <time.h>

static const char* NVS_NAMESPACE   = "sparky";
static const char* NVS_KEY_MODE    = "mode";
static const char* NVS_KEY_PIN    = "pin";
static const char* NVS_KEY_ROT    = "rot";
static const char* NVS_KEY_BUZZ   = "buzz";
static const char* NVS_KEY_CLK12  = "clk_12";
static const char* NVS_KEY_CLK_TZ = "clk_tz";
static const char* NVS_KEY_CLK_DST = "clk_dst";
static const char* NVS_KEY_WALL_UTC = "wall_utc";
static const char* NVS_KEY_WIFI_SSID   = "wifi_ssid";
static const char* NVS_KEY_WIFI_PASS   = "wifi_pass";
static const char* NVS_KEY_ADMIN_AP_SSID = "adm_ap_ssid";
static const char* NVS_KEY_ADMIN_AP_PASS = "adm_ap_pass";
static const char* NVS_KEY_OTA_URL     = "ota_url";
static const char* NVS_KEY_OTA_AUTO    = "ota_auto";
static const char* NVS_KEY_OTA_INSTALL = "ota_inst";
static const char* NVS_KEY_TRSYNC_EN   = "trsync_en";
static const char* NVS_KEY_TRSYNC_URL  = "trsync_url";
static const char* NVS_KEY_TRSYNC_TOK  = "trsync_tok";
static const char* NVS_KEY_TRSYNC_CUB  = "trsync_cub";
static const char* NVS_KEY_TRSYNC_TGT  = "trsync_tgt";
static const char* NVS_KEY_EMAIL_REP   = "email_rep";
static const char* NVS_KEY_DEVICE_ID   = "device_id";
static const char* NVS_KEY_SMTP_SRV    = "smtp_srv";
static const char* NVS_KEY_SMTP_PORT   = "smtp_port";
static const char* NVS_KEY_SMTP_USER   = "smtp_user";
static const char* NVS_KEY_SMTP_PASS   = "smtp_pass";
static const char* NVS_KEY_REPORT_TO   = "report_to";

static AppMode s_mode = APP_MODE_FIELD;
static int s_rotation = 1;   /* 0=portrait, 1=landscape; default landscape */
static bool s_buzzer = true;
static bool s_clock12 = true;
static int16_t s_clockTzOffsetMin = 0;
static bool s_clockDstExtra = false;
static bool s_loaded = false;

void AppState_load(void) {
  if (s_loaded) return;
  Preferences prefs;
  if (prefs.begin(NVS_NAMESPACE, true)) {
    s_mode = (AppMode)prefs.getUChar(NVS_KEY_MODE, (uint8_t)APP_MODE_FIELD);
    s_rotation = prefs.getInt(NVS_KEY_ROT, 1);
    s_buzzer = prefs.getBool(NVS_KEY_BUZZ, true);
    s_clock12 = prefs.getBool(NVS_KEY_CLK12, true);
    prefs.end();
  }
  s_loaded = true;
  Standards_setFieldMode(s_mode == APP_MODE_FIELD);
}

void AppState_save(void) {
  Preferences prefs;
  if (prefs.begin(NVS_NAMESPACE, false)) {
    prefs.putUChar(NVS_KEY_MODE, (uint8_t)s_mode);
    prefs.end();
  }
}

AppMode AppState_getMode(void) {
  if (!s_loaded) AppState_load();
  return s_mode;
}

void AppState_setMode(AppMode mode) {
  s_mode = mode;
  Standards_setFieldMode(mode == APP_MODE_FIELD);
  AppState_save();
}

bool AppState_isFieldMode(void) {
  return AppState_getMode() == APP_MODE_FIELD;
}

bool AppState_checkPin(uint32_t pin) {
  return pin == AppState_getPin();
}

uint32_t AppState_getPin(void) {
  if (!s_loaded) AppState_load();
  Preferences prefs;
  if (prefs.begin(NVS_NAMESPACE, true)) {
    uint32_t p = prefs.getULong(NVS_KEY_PIN, (uint32_t)APP_STATE_DEFAULT_PIN);
    prefs.end();
    return p;
  }
  return (uint32_t)APP_STATE_DEFAULT_PIN;  /* 12345 */
}

void AppState_setPin(uint32_t pin) {
  Preferences prefs;
  if (prefs.begin(NVS_NAMESPACE, false)) {
    prefs.putULong(NVS_KEY_PIN, pin);
    prefs.end();
  }
}

int AppState_getRotation(void) {
  if (!s_loaded) AppState_load();
  return s_rotation;
}

void AppState_setRotation(int rotation) {
  s_rotation = (rotation != 0) ? 1 : 0;
  Preferences prefs;
  if (prefs.begin(NVS_NAMESPACE, false)) {
    prefs.putInt(NVS_KEY_ROT, s_rotation);
    prefs.end();
  }
}

bool AppState_getBuzzerEnabled(void) {
  if (!s_loaded) AppState_load();
  return s_buzzer;
}

void AppState_setBuzzerEnabled(bool on) {
  s_buzzer = on;
  Preferences prefs;
  if (prefs.begin(NVS_NAMESPACE, false)) {
    prefs.putBool(NVS_KEY_BUZZ, on);
    prefs.end();
  }
}

bool AppState_getClock12Hour(void) {
  if (!s_loaded) AppState_load();
  return s_clock12;
}

void AppState_setClock12Hour(bool on) {
  s_clock12 = on;
  Preferences prefs;
  if (prefs.begin(NVS_NAMESPACE, false)) {
    prefs.putBool(NVS_KEY_CLK12, on);
    prefs.end();
  }
}

int16_t AppState_getClockTzOffsetMinutes(void) {
  if (!s_loaded) AppState_load();
  return s_clockTzOffsetMin;
}

void AppState_setClockTzOffsetMinutes(int16_t minutes) {
  if (minutes < -900) minutes = -900;
  if (minutes > 900) minutes = 900;
  s_clockTzOffsetMin = minutes;
  Preferences prefs;
  if (prefs.begin(NVS_NAMESPACE, false)) {
    prefs.putInt(NVS_KEY_CLK_TZ, (int)minutes);
    prefs.end();
  }
}

bool AppState_getClockDstExtraHour(void) {
  if (!s_loaded) AppState_load();
  return s_clockDstExtra;
}

void AppState_setClockDstExtraHour(bool on) {
  s_clockDstExtra = on;
  Preferences prefs;
  if (prefs.begin(NVS_NAMESPACE, false)) {
    prefs.putBool(NVS_KEY_CLK_DST, on);
    prefs.end();
  }
}

void AppState_saveWallClockUtc(time_t utc) {
  if (utc < (time_t)100000) return;
  uint64_t v = (uint64_t)utc;
  Preferences prefs;
  if (prefs.begin(NVS_NAMESPACE, false)) {
    prefs.putBytes(NVS_KEY_WALL_UTC, &v, sizeof(v));
    prefs.end();
  }
}

void AppState_applySavedWallClockIfInvalid(void) {
  time_t now = time(nullptr);
  if (now >= (time_t)100000) return;
  Preferences prefs;
  if (!prefs.begin(NVS_NAMESPACE, true)) return;
  uint64_t v = 0;
  if (prefs.getBytesLength(NVS_KEY_WALL_UTC) == sizeof(v)) prefs.getBytes(NVS_KEY_WALL_UTC, &v, sizeof(v));
  prefs.end();
  if (v < 100000ULL) return;
  struct timeval tv;
  tv.tv_sec = (time_t)v;
  tv.tv_usec = 0;
  settimeofday(&tv, nullptr);
}

void AppState_getWifiCredentials(char* ssid, unsigned ssid_size, char* pass, unsigned pass_size) {
  if (!ssid || ssid_size == 0) return;
  ssid[0] = '\0';
  if (pass && pass_size) pass[0] = '\0';
  if (!s_loaded) AppState_load();
  Preferences prefs;
  if (prefs.begin(NVS_NAMESPACE, true)) {
    if (prefs.isKey(NVS_KEY_WIFI_SSID)) prefs.getString(NVS_KEY_WIFI_SSID, ssid, ssid_size);
    if (pass && pass_size && prefs.isKey(NVS_KEY_WIFI_PASS)) prefs.getString(NVS_KEY_WIFI_PASS, pass, pass_size);
    prefs.end();
  }
}

void AppState_setWifiCredentials(const char* ssid, const char* pass) {
  Preferences prefs;
  if (prefs.begin(NVS_NAMESPACE, false)) {
    if (ssid) prefs.putString(NVS_KEY_WIFI_SSID, ssid);
    if (pass) prefs.putString(NVS_KEY_WIFI_PASS, pass);
    prefs.end();
  }
}

void AppState_getAdminApCredentials(char* ssid, unsigned ssid_size, char* pass, unsigned pass_size) {
  if (ssid && ssid_size) ssid[0] = '\0';
  if (pass && pass_size) pass[0] = '\0';
  Preferences prefs;
  if (prefs.begin(NVS_NAMESPACE, true)) {
    if (ssid && ssid_size && prefs.isKey(NVS_KEY_ADMIN_AP_SSID)) prefs.getString(NVS_KEY_ADMIN_AP_SSID, ssid, ssid_size);
    if (pass && pass_size && prefs.isKey(NVS_KEY_ADMIN_AP_PASS)) prefs.getString(NVS_KEY_ADMIN_AP_PASS, pass, pass_size);
    prefs.end();
  }
}

void AppState_setAdminApCredentials(const char* ssid, const char* pass) {
  Preferences prefs;
  if (prefs.begin(NVS_NAMESPACE, false)) {
    prefs.putString(NVS_KEY_ADMIN_AP_SSID, ssid ? ssid : "");
    prefs.putString(NVS_KEY_ADMIN_AP_PASS, pass ? pass : "");
    prefs.end();
  }
}

void AppState_getOtaManifestUrl(char* buf, unsigned size) {
  if (!buf || size == 0) return;
  buf[0] = '\0';
  Preferences prefs;
  if (prefs.begin(NVS_NAMESPACE, true)) {
    if (prefs.isKey(NVS_KEY_OTA_URL)) prefs.getString(NVS_KEY_OTA_URL, buf, size);
    prefs.end();
  }
}

void AppState_setOtaManifestUrl(const char* s) {
  Preferences prefs;
  if (prefs.begin(NVS_NAMESPACE, false)) {
    prefs.putString(NVS_KEY_OTA_URL, s ? s : "");
    prefs.end();
  }
}

bool AppState_getOtaAutoCheckEnabled(void) {
  Preferences prefs;
  if (prefs.begin(NVS_NAMESPACE, true)) {
    bool out = prefs.getBool(NVS_KEY_OTA_AUTO, true);
    prefs.end();
    return out;
  }
  return true;
}

void AppState_setOtaAutoCheckEnabled(bool on) {
  Preferences prefs;
  if (prefs.begin(NVS_NAMESPACE, false)) {
    prefs.putBool(NVS_KEY_OTA_AUTO, on);
    prefs.end();
  }
}

bool AppState_getOtaAutoInstallEnabled(void) {
  Preferences prefs;
  if (prefs.begin(NVS_NAMESPACE, true)) {
    bool out = prefs.getBool(NVS_KEY_OTA_INSTALL, true);
    prefs.end();
    return out;
  }
  return true;
}

void AppState_setOtaAutoInstallEnabled(bool on) {
  Preferences prefs;
  if (prefs.begin(NVS_NAMESPACE, false)) {
    prefs.putBool(NVS_KEY_OTA_INSTALL, on);
    prefs.end();
  }
}

static void getStr(Preferences& prefs, const char* key, char* buf, unsigned size) {
  if (!buf || size == 0 || !key) return;
  if (!prefs.isKey(key)) {
    buf[0] = '\0';
    return;
  }
  prefs.getString(key, buf, size);
  buf[size - 1] = '\0';
}

bool AppState_getTrainingSyncEnabled(void) {
  Preferences prefs;
  if (prefs.begin(NVS_NAMESPACE, true)) {
    bool out = prefs.getBool(NVS_KEY_TRSYNC_EN, false);
    prefs.end();
    return out;
  }
  return false;
}

void AppState_setTrainingSyncEnabled(bool on) {
  Preferences prefs;
  if (prefs.begin(NVS_NAMESPACE, false)) {
    prefs.putBool(NVS_KEY_TRSYNC_EN, on);
    prefs.end();
  }
}

void AppState_getTrainingSyncEndpoint(char* buf, unsigned size) {
  if (!buf || size == 0) return;
  buf[0] = '\0';
  Preferences prefs;
  if (prefs.begin(NVS_NAMESPACE, true)) {
    getStr(prefs, NVS_KEY_TRSYNC_URL, buf, size);
    prefs.end();
  }
}

void AppState_setTrainingSyncEndpoint(const char* s) {
  Preferences prefs;
  if (prefs.begin(NVS_NAMESPACE, false)) {
    prefs.putString(NVS_KEY_TRSYNC_URL, s ? s : "");
    prefs.end();
  }
}

void AppState_getTrainingSyncToken(char* buf, unsigned size) {
  if (!buf || size == 0) return;
  buf[0] = '\0';
  Preferences prefs;
  if (prefs.begin(NVS_NAMESPACE, true)) {
    getStr(prefs, NVS_KEY_TRSYNC_TOK, buf, size);
    prefs.end();
  }
}

void AppState_setTrainingSyncToken(const char* s) {
  Preferences prefs;
  if (prefs.begin(NVS_NAMESPACE, false)) {
    prefs.putString(NVS_KEY_TRSYNC_TOK, s ? s : "");
    prefs.end();
  }
}

void AppState_getTrainingSyncCubicleId(char* buf, unsigned size) {
  if (!buf || size == 0) return;
  buf[0] = '\0';
  Preferences prefs;
  if (prefs.begin(NVS_NAMESPACE, true)) {
    getStr(prefs, NVS_KEY_TRSYNC_CUB, buf, size);
    prefs.end();
  }
}

void AppState_setTrainingSyncCubicleId(const char* s) {
  Preferences prefs;
  if (prefs.begin(NVS_NAMESPACE, false)) {
    prefs.putString(NVS_KEY_TRSYNC_CUB, s ? s : "");
    prefs.end();
  }
}

void AppState_getDeviceIdOverride(char* buf, unsigned size) {
  if (!buf || size == 0) return;
  buf[0] = '\0';
  Preferences prefs;
  if (prefs.begin(NVS_NAMESPACE, true)) {
    getStr(prefs, NVS_KEY_DEVICE_ID, buf, size);
    prefs.end();
  }
}

void AppState_setDeviceIdOverride(const char* s) {
  Preferences prefs;
  if (prefs.begin(NVS_NAMESPACE, false)) {
    prefs.putString(NVS_KEY_DEVICE_ID, s ? s : "");
    prefs.end();
  }
}

TrainingSyncTarget AppState_getTrainingSyncTarget(void) {
  Preferences prefs;
  if (prefs.begin(NVS_NAMESPACE, true)) {
    int v = prefs.getInt(NVS_KEY_TRSYNC_TGT, (int)TRAINING_SYNC_TARGET_AUTO);
    prefs.end();
    if (v < (int)TRAINING_SYNC_TARGET_AUTO || v > (int)TRAINING_SYNC_TARGET_SHAREPOINT)
      return TRAINING_SYNC_TARGET_AUTO;
    return (TrainingSyncTarget)v;
  }
  return TRAINING_SYNC_TARGET_AUTO;
}

void AppState_setTrainingSyncTarget(TrainingSyncTarget target) {
  int v = (int)target;
  if (v < (int)TRAINING_SYNC_TARGET_AUTO || v > (int)TRAINING_SYNC_TARGET_SHAREPOINT)
    v = (int)TRAINING_SYNC_TARGET_AUTO;
  Preferences prefs;
  if (prefs.begin(NVS_NAMESPACE, false)) {
    prefs.putInt(NVS_KEY_TRSYNC_TGT, v);
    prefs.end();
  }
}

bool AppState_getEmailReportEnabled(void) {
  Preferences prefs;
  if (prefs.begin(NVS_NAMESPACE, true)) {
    bool out = prefs.getBool(NVS_KEY_EMAIL_REP, false);
    prefs.end();
    return out;
  }
  return false;
}

void AppState_setEmailReportEnabled(bool on) {
  Preferences prefs;
  if (prefs.begin(NVS_NAMESPACE, false)) {
    prefs.putBool(NVS_KEY_EMAIL_REP, on);
    prefs.end();
  }
}

void AppState_getSmtpServer(char* buf, unsigned size) {
  if (!buf || size == 0) return;
  buf[0] = '\0';
  Preferences prefs;
  if (prefs.begin(NVS_NAMESPACE, true)) {
    getStr(prefs, NVS_KEY_SMTP_SRV, buf, size);
    prefs.end();
  }
}

void AppState_setSmtpServer(const char* s) {
  Preferences prefs;
  if (prefs.begin(NVS_NAMESPACE, false)) {
    prefs.putString(NVS_KEY_SMTP_SRV, s ? s : "");
    prefs.end();
  }
}

void AppState_getSmtpPort(char* buf, unsigned size) {
  if (!buf || size == 0) return;
  buf[0] = '\0';
  Preferences prefs;
  if (prefs.begin(NVS_NAMESPACE, true)) {
    getStr(prefs, NVS_KEY_SMTP_PORT, buf, size);
    if (!buf[0]) strncpy(buf, "587", size - 1);
    buf[size - 1] = '\0';
    prefs.end();
  }
}

void AppState_setSmtpPort(const char* s) {
  Preferences prefs;
  if (prefs.begin(NVS_NAMESPACE, false)) {
    prefs.putString(NVS_KEY_SMTP_PORT, s ? s : "587");
    prefs.end();
  }
}

void AppState_getSmtpUser(char* buf, unsigned size) {
  if (!buf || size == 0) return;
  buf[0] = '\0';
  Preferences prefs;
  if (prefs.begin(NVS_NAMESPACE, true)) {
    getStr(prefs, NVS_KEY_SMTP_USER, buf, size);
    prefs.end();
  }
}

void AppState_setSmtpUser(const char* s) {
  Preferences prefs;
  if (prefs.begin(NVS_NAMESPACE, false)) {
    prefs.putString(NVS_KEY_SMTP_USER, s ? s : "");
    prefs.end();
  }
}

void AppState_getSmtpPass(char* buf, unsigned size) {
  if (!buf || size == 0) return;
  buf[0] = '\0';
  Preferences prefs;
  if (prefs.begin(NVS_NAMESPACE, true)) {
    getStr(prefs, NVS_KEY_SMTP_PASS, buf, size);
    prefs.end();
  }
}

void AppState_setSmtpPass(const char* s) {
  Preferences prefs;
  if (prefs.begin(NVS_NAMESPACE, false)) {
    prefs.putString(NVS_KEY_SMTP_PASS, s ? s : "");
    prefs.end();
  }
}

void AppState_getReportToEmail(char* buf, unsigned size) {
  if (!buf || size == 0) return;
  buf[0] = '\0';
  Preferences prefs;
  if (prefs.begin(NVS_NAMESPACE, true)) {
    getStr(prefs, NVS_KEY_REPORT_TO, buf, size);
    prefs.end();
  }
}

void AppState_setReportToEmail(const char* s) {
  Preferences prefs;
  if (prefs.begin(NVS_NAMESPACE, false)) {
    prefs.putString(NVS_KEY_REPORT_TO, s ? s : "");
    prefs.end();
  }
}
