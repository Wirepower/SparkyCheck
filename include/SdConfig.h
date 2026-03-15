#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Mount SD, create provisioning templates if missing, and apply enabled config. */
void SdConfig_initAndApply(void);

/** True if SD card is mounted and accessible. */
bool SdConfig_isAvailable(void);

/** Last SD provisioning status text for debugging/UI logs. */
void SdConfig_getLastStatus(char* buf, unsigned size);

#ifdef __cplusplus
}
#endif
