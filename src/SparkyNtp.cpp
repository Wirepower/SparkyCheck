#include "SparkyNtp.h"
#include "AppState.h"
#include "SparkyRtc.h"
#include "WifiManager.h"

#include <Arduino.h>
#include <esp_sntp.h>
#include <sys/time.h>
#include <time.h>

static bool s_sntpRunning = false;

static void onSync(struct timeval* tv) {
  (void)tv;
  time_t t = time(nullptr);
  if (t > (time_t)100000) {
    AppState_saveWallClockUtc(t);
    (void)SparkyRtc_writeFromSystemClock();
    Serial.printf("[NTP] sync OK utc=%lld\n", (long long)t);
  }
}

static void startSntp(void) {
  char s1[APP_STATE_NTP_SERVER_LEN];
  char s2[APP_STATE_NTP_SERVER_LEN];
  AppState_getNtpServer1(s1, sizeof(s1));
  AppState_getNtpServer2(s2, sizeof(s2));
  if (!s1[0]) {
    strncpy(s1, "pool.ntp.org", sizeof(s1) - 1);
    s1[sizeof(s1) - 1] = '\0';
  }
  if (!s2[0]) {
    strncpy(s2, "time.google.com", sizeof(s2) - 1);
    s2[sizeof(s2) - 1] = '\0';
  }

  esp_sntp_stop();
  esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
  esp_sntp_setservername(0, s1);
  esp_sntp_setservername(1, s2);
  esp_sntp_set_time_sync_notification_cb(onSync);
  esp_sntp_init();
  s_sntpRunning = true;
  Serial.printf("[NTP] started servers=%s, %s\n", s1, s2);
}

void SparkyNtp_init(void) {
  s_sntpRunning = false;
}

void SparkyNtp_requestRestart(void) {
  if (s_sntpRunning) {
    esp_sntp_stop();
    s_sntpRunning = false;
  }
}

void SparkyNtp_tick(void) {
  if (!WifiManager_isConnected()) {
    if (s_sntpRunning) {
      esp_sntp_stop();
      s_sntpRunning = false;
    }
    return;
  }
  if (!AppState_getNtpEnabled()) {
    if (s_sntpRunning) {
      esp_sntp_stop();
      s_sntpRunning = false;
    }
    return;
  }
  if (!s_sntpRunning) startSntp();
}
