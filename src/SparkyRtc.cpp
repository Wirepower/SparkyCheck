#include "SparkyRtc.h"
#include <Arduino.h>
#include <Wire.h>
#include <string.h>
#include <sys/time.h>

#if defined(SPARKYCHECK_PANEL_43B)
#define SPARKY_RTC_HAVE_HW 1
#define SPARKY_RTC_SDA 8
#define SPARKY_RTC_SCL 9
#else
#define SPARKY_RTC_HAVE_HW 0
#endif

/* PCF85063A on Waveshare 4.3B is typically 0x51 (7-bit). Probe alternates after touch/display init. */
static const uint8_t kRtcProbeAddrs[] = {0x51, 0x50, 0x52};

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

static bool i2cWriteReg(uint8_t reg, uint8_t val) {
  if (!s_i2cAddr) return false;
  Wire.beginTransmission(s_i2cAddr);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

static bool i2cReadRegs(uint8_t reg, uint8_t* buf, size_t n) {
  if (!buf || n == 0 || !s_i2cAddr) return false;
  Wire.beginTransmission(s_i2cAddr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  size_t got = Wire.requestFrom((int)s_i2cAddr, (int)n);
  if (got != n) return false;
  for (size_t i = 0; i < n; i++) buf[i] = (uint8_t)Wire.read();
  return true;
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

static void pickRtcAddress(void) {
  s_i2cAddr = 0;
  for (size_t i = 0; i < sizeof(kRtcProbeAddrs); i++) {
    uint8_t a = kRtcProbeAddrs[i];
    if (!probeAddress(a)) continue;
    uint8_t sec = 0;
    Wire.beginTransmission(a);
    Wire.write((uint8_t)0x04);
    if (Wire.endTransmission(false) != 0) continue;
    if (Wire.requestFrom((int)a, 1) != 1) continue;
    sec = (uint8_t)Wire.read();
    (void)sec;
    s_i2cAddr = a;
    return;
  }
}

#endif

void SparkyRtc_init(void) {
  if (s_inited) return;
  s_inited = true;
  s_present = false;
  s_i2cAddr = 0;
#if SPARKY_RTC_HAVE_HW
  Wire.begin(SPARKY_RTC_SDA, SPARKY_RTC_SCL);
  Wire.setClock(100000);
  delay(2);
  pickRtcAddress();
  s_present = (s_i2cAddr != 0);
#endif
}

void SparkyRtc_refreshPresence(void) {
#if SPARKY_RTC_HAVE_HW
  Wire.begin(SPARKY_RTC_SDA, SPARKY_RTC_SCL);
  Wire.setClock(100000);
  delay(1);
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
  int fullYear = 2000 + yy;
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
  int year2 = yearFull % 100;
  if (year2 < 0 || year2 > 99) return false;
  int mon = t->tm_mon + 1;
  if (mon < 1 || mon > 12) return false;

  uint8_t ctrl1 = 0;
  if (!i2cReadRegs(0x00, &ctrl1, 1)) return false;
  uint8_t stopOn = (uint8_t)(ctrl1 | (1u << 5));
  if (!i2cWriteReg(0x00, stopOn)) return false;

  uint8_t buf[7];
  buf[0] = (uint8_t)(toBcd((uint8_t)t->tm_sec) & 0x7Fu);
  buf[1] = (uint8_t)(toBcd((uint8_t)t->tm_min) & 0x7Fu);
  buf[2] = (uint8_t)(toBcd((uint8_t)t->tm_hour) & 0x3Fu);
  buf[3] = (uint8_t)(toBcd((uint8_t)t->tm_mday) & 0x3Fu);
  buf[4] = (uint8_t)(t->tm_wday & 0x07);
  uint8_t monthBcd = (uint8_t)(toBcd((uint8_t)mon) & 0x1Fu);
  if (yearFull >= 2000) monthBcd |= 0x80u;
  buf[5] = monthBcd;
  buf[6] = toBcd((uint8_t)year2);

  bool ok = i2cBurstWrite(0x04, buf, sizeof(buf));
  uint8_t stopOff = (uint8_t)(ctrl1 & ~(1u << 5));
  i2cWriteReg(0x00, stopOff);
  return ok;
#endif
}

bool SparkyRtc_syncSystemFromRtc(void) {
  struct tm rtcTm;
  if (!SparkyRtc_readTm(&rtcTm)) return false;
  rtcTm.tm_isdst = -1;
  time_t sec = mktime(&rtcTm);
  if (sec == (time_t)-1) return false;
  struct timeval tv;
  tv.tv_sec = sec;
  tv.tv_usec = 0;
  return settimeofday(&tv, nullptr) == 0;
}

bool SparkyRtc_writeFromSystemClock(void) {
  time_t now = time(nullptr);
  if (now < (time_t)100000) return false;
  struct tm lt;
  if (!localtime_r(&now, &lt)) return false;
  return SparkyRtc_writeTm(&lt);
}
