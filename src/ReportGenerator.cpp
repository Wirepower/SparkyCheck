/**
 * Report generation: CSV + HTML to LittleFS. Uses fixed buffers and
 * line-by-line file write to avoid heap fragmentation and OOM on ESP32.
 */
#include "ReportGenerator.h"
#include "AppState.h"
#include "Standards.h"
#include "GoogleSync.h"
#include "EmailTest.h"
#include "SparkyTime.h"
#include <LittleFS.h>
#include <SD_MMC.h>
#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define MAX_RESULTS 24
#define BASENAME_LEN 64

struct Row {
  char name[32];
  char value[24];
  char unit[16];
  bool passed;
  char clause[24];
};

struct ReportEmailWork {
  char textBody[2200];
  char htmlCore[3000];
};

/* Build email bodies in static storage — ~5.2KB on stack in ReportGenerator_end() overflowed loopTask. */
static char s_emailTextBuild[2200];
static char s_emailHtmlBuild[3000];

static ReportEmailWork s_reportEmailWork;
static volatile bool s_reportEmailPending = false;
static volatile bool s_reportEmailInFlight = false;

static void reportEmailTask(void* arg) {
  (void)arg;
  char emErr[160] = "";
  EmailTest_sendCustomNow("SparkyCheck Report", "Verification Report", s_reportEmailWork.textBody, s_reportEmailWork.htmlCore,
                          emErr, sizeof(emErr));
  s_reportEmailInFlight = false;
  vTaskDelete(nullptr);
}

void ReportGenerator_pollDeferredEmail(void) {
  if (!s_reportEmailPending || s_reportEmailInFlight) return;
  s_reportEmailPending = false;
  s_reportEmailInFlight = true;
  if (xTaskCreate(reportEmailTask, "rpt_mail", 12288, nullptr, 1, nullptr) != pdPASS) {
    s_reportEmailInFlight = false;
    s_reportEmailPending = true;
  }
}

static struct {
  bool active;
  char basename[BASENAME_LEN];
  char student_id[24];
  int count;
  Row rows[MAX_RESULTS];
} s_report;

static bool s_fs_ok = false;

bool ReportGenerator_init(void) {
  if (s_fs_ok) return true;
  s_fs_ok = LittleFS.begin(true);
  return s_fs_ok;
}

static void sanitizeBaseChars(char* s) {
  if (!s) return;
  for (; *s; ++s) {
    unsigned char c = (unsigned char)*s;
    if (c <= ' ' || c > 126 || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
      *s = '_';
  }
}

static void trimDots(char* s) {
  if (!s) return;
  while (s[0] == '.') memmove(s, s + 1, strlen(s) + 1);
  size_t n = strlen(s);
  while (n > 0 && s[n - 1] == '.') s[--n] = '\0';
}

static void appendTimeSuffix(char* out, unsigned cap, const char* prefix) {
  time_t now = time(nullptr);
  if (now > 1700000000) {
    struct tm ti;
    localtime_r(&now, &ti);
    if (prefix && prefix[0])
      snprintf(out, cap, "%s_%04d%02d%02d_%02d%02d%02d", prefix, ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday, ti.tm_hour,
               ti.tm_min, ti.tm_sec);
    else
      snprintf(out, cap, "Job_%04d%02d%02d_%02d%02d%02d", ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday, ti.tm_hour, ti.tm_min,
               ti.tm_sec);
  } else {
    if (prefix && prefix[0])
      snprintf(out, cap, "%s_%lu", prefix, (unsigned long)millis());
    else
      snprintf(out, cap, "Job_%lu", (unsigned long)(millis() / 1000));
  }
  out[cap - 1] = '\0';
}

/** "Earth continuity" -> EarthContinuity for filenames. */
static void testNameToSlug(const char* in, char* out, size_t cap) {
  if (!out || cap < 2) {
    if (out) out[0] = '\0';
    return;
  }
  out[0] = '\0';
  if (!in || !in[0]) {
    strncpy(out, "Test", cap - 1);
    out[cap - 1] = '\0';
    return;
  }
  bool capNext = true;
  size_t j = 0;
  for (const char* p = in; *p && j + 1 < cap; ++p) {
    unsigned char c = (unsigned char)*p;
    if (isalnum((int)c)) {
      out[j++] = (char)(capNext ? toupper((int)c) : tolower((int)c));
      capNext = false;
    } else if (c == ' ' || c == '-' || c == '/' || c == '.' || c == ',' || c == '_' || c == '(' || c == ')' || c == '&') {
      capNext = true;
    }
  }
  out[j] = '\0';
  if (j == 0) {
    strncpy(out, "Test", cap - 1);
    out[cap - 1] = '\0';
  }
}

static bool reportCsvOrHtmlExists(const char* base) {
  if (!base || !base[0] || !s_fs_ok) return false;
  char p1[96], p2[96];
  snprintf(p1, sizeof(p1), "/reports/%s.csv", base);
  snprintf(p2, sizeof(p2), "/reports/%s.html", base);
  return LittleFS.exists(p1) || LittleFS.exists(p2);
}

static void makeBasename(char* out, unsigned cap, const char* test_name_or_null) {
  char cand[BASENAME_LEN] = "";
  if (AppState_getMode() == APP_MODE_TRAINING) {
    AppState_getTrainingSyncCubicleId(cand, sizeof(cand));
    if (!cand[0]) AppState_getDeviceIdOverride(cand, sizeof(cand));
    if (!cand[0] && s_report.student_id[0]) strncpy(cand, s_report.student_id, sizeof(cand) - 1);
  } else {
    char dev[GOOGLE_SYNC_DEVICE_ID_LEN];
    GoogleSync_getDeviceId(dev, sizeof(dev));
    if (dev[0]) strncpy(cand, dev, sizeof(cand) - 1);
    if (!cand[0]) AppState_getDeviceIdOverride(cand, sizeof(cand));
  }
  cand[sizeof(cand) - 1] = '\0';
  sanitizeBaseChars(cand);
  trimDots(cand);

  char slug[40] = "";
  testNameToSlug(test_name_or_null, slug, sizeof(slug));
  sanitizeBaseChars(slug);
  trimDots(slug);
  if (strlen(slug) > 32) slug[32] = '\0';

  /* Room for base name + up to 4-digit suffix (…EarthContinuity9999) within cap. */
  const size_t maxTotal = (cap > 1) ? (cap - 1) : 0;
  const size_t maxNumDigits = 4;
  size_t maxPrefixChars = (maxTotal > maxNumDigits) ? (maxTotal - maxNumDigits) : maxTotal;
  while (strlen(cand) > 28) cand[strlen(cand) - 1] = '\0';

  char prefix[BASENAME_LEN + 8] = "";
  if (cand[0])
    snprintf(prefix, sizeof(prefix), "%s_%s", cand, slug);
  else
    snprintf(prefix, sizeof(prefix), "Job_%s", slug);

  sanitizeBaseChars(prefix);
  trimDots(prefix);
  while (strlen(prefix) > maxPrefixChars) prefix[strlen(prefix) - 1] = '\0';
  while (prefix[0] && prefix[strlen(prefix) - 1] == '_') prefix[strlen(prefix) - 1] = '\0';

  if (!prefix[0]) {
    appendTimeSuffix(out, cap, cand[0] ? cand : NULL);
    return;
  }

  if (!reportCsvOrHtmlExists(prefix)) {
    strncpy(out, prefix, cap - 1);
    out[cap - 1] = '\0';
    return;
  }
  for (int n = 1; n < 10000; n++) {
    char withNum[BASENAME_LEN + 16];
    snprintf(withNum, sizeof(withNum), "%s%d", prefix, n);
    if (strlen(withNum) > maxTotal) continue;
    if (!reportCsvOrHtmlExists(withNum)) {
      strncpy(out, withNum, cap - 1);
      out[cap - 1] = '\0';
      return;
    }
  }
  appendTimeSuffix(out, cap, cand[0] ? cand : NULL);
}

bool ReportGenerator_begin(const char* job_id_or_null, const char* test_name_or_null) {
  if (!s_fs_ok && !ReportGenerator_init()) return false;
  s_report.active = true;
  s_report.count = 0;
  if (job_id_or_null && job_id_or_null[0])
    strncpy(s_report.basename, job_id_or_null, BASENAME_LEN - 1);
  else
    makeBasename(s_report.basename, BASENAME_LEN, test_name_or_null);
  s_report.basename[BASENAME_LEN - 1] = '\0';
  return true;
}

void ReportGenerator_setStudentId(const char* student_id) {
  strncpy(s_report.student_id, student_id ? student_id : "", sizeof(s_report.student_id) - 1);
  s_report.student_id[sizeof(s_report.student_id) - 1] = '\0';
}

void ReportGenerator_addResult(const char* name, const char* value, const char* unit, bool passed, const char* clause) {
  if (!s_report.active || s_report.count >= MAX_RESULTS) return;
  Row* r = &s_report.rows[s_report.count++];
  strncpy(r->name, name ? name : "", sizeof(r->name) - 1);
  r->name[sizeof(r->name) - 1] = '\0';
  strncpy(r->value, value ? value : "", sizeof(r->value) - 1);
  r->value[sizeof(r->value) - 1] = '\0';
  strncpy(r->unit, unit ? unit : "", sizeof(r->unit) - 1);
  r->unit[sizeof(r->unit) - 1] = '\0';
  r->passed = passed;
  strncpy(r->clause, clause ? clause : "", sizeof(r->clause) - 1);
  r->clause[sizeof(r->clause) - 1] = '\0';
}

#define LINE_BUF_SIZE 256

static bool writeLine(File& f, const char* line) {
  size_t n = strlen(line);
  return f.write((const uint8_t*)line, n) == n;
}

static bool writeReportToFs(fs::FS& fs, const char* dir, const char* basename,
                            const char* device_id, const char* student_id,
                            const char* rules_ver, Row* rows, int count) {
  if (!fs.exists(dir) && !fs.mkdir(dir)) return false;

  char path[96];
  char line[LINE_BUF_SIZE];

  snprintf(path, sizeof(path), "%s/%s.csv", dir, basename);
  File fc = fs.open(path, "w");
  if (!fc) return false;
  snprintf(line, sizeof(line), "Device ID,%s\r\n", device_id ? device_id : "");
  if (!writeLine(fc, line)) { fc.close(); return false; }
  {
    char gen[56];
    SparkyTime_formatPreferred(gen, sizeof(gen));
    snprintf(line, sizeof(line), "Generated,%s\r\n", gen);
    if (!writeLine(fc, line)) { fc.close(); return false; }
  }
  if (student_id && student_id[0]) {
    snprintf(line, sizeof(line), "Student ID,%s\r\n", student_id);
    if (!writeLine(fc, line)) { fc.close(); return false; }
  }
  if (!writeLine(fc, "Test,Value,Unit,Result,Clause\r\n")) { fc.close(); return false; }
  for (int i = 0; i < count; i++) {
    Row* r = &rows[i];
    snprintf(line, sizeof(line), "%s,%s,%s,%s,%s\r\n", r->name, r->value, r->unit,
             r->passed ? "PASS" : "FAIL", r->clause);
    if (!writeLine(fc, line)) { fc.close(); return false; }
  }
  fc.close();

  snprintf(path, sizeof(path), "%s/%s.html", dir, basename);
  File fh = fs.open(path, "w");
  if (!fh) return false;
  if (!writeLine(fh, "<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><title>SparkyCheck Report</title>")) { fh.close(); return false; }
  if (!writeLine(fh, "<style>body{font-family:sans-serif;margin:1rem;} table{border-collapse:collapse;} th,td{border:1px solid #333;padding:6px 10px;} .pass{background:#cfc;} .fail{background:#fcc;}</style></head><body>")) { fh.close(); return false; }
  {
    char gen[56];
    SparkyTime_formatPreferred(gen, sizeof(gen));
    if (student_id && student_id[0])
      snprintf(line, sizeof(line),
               "<h1>SparkyCheck Verification Report</h1><p><strong>Job:</strong> %s &nbsp; <strong>Device:</strong> %s &nbsp; <strong>Student:</strong> %s &nbsp; <strong>Rules:</strong> %s &nbsp; <strong>Generated:</strong> %s</p>",
               basename, device_id ? device_id : "", student_id, rules_ver ? rules_ver : "", gen);
    else
      snprintf(line, sizeof(line),
               "<h1>SparkyCheck Verification Report</h1><p><strong>Job:</strong> %s &nbsp; <strong>Device:</strong> %s &nbsp; <strong>Rules:</strong> %s &nbsp; <strong>Generated:</strong> %s</p>",
               basename, device_id ? device_id : "", rules_ver ? rules_ver : "", gen);
  }
  if (!writeLine(fh, line)) { fh.close(); return false; }
  if (!writeLine(fh, "<table><tr><th>Test</th><th>Value</th><th>Unit</th><th>Result</th><th>Clause</th></tr>")) { fh.close(); return false; }
  for (int i = 0; i < count; i++) {
    Row* r = &rows[i];
    snprintf(line, sizeof(line), "<tr class=\"%s\"><td>%s</td><td>%s</td><td>%s</td><td>%s</td><td>%s</td></tr>",
             r->passed ? "pass" : "fail", r->name, r->value, r->unit, r->passed ? "PASS" : "FAIL", r->clause);
    if (!writeLine(fh, line)) { fh.close(); return false; }
  }
  { char foot[160];
    Standards_getReportFooterStandardsLine(foot, sizeof(foot));
    snprintf(line, sizeof(line), "</table><p><em>%s</em></p></body></html>", foot);
    if (!writeLine(fh, line)) { fh.close(); return false; }
  }
  fh.close();
  return true;
}

bool ReportGenerator_end(void) {
  if (!s_report.active) return false;

  char rules_ver[16];
  char device_id[GOOGLE_SYNC_DEVICE_ID_LEN];
  Standards_getRulesVersion(rules_ver, sizeof(rules_ver));
  GoogleSync_getDeviceId(device_id, sizeof(device_id));

  if (!writeReportToFs(LittleFS, "/reports", s_report.basename, device_id,
                       s_report.student_id, rules_ver, s_report.rows, s_report.count)) {
    s_report.active = false;
    return false;
  }

  if (AppState_getEmailReportEnabled() && s_report.count > 0) {
    char* textBody = s_emailTextBuild;
    char* htmlCore = s_emailHtmlBuild;
    int okCount = 0;
    for (int i = 0; i < s_report.count; i++) if (s_report.rows[i].passed) okCount++;
    int failCount = s_report.count - okCount;

    char genEm[56];
    SparkyTime_formatPreferred(genEm, sizeof(genEm));
    int n = snprintf(textBody, sizeof(s_emailTextBuild),
                     "SparkyCheck report summary\r\n\r\n"
                     "Job: %s\r\n"
                     "Device: %s\r\n"
                     "Student: %s\r\n"
                     "Rules: %s\r\n"
                     "Generated: %s\r\n"
                     "Results: %d total, %d pass, %d fail\r\n\r\n",
                     s_report.basename,
                     device_id[0] ? device_id : "(not set)",
                     s_report.student_id[0] ? s_report.student_id : "(not set)",
                     rules_ver,
                     genEm,
                     s_report.count, okCount, failCount);
    if (n < 0) n = 0;
    for (int i = 0; i < s_report.count && n < (int)sizeof(s_emailTextBuild) - 4; i++) {
      n += snprintf(textBody + n, sizeof(s_emailTextBuild) - (unsigned)n, "- %s: %s %s [%s]\r\n",
                    s_report.rows[i].name, s_report.rows[i].value, s_report.rows[i].unit,
                    s_report.rows[i].passed ? "PASS" : "FAIL");
    }

    n = snprintf(htmlCore, sizeof(s_emailHtmlBuild),
                 "<p><strong>SparkyCheck report summary</strong></p>"
                 "<p><strong>Job:</strong> %s<br/>"
                 "<strong>Device:</strong> %s<br/>"
                 "<strong>Student:</strong> %s<br/>"
                 "<strong>Rules:</strong> %s<br/>"
                 "<strong>Generated:</strong> %s<br/>"
                 "<strong>Results:</strong> %d total, %d pass, %d fail</p>"
                 "<table style='border-collapse:collapse;width:100%%'>"
                 "<tr><th style='border:1px solid #cbd5e1;padding:6px;text-align:left'>Test</th>"
                 "<th style='border:1px solid #cbd5e1;padding:6px;text-align:left'>Value</th>"
                 "<th style='border:1px solid #cbd5e1;padding:6px;text-align:left'>Unit</th>"
                 "<th style='border:1px solid #cbd5e1;padding:6px;text-align:left'>Result</th></tr>",
                 s_report.basename,
                 device_id[0] ? device_id : "(not set)",
                 s_report.student_id[0] ? s_report.student_id : "(not set)",
                 rules_ver,
                 genEm,
                 s_report.count, okCount, failCount);
    if (n < 0) n = 0;
    for (int i = 0; i < s_report.count && n < (int)sizeof(s_emailHtmlBuild) - 96; i++) {
      n += snprintf(htmlCore + n, sizeof(s_emailHtmlBuild) - (unsigned)n,
                    "<tr><td style='border:1px solid #cbd5e1;padding:6px'>%s</td>"
                    "<td style='border:1px solid #cbd5e1;padding:6px'>%s</td>"
                    "<td style='border:1px solid #cbd5e1;padding:6px'>%s</td>"
                    "<td style='border:1px solid #cbd5e1;padding:6px'>%s</td></tr>",
                    s_report.rows[i].name, s_report.rows[i].value, s_report.rows[i].unit,
                    s_report.rows[i].passed ? "PASS" : "FAIL");
    }
    if (n < (int)sizeof(s_emailHtmlBuild) - 16) snprintf(htmlCore + n, sizeof(s_emailHtmlBuild) - (unsigned)n, "</table>");

    if (!s_reportEmailInFlight) {
      memcpy(s_reportEmailWork.textBody, textBody, sizeof(s_reportEmailWork.textBody));
      memcpy(s_reportEmailWork.htmlCore, htmlCore, sizeof(s_reportEmailWork.htmlCore));
      s_reportEmailPending = true;
    }
  }

  /* Field mode: also persist report copies on SD card when available. */
  if (AppState_isFieldMode()) {
#ifdef SPARKYCHECK_PANEL_43B
    // 4.3B SD path is SPI+CH422G controlled CS, not SD_MMC.
    // Skip SD mirror write until dedicated SPI SD support is added.
#else
    bool sd_ok = (SD_MMC.cardType() != CARD_NONE);
    if (!sd_ok) sd_ok = SD_MMC.begin("/sdcard", true);
    if (sd_ok) {
      writeReportToFs(SD_MMC, "/reports", s_report.basename, device_id,
                      s_report.student_id, rules_ver, s_report.rows, s_report.count);
    }
#endif
  }

  s_report.active = false;
  return true;
}

void ReportGenerator_getLastBasename(char* buf, unsigned buf_size) {
  if (buf_size) buf[0] = '\0';
  strncpy(buf, s_report.basename, buf_size - 1);
  if (buf_size) buf[buf_size - 1] = '\0';
}

static void htmlBasenameFromFsName(const String& name, char* out, size_t outsz) {
  if (!out || outsz < 2) return;
  out[0] = '\0';
  String base = name;
  if (base.startsWith("/reports/")) base = base.substring(9);
  else if (base.startsWith("reports/")) base = base.substring(8);
  if (!base.endsWith(".html")) return;
  base = base.substring(0, base.length() - 5);
  if (base.indexOf('/') >= 0 || base.indexOf('\\') >= 0) return;
  strncpy(out, base.c_str(), outsz - 1);
  out[outsz - 1] = '\0';
}

int ReportGenerator_listReports(char* buf, unsigned buf_size) {
  if (!s_fs_ok || !buf || buf_size < 2) return 0;
  buf[0] = '\0';
  File root = LittleFS.open("/reports");
  if (!root || !root.isDirectory()) return 0;
  int n = 0;
  unsigned int used = 0;
  File f = root.openNextFile();
  char line[64];
  while (f && n < 32) {
    htmlBasenameFromFsName(f.name(), line, sizeof(line));
    if (line[0]) {
      size_t len = strlen(line);
      if (used + len + 2 <= buf_size) {
        if (n) { buf[used++] = '\n'; buf[used] = '\0'; }
        memcpy(buf + used, line, len + 1);
        used += len;
        n++;
      }
    }
    f = root.openNextFile();
  }
  return n;
}

static int strcmp_desc(const void* a, const void* b) {
  return strcmp((const char*)b, (const char*)a);
}

int ReportGenerator_fillBasenameList(char (*lines)[64], int maxLines) {
  if (!s_fs_ok || !lines || maxLines <= 0) return 0;
  char tmp[48][64];
  int n = 0;
  File root = LittleFS.open("/reports");
  if (!root || !root.isDirectory()) return 0;
  File f = root.openNextFile();
  while (f && n < 48) {
    htmlBasenameFromFsName(f.name(), tmp[n], 64);
    if (tmp[n][0]) n++;
    f = root.openNextFile();
  }
  if (n <= 0) return 0;
  qsort(tmp, (size_t)n, 64, strcmp_desc);
  int out = 0;
  for (int i = 0; i < n && out < maxLines; i++) {
    if (i > 0 && strcmp(tmp[i], tmp[i - 1]) == 0) continue;
    strncpy(lines[out], tmp[i], 63);
    lines[out][63] = '\0';
    out++;
  }
  return out;
}

bool ReportGenerator_readReportCsv(const char* basename, char* buf, unsigned buf_size, unsigned* out_bytes_or_null) {
  if (!basename || !basename[0] || !buf || buf_size < 2 || !s_fs_ok) return false;
  if (strchr(basename, '/') || strchr(basename, '\\') || strchr(basename, '.')) return false;
  char path[96];
  snprintf(path, sizeof(path), "/reports/%s.csv", basename);
  if (!LittleFS.exists(path)) return false;
  File f = LittleFS.open(path, "r");
  if (!f) return false;
  size_t n = f.read((uint8_t*)buf, (size_t)buf_size - 1u);
  f.close();
  buf[n] = '\0';
  if (out_bytes_or_null) *out_bytes_or_null = (unsigned)n;
  return true;
}

bool ReportGenerator_deleteReportBasename(const char* basename) {
  if (!basename || !basename[0] || !s_fs_ok) return false;
  if (strchr(basename, '/') || strchr(basename, '\\') || strchr(basename, '.')) return false;
  char p1[96], p2[96];
  snprintf(p1, sizeof(p1), "/reports/%s.csv", basename);
  snprintf(p2, sizeof(p2), "/reports/%s.html", basename);
  bool any = false;
  if (LittleFS.exists(p1)) {
    any = true;
    LittleFS.remove(p1);
  }
  if (LittleFS.exists(p2)) {
    any = true;
    LittleFS.remove(p2);
  }
  return any;
}
