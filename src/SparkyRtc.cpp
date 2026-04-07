#include "SparkyRtc.h"
#include "SparkyTime.h"
#include <Arduino.h>
#include <Wire.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

#if defined(SPARKYCHECK_PANEL_43B)
#define SPARKY_RTC_HAVE_HW 1
#define SPARKY_RTC_SDA 8
#define SPARKY_RTC_SCL 9
/* Slower clock is more reliable when sharing GPIO8/9 with GT911 after panel init. */
#define SPARKY_RTC_I2C_HZ 100000
#else
#define SPARKY_RTC_HAVE_HW 0
#endif

/* PCF85063A 7-bit address on Waveshare 4.3B demo (waveshare_pcf85063a.h). */
static const uint8_t kRtcI2cAddr7 = 0x51;

/* Year in chip registers is 0–99 meaning 1970–2069 (Waveshare YEAR_OFFSET = 1970). */
static const int kRtcYearOffset = 1970;

/* Reg 0x00: same default as Waveshare PCF85063A_Init — 12.5 pF load, clock running. */
static const uint8_t kRtcCtrl1Init = 0x01u; /* RTC_CTRL_1_DEFAULT | RTC_CTRL_1_CAP_SEL */

static bool s_inited = false;
static bool s_present = false;
static uint8_t s_i2cAddr = 0;

static uint8_t toBcd(uint8_t v) {
  return (uint8_t)(((v / 10) << 4) | (v % 10));
}

static uint8_t fromBcd(uint8_t v) {
  return (uint8_t)(((v >> 4) * 10) + (v & 0x0Fu));
}

#if SPARKY_RTC_HAVE_HW

static bool i2cReadRegsAt(uint8_t addr, uint8_t reg, uint8_t* buf, size_t n) {
  if (!buf || n == 0 || !addr) return false;
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  size_t got = Wire.requestFrom((int)addr, (int)n);
  if (got != n) return false;
  for (size_t i = 0; i < n; i++) buf[i] = (uint8_t)Wire.read();
  return true;
}

static bool i2cReadRegs(uint8_t reg, uint8_t* buf, size_t n) {
  return i2cReadRegsAt(s_i2cAddr, reg, buf, n);
}

static bool i2cBurstWrite(uint8_t reg, const uint8_t* data, size_t n) {
  if (!s_i2cAddr) return false;
  Wire.beginTransmission(s_i2cAddr);
  Wire.write(reg);
  for (size_t i = 0; i < n; i++) Wire.write(data[i]);
  return Wire.endTransmission() == 0;
}

static bool probeAddress(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

/** PCF85063 time registers: plausible BCD ranges (not strict on weekday/month). */
static bool raw7LooksLikeRtc(const uint8_t* raw) {
  if (!raw) return false;
  int s = fromBcd((uint8_t)(raw[0] & 0x7Fu));
  int m = fromBcd((uint8_t)(raw[1] & 0x7Fu));
  int d = fromBcd((uint8_t)(raw[3] & 0x3Fu));
  return s >= 0 && s <= 59 && m >= 0 && m <= 59 && d >= 1 && d <= 31;
}

static bool trySelectRtcAt(uint8_t a) {
  if (!probeAddress(a)) return false;
  uint8_t raw[7];
  if (!i2cReadRegsAt(a, 0x04, raw, sizeof(raw))) return false;
  if (raw7LooksLikeRtc(raw)) {
    s_i2cAddr = a;
    return true;
  }
  /* PCF85063 at 0x51 can read as invalid BCD until first valid load (battery / fresh part). */
  if (a == kRtcI2cAddr7) {
    s_i2cAddr = a;
    return true;
  }
  return false;
}

static void pickRtcAddress(void) {
  s_i2cAddr = 0;
  if (trySelectRtcAt(kRtcI2cAddr7)) return;
  /* Some boards tie A0 differently; keep narrow fallback. */
  if (trySelectRtcAt(0x50)) return;
  trySelectRtcAt(0x52);
}

static bool rtcApplyWaveshareCtrl1Init(void) {
  if (!s_i2cAddr) return false;
  Wire.beginTransmission(s_i2cAddr);
  Wire.write((uint8_t)0x00);
  Wire.write(kRtcCtrl1Init);
  return Wire.endTransmission() == 0;
}

/**
 * Waveshare 4.3B: PCF85063 /INT and the VBAT divider both tie to GPIO6. If Control_2 enables an alarm,
 * minute IRQ, or CLKOUT on that pin, it pulls the line and battery ADC reads ~0% while the pack is fine.
 * We only use I2C for time — force Control_2 = 0 (defaults: no IRQ, no CLKOUT on /INT).
 */
static bool rtcIntPinHighZForSharedBatterySense(void) {
  if (!s_i2cAddr) return false;
  const uint8_t ctrl2Off = 0x00u;
  return i2cBurstWrite(0x01, &ctrl2Off, 1);
}

static void rtcBusProbe(void) {
  Wire.setPins(SPARKY_RTC_SDA, SPARKY_RTC_SCL);
#if defined(SPARKYCHECK_PANEL_43B)
  /* Wire.begin() already done in SparkyRtc_earlyInitSharedI2c() before panel init. */
#else
  Wire.begin();
#endif
  Wire.setClock(SPARKY_RTC_I2C_HZ);
  delay(4);
  pickRtcAddress();
  s_present = (s_i2cAddr != 0);
  if (s_present) {
    rtcApplyWaveshareCtrl1Init();
    (void)rtcIntPinHighZForSharedBatterySense();
  }
}

extern "C" void SparkyRtc_releaseBatteryAdcSharedPin(void) {
  if (!s_inited || !s_i2cAddr) return;
  Wire.setPins(SPARKY_RTC_SDA, SPARKY_RTC_SCL);
  Wire.setClock(SPARKY_RTC_I2C_HZ);
  (void)rtcIntPinHighZForSharedBatterySense();
}

#endif

#if defined(SPARKYCHECK_PANEL_43B)
extern "C" void SparkyRtc_earlyInitSharedI2c(void) {
  Wire.setPins(SPARKY_RTC_SDA, SPARKY_RTC_SCL);
  Wire.begin();
  Wire.setClock(400000);
}
#endif

void SparkyRtc_init(void) {
  if (s_inited) return;
  s_inited = true;
  s_present = false;
  s_i2cAddr = 0;
#if SPARKY_RTC_HAVE_HW
  /* Shared I2C on GPIO8/9: Wire.begin() runs before tft.init() on 4.3B (see SparkyRtc_earlyInitSharedI2c). */
  rtcBusProbe();
#endif
}

void SparkyRtc_refreshPresence(void) {
#if SPARKY_RTC_HAVE_HW
  if (!s_inited) {
    SparkyRtc_init();
    return;
  }
  /* Never call Wire.begin() here — it resets the I2C driver and breaks GT911 touch. */
  pickRtcAddress();
  s_present = (s_i2cAddr != 0);
#endif
}

bool SparkyRtc_isPresent(void) {
  if (!s_inited) SparkyRtc_init();
  return s_present;
}

bool SparkyRtc_readTm(struct tm* out) {
  if (!out) return false;
  memset(out, 0, sizeof(*out));
#if !SPARKY_RTC_HAVE_HW
  return false;
#else
  if (!SparkyRtc_isPresent()) return false;
  uint8_t raw[7];
  if (!i2cReadRegs(0x04, raw, sizeof(raw))) return false;

  out->tm_sec = fromBcd((uint8_t)(raw[0] & 0x7Fu));
  out->tm_min = fromBcd((uint8_t)(raw[1] & 0x7Fu));

  uint8_t hreg = raw[2];
  if (hreg & 0x40u) {
    bool pm = (hreg & 0x20u) != 0;
    uint8_t h12 = fromBcd((uint8_t)(hreg & 0x1Fu));
    int hr;
    if (h12 == 12)
      hr = pm ? 12 : 0;
    else
      hr = (int)h12 + (pm ? 12 : 0);
    out->tm_hour = hr;
  } else {
    out->tm_hour = fromBcd((uint8_t)(hreg & 0x3Fu));
  }

  out->tm_mday = fromBcd((uint8_t)(raw[3] & 0x3Fu));
  out->tm_wday = (int)(raw[4] & 0x07u);

  uint8_t mreg = raw[5];
  out->tm_mon = (int)fromBcd((uint8_t)(mreg & 0x1Fu)) - 1;
  if (out->tm_mon < 0 || out->tm_mon > 11) out->tm_mon = 0;

  int yy = (int)fromBcd(raw[6]);
  int fullYear = kRtcYearOffset + yy;
  out->tm_year = fullYear - 1900;

  out->tm_isdst = -1;
  return true;
#endif
}

bool SparkyRtc_writeTm(const struct tm* t) {
  if (!t) return false;
#if !SPARKY_RTC_HAVE_HW
  (void)t;
  return false;
#else
  if (!SparkyRtc_isPresent()) return false;
  int yearFull = t->tm_year + 1900;
  int yearReg = yearFull - kRtcYearOffset;
  if (yearReg < 0 || yearReg > 99) return false;
  int mon = t->tm_mon + 1;
  if (mon < 1 || mon > 12) return false;

  /* Same burst layout as Waveshare PCF85063A_Set_All (no STOP toggle in their demo). */
  uint8_t buf[7];
  buf[0] = (uint8_t)(toBcd((uint8_t)t->tm_sec) & 0x7Fu);
  buf[1] = (uint8_t)(toBcd((uint8_t)t->tm_min) & 0x7Fu);
  buf[2] = (uint8_t)(toBcd((uint8_t)t->tm_hour) & 0x3Fu);
  buf[3] = (uint8_t)(toBcd((uint8_t)t->tm_mday) & 0x3Fu);
  buf[4] = (uint8_t)(t->tm_wday & 0x07);
  buf[5] = (uint8_t)(toBcd((uint8_t)mon) & 0x1Fu);
  buf[6] = toBcd((uint8_t)yearReg);

  return i2cBurstWrite(0x04, buf, sizeof(buf));
#endif
}

bool SparkyRtc_syncSystemFromRtc(void) {
  struct tm rtcTm;
  if (!SparkyRtc_readTm(&rtcTm)) return false;
  int y = rtcTm.tm_year + 1900;
  return SparkyTime_setSystemUtcFromWallFields(rtcTm.tm_mday, rtcTm.tm_mon + 1, y, rtcTm.tm_hour, rtcTm.tm_min,
                                                rtcTm.tm_sec, false);
}

bool SparkyRtc_writeFromSystemClock(void) {
  time_t now = time(nullptr);
  if (now < (time_t)100000) return false;
  /* Match on-screen wall clock (TZ offset + DST), not libc localtime (TZ often unset on ESP32). */
  time_t w = SparkyTime_utcToWallTime(now);
  struct tm wall;
  if (!gmtime_r(&w, &wall)) return false;
  return SparkyRtc_writeTm(&wall);
}
