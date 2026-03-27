#include "WifiManager.h"
#include "AppState.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <string.h>

static char s_lastPortalUrl[160] = "http://neverssl.com/";

int WifiManager_scan(WifiNetwork* networks, int max_count) {
  if (!networks || max_count <= 0) return 0;
  int n = WiFi.scanNetworks();
  int out = 0;
  for (int i = 0; i < n && out < max_count; i++) {
    String s = WiFi.SSID(i);
    if (s.length() >= WIFI_SSID_LEN) continue;
    strncpy(networks[out].ssid, s.c_str(), WIFI_SSID_LEN - 1);
    networks[out].ssid[WIFI_SSID_LEN - 1] = '\0';
    networks[out].rssi = WiFi.RSSI(i);
    networks[out].secure = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN) ? 1 : 0;
    out++;
  }
  return out;
}

bool WifiManager_connect(const char* ssid, const char* pass) {
  if (!ssid || !ssid[0]) return false;
  WiFi.disconnect(true);
  delay(100);
  WiFi.mode(WIFI_STA);
  if (pass && pass[0])
    WiFi.begin(ssid, pass);
  else
    WiFi.begin(ssid);
  for (int i = 0; i < 50; i++) {
    if (WiFi.status() == WL_CONNECTED) {
      AppState_setWifiCredentials(ssid, pass ? pass : "");
      return true;
    }
    delay(200);
  }
  return false;
}

void WifiManager_disconnect(bool clear_saved) {
  WiFi.disconnect(true);
  if (clear_saved) AppState_setWifiCredentials("", "");
}

bool WifiManager_isConnected(void) {
  return WiFi.status() == WL_CONNECTED;
}

bool WifiManager_getIpString(char* buf, unsigned buf_size) {
  if (!buf || buf_size == 0 || WiFi.status() != WL_CONNECTED) return false;
  IPAddress ip = WiFi.localIP();
  snprintf(buf, buf_size, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
  return true;
}

bool WifiManager_getConnectedSsid(char* buf, unsigned buf_size) {
  if (!buf || buf_size == 0 || WiFi.status() != WL_CONNECTED) return false;
  strncpy(buf, WiFi.SSID().c_str(), buf_size - 1);
  buf[buf_size - 1] = '\0';
  return true;
}

void WifiManager_reconnectSaved(void) {
  char ssid[WIFI_SSID_LEN], pass[WIFI_PASS_LEN];
  AppState_getWifiCredentials(ssid, sizeof(ssid), pass, sizeof(pass));
  if (ssid[0] && WiFi.status() != WL_CONNECTED)
    WifiManager_connect(ssid, pass[0] ? pass : nullptr);
}

bool WifiManager_isCaptivePortalLikely(void) {
  if (WiFi.status() != WL_CONNECTED) return false;
  HTTPClient http;
  static const char* kProbeUrl = "http://connectivitycheck.gstatic.com/generate_204";
  static const char* hdrs[] = { "Location" };
  s_lastPortalUrl[0] = '\0';
  http.setConnectTimeout(3000);
  http.setTimeout(4000);
  if (!http.begin(kProbeUrl)) return false;
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  http.collectHeaders(hdrs, 1);
  int code = http.GET();
  String loc = http.header("Location");
  http.end();
  if (loc.length() > 0) {
    strncpy(s_lastPortalUrl, loc.c_str(), sizeof(s_lastPortalUrl) - 1);
    s_lastPortalUrl[sizeof(s_lastPortalUrl) - 1] = '\0';
  } else {
    strncpy(s_lastPortalUrl, "http://neverssl.com/", sizeof(s_lastPortalUrl) - 1);
    s_lastPortalUrl[sizeof(s_lastPortalUrl) - 1] = '\0';
  }
  if (code == 204) return false;
  if (code >= 300 && code < 400) return true;
  return code == 200;
}

bool WifiManager_getPortalUrl(char* buf, unsigned buf_size) {
  if (!buf || buf_size == 0) return false;
  const char* src = s_lastPortalUrl[0] ? s_lastPortalUrl : "http://neverssl.com/";
  strncpy(buf, src, buf_size - 1);
  buf[buf_size - 1] = '\0';
  return true;
}
