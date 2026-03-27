/**
 * SparkyCheck – Local web admin portal (PIN-protected).
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/** Start local admin portal on port 80. Safe to call once during setup. */
void AdminPortal_init(void);

/** Maintain fallback hotspot state (call regularly from loop). */
void AdminPortal_tick(void);

/** True when fallback AP hotspot is active. */
bool AdminPortal_isApActive(void);

/** Copy fallback AP SSID/password/IP values; returns false if AP inactive. */
bool AdminPortal_getApSsid(char* buf, unsigned size);
bool AdminPortal_getApPass(char* buf, unsigned size);
bool AdminPortal_getApIp(char* buf, unsigned size);

#ifdef __cplusplus
}
#endif

