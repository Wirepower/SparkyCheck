#if defined(SPARKYCHECK_PANEL_43B)

#include "SparkySd43b.h"
#include "SparkyPanelDisplay.h"

#include <Arduino.h>
#include <SPI.h>
#include <SdFat.h>
#include <chip/esp_expander_base.hpp>

/* Waveshare 4.3B: TF SPI on GPIO11/12/13, CS active low on CH422G EXIO4 (digital pin 4). */
static constexpr int kSdSpiMosi = 11;
static constexpr int kSdSpiSck = 12;
static constexpr int kSdSpiMiso = 13;
static constexpr int kSdCsCh422Pin = 4;

static SPIClass s_sdSpi(HSPI);
static SdFat32 s_sd;
static bool s_mounted = false;

void sdCsInit(SdCsPin_t pin) {
  (void)pin;
  auto* ex = SparkyPanelDisplay::ioExpanderBase();
  if (ex) ex->digitalWrite(kSdCsCh422Pin, 1);
}

void sdCsWrite(SdCsPin_t pin, bool level) {
  (void)pin;
  auto* ex = SparkyPanelDisplay::ioExpanderBase();
  if (ex) ex->digitalWrite(kSdCsCh422Pin, level ? 1 : 0);
}

bool SparkySd43b_mount(void) {
  if (s_mounted) return true;
  if (!SparkyPanelDisplay::ioExpanderBase()) return false;

  sdCsInit(0);
  s_sdSpi.begin(kSdSpiSck, kSdSpiMiso, kSdSpiMosi, -1);

  SdSpiConfig cfgFast(/*csPin*/ 0, DEDICATED_SPI, SD_SCK_MHZ(25), &s_sdSpi);
  if (s_sd.begin(cfgFast)) {
    s_mounted = true;
    return true;
  }
  SdSpiConfig cfgSlow(0, DEDICATED_SPI, SD_SCK_MHZ(10), &s_sdSpi);
  if (!s_sd.begin(cfgSlow)) return false;
  s_mounted = true;
  return true;
}

bool SparkySd43b_isMounted(void) {
  return s_mounted;
}

SdFat32& SparkySd43b_volume(void) {
  return s_sd;
}

#endif /* SPARKYCHECK_PANEL_43B */
