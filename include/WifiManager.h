/**
 * SparkyCheck – WiFi scan, connect, status. Credentials saved in NVS via AppState.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_MAX_SSIDS 16
#define WIFI_SSID_LEN  33
#define WIFI_PASS_LEN  64

/** Single scan result. */
typedef struct {
  char ssid[WIFI_SSID_LEN];
  int8_t rssi;
  uint8_t secure;  /* 0 = open, else encrypted */
} WifiNetwork;

/** Run scan; returns number of networks (max WIFI_MAX_SSIDS). Fills networks[]. */
int WifiManager_scan(WifiNetwork* networks, int max_count);

/** Connect to ssid with password. Saves to NVS on success. Returns true if connected. */
bool WifiManager_connect(const char* ssid, const char* pass);

/** Disconnect and optionally clear saved credentials. */
void WifiManager_disconnect(bool clear_saved);

/** True if currently connected. */
bool WifiManager_isConnected(void);

/** Copy current IP string into buf (e.g. "192.168.1.5"). Returns false if not connected. */
bool WifiManager_getIpString(char* buf, unsigned buf_size);

/** Copy connected SSID into buf. Returns false if not connected. */
bool WifiManager_getConnectedSsid(char* buf, unsigned buf_size);

/** Try reconnect using saved credentials (call from setup). */
void WifiManager_reconnectSaved(void);

/** Probe internet check endpoint; true means captive portal is likely intercepting traffic. */
bool WifiManager_isCaptivePortalLikely(void);

/** Most recent portal redirect URL (or fallback helper URL). */
bool WifiManager_getPortalUrl(char* buf, unsigned buf_size);

#ifdef __cplusplus
}
#endif
