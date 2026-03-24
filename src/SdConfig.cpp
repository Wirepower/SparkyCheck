#include "SdConfig.h"

#include "AppState.h"
#include "WifiManager.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <SD_MMC.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

static bool s_sdAvailable = false;
static bool s_attempted = false;
static char s_status[96] = "SD provisioning idle.";

static const char* kTemplatePath = "/sparkycheck_provisioning_template.json";
static const char* kConfigPath   = "/sparkycheck_provisioning.json";

static void setStatus(const char* s) {
  if (!s) s = "";
  strncpy(s_status, s, sizeof(s_status) - 1);
  s_status[sizeof(s_status) - 1] = '\0';
}

static bool ieq(const char* a, const char* b) {
  if (!a || !b) return false;
  while (*a && *b) {
    char ca = *a, cb = *b;
    if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
    if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
    if (ca != cb) return false;
    a++; b++;
  }
  return *a == '\0' && *b == '\0';
}

static TrainingSyncTarget parseSyncTarget(const char* s) {
  if (!s || !s[0]) return TRAINING_SYNC_TARGET_AUTO;
  if (ieq(s, "google")) return TRAINING_SYNC_TARGET_GOOGLE;
  if (ieq(s, "sharepoint")) return TRAINING_SYNC_TARGET_SHAREPOINT;
  return TRAINING_SYNC_TARGET_AUTO;
}

static bool mountSd(void) {
  if (s_attempted) return s_sdAvailable;
  s_attempted = true;
#ifdef SPARKYCHECK_PANEL_43B
  // Waveshare 4.3B uses TF over SPI with CS via CH422G (EXIO4).
  // Current SD_MMC path is not compatible and can crash on Arduino 3.x.
  s_sdAvailable = false;
  setStatus("SD provisioning disabled on 4.3B build.");
  return false;
#endif
  s_sdAvailable = SD_MMC.begin("/sdcard", true);
  if (!s_sdAvailable) setStatus("SD not mounted.");
  else setStatus("SD mounted.");
  return s_sdAvailable;
}

template <typename TDoc>
static bool writeJsonFile(const char* path, const TDoc& doc) {
  File f = SD_MMC.open(path, FILE_WRITE);
  if (!f) return false;
  if (serializeJsonPretty(doc, f) == 0) { f.close(); return false; }
  f.print("\n");
  f.close();
  return true;
}

static void ensureProvisioningFiles(void) {
  if (!s_sdAvailable) return;

  if (!SD_MMC.exists(kTemplatePath)) {
    StaticJsonDocument<2048> t;
    t["template_version"] = 1;
    t["enabled"] = false;
    t["notes"] = "Copy this file to sparkycheck_provisioning.json and set enabled=true to apply on boot.";

    JsonObject s = t.createNestedObject("settings");
    s["mode"] = "training";
    s["rotation"] = 1;
    s["buzzer_enabled"] = true;

    JsonObject wifi = s.createNestedObject("wifi");
    wifi["ssid"] = "YOUR_WIFI_SSID";
    wifi["pass"] = "YOUR_WIFI_PASSWORD";

    JsonObject ota = s.createNestedObject("ota");
    ota["manifest_url"] = "";
    ota["auto_check"] = true;
    ota["auto_install"] = true;

    JsonObject rep = s.createNestedObject("reporting");
    rep["email_enabled"] = false;

    JsonObject smtp = rep.createNestedObject("smtp");
    smtp["server"] = "";
    smtp["port"] = "587";
    smtp["user"] = "";
    smtp["pass"] = "";
    smtp["to"] = "";

    JsonObject cloud = rep.createNestedObject("cloud_sync");
    cloud["enabled"] = false;
    cloud["target"] = "auto";
    cloud["endpoint_url"] = "";
    cloud["auth_token"] = "";
    cloud["cubicle_id"] = "CUB-01";
    cloud["device_id_label"] = "CUB-01";

    writeJsonFile(kTemplatePath, t);
  }

  if (!SD_MMC.exists(kConfigPath)) {
    StaticJsonDocument<1024> c;
    c["enabled"] = false;
    c["settings"]["mode"] = "training";
    c["settings"]["reporting"]["cloud_sync"]["enabled"] = false;
    c["settings"]["reporting"]["email_enabled"] = false;
    c["notes"] = "Set enabled=true after editing values.";
    writeJsonFile(kConfigPath, c);
  }
}

static void applyBoolIfPresent(JsonObjectConst obj, const char* key, bool (*getter)(void), void (*setter)(bool)) {
  if (!obj.containsKey(key)) return;
  bool v = obj[key].as<bool>();
  if (getter() != v) setter(v);
}

static void applyIntIfPresent(JsonObjectConst obj, const char* key, int (*getter)(void), void (*setter)(int)) {
  if (!obj.containsKey(key)) return;
  int v = obj[key].as<int>();
  if (getter() != v) setter(v);
}

static void applyStringIfPresent(JsonObjectConst obj, const char* key,
                                 void (*getter)(char*, unsigned),
                                 void (*setter)(const char*),
                                 unsigned maxLen) {
  if (!obj.containsKey(key)) return;
  const char* v = obj[key].as<const char*>();
  if (!v) v = "";
  static char cur[256];
  if (maxLen > sizeof(cur)) maxLen = sizeof(cur);
  getter(cur, maxLen);
  if (strncmp(cur, v, maxLen - 1) != 0) setter(v);
}

static void applyProvisioning(void) {
  File f = SD_MMC.open(kConfigPath, FILE_READ);
  if (!f) { setStatus("Provisioning file not found."); return; }

  StaticJsonDocument<3072> doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) { setStatus("Provisioning JSON parse failed."); return; }
  if (!(doc["enabled"] | false)) { setStatus("Provisioning disabled in file."); return; }

  JsonObjectConst s = doc["settings"].as<JsonObjectConst>();
  if (s.isNull()) { setStatus("Provisioning missing settings object."); return; }

  if (s.containsKey("mode")) {
    const char* m = s["mode"] | "";
    if (ieq(m, "field")) AppState_setMode(APP_MODE_FIELD);
    else if (ieq(m, "training")) AppState_setMode(APP_MODE_TRAINING);
  }
  applyIntIfPresent(s, "rotation", AppState_getRotation, AppState_setRotation);
  applyBoolIfPresent(s, "buzzer_enabled", AppState_getBuzzerEnabled, AppState_setBuzzerEnabled);

  JsonObjectConst wifi = s["wifi"].as<JsonObjectConst>();
  if (!wifi.isNull() && (wifi.containsKey("ssid") || wifi.containsKey("pass"))) {
    char ssid[WIFI_SSID_LEN], pass[WIFI_PASS_LEN];
    AppState_getWifiCredentials(ssid, sizeof(ssid), pass, sizeof(pass));
    if (wifi.containsKey("ssid")) {
      const char* v = wifi["ssid"] | "";
      strncpy(ssid, v, sizeof(ssid) - 1); ssid[sizeof(ssid) - 1] = '\0';
    }
    if (wifi.containsKey("pass")) {
      const char* v = wifi["pass"] | "";
      strncpy(pass, v, sizeof(pass) - 1); pass[sizeof(pass) - 1] = '\0';
    }
    AppState_setWifiCredentials(ssid, pass);
  }

  JsonObjectConst ota = s["ota"].as<JsonObjectConst>();
  if (!ota.isNull()) {
    applyStringIfPresent(ota, "manifest_url", AppState_getOtaManifestUrl, AppState_setOtaManifestUrl, APP_STATE_OTA_URL_LEN);
    applyBoolIfPresent(ota, "auto_check", AppState_getOtaAutoCheckEnabled, AppState_setOtaAutoCheckEnabled);
    applyBoolIfPresent(ota, "auto_install", AppState_getOtaAutoInstallEnabled, AppState_setOtaAutoInstallEnabled);
  }

  JsonObjectConst reporting = s["reporting"].as<JsonObjectConst>();
  if (!reporting.isNull()) {
    applyBoolIfPresent(reporting, "email_enabled", AppState_getEmailReportEnabled, AppState_setEmailReportEnabled);

    JsonObjectConst smtp = reporting["smtp"].as<JsonObjectConst>();
    if (!smtp.isNull()) {
      applyStringIfPresent(smtp, "server", AppState_getSmtpServer, AppState_setSmtpServer, APP_STATE_EMAIL_STR_LEN);
      applyStringIfPresent(smtp, "port", AppState_getSmtpPort, AppState_setSmtpPort, APP_STATE_EMAIL_STR_LEN);
      applyStringIfPresent(smtp, "user", AppState_getSmtpUser, AppState_setSmtpUser, APP_STATE_EMAIL_STR_LEN);
      applyStringIfPresent(smtp, "pass", AppState_getSmtpPass, AppState_setSmtpPass, APP_STATE_EMAIL_STR_LEN);
      applyStringIfPresent(smtp, "to", AppState_getReportToEmail, AppState_setReportToEmail, APP_STATE_EMAIL_STR_LEN);
    }

    JsonObjectConst cloud = reporting["cloud_sync"].as<JsonObjectConst>();
    if (!cloud.isNull()) {
      applyBoolIfPresent(cloud, "enabled", AppState_getTrainingSyncEnabled, AppState_setTrainingSyncEnabled);
      applyStringIfPresent(cloud, "endpoint_url", AppState_getTrainingSyncEndpoint, AppState_setTrainingSyncEndpoint, APP_STATE_TRAINING_SYNC_URL_LEN);
      applyStringIfPresent(cloud, "auth_token", AppState_getTrainingSyncToken, AppState_setTrainingSyncToken, APP_STATE_TRAINING_SYNC_TOKEN_LEN);
      applyStringIfPresent(cloud, "cubicle_id", AppState_getTrainingSyncCubicleId, AppState_setTrainingSyncCubicleId, APP_STATE_TRAINING_SYNC_CUBICLE_LEN);
      applyStringIfPresent(cloud, "device_id_label", AppState_getDeviceIdOverride, AppState_setDeviceIdOverride, APP_STATE_DEVICE_ID_LEN);
      if (cloud.containsKey("target")) {
        AppState_setTrainingSyncTarget(parseSyncTarget(cloud["target"] | "auto"));
      }
    }
  }
  setStatus("Provisioning applied from SD.");
}

void SdConfig_initAndApply(void) {
  if (!mountSd()) return;
  ensureProvisioningFiles();
  applyProvisioning();
}

bool SdConfig_isAvailable(void) {
  return s_sdAvailable;
}

void SdConfig_getLastStatus(char* buf, unsigned size) {
  if (!buf || size == 0) return;
  strncpy(buf, s_status, size - 1);
  buf[size - 1] = '\0';
}
