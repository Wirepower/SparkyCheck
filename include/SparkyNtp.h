/**
 * SNTP client: syncs system time to UTC when Wi-Fi is up (offset/DST still from AppState).
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void SparkyNtp_init(void);

/** Call from loop when Wi-Fi state may change; starts/stops SNTP as needed. */
void SparkyNtp_tick(void);

/** Stop SNTP so next tick restarts with current AppState servers (after admin save). */
void SparkyNtp_requestRestart(void);

#ifdef __cplusplus
}
#endif
