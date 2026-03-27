#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Returns true when battery sensing is configured and a percent is available.
bool BatteryStatus_getPercent(int* outPercent);

#ifdef __cplusplus
}
#endif

