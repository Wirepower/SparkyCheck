/**
 * Report generation: CSV + HTML to LittleFS. Uses fixed buffers and
 * line-by-line file write to avoid heap fragmentation and OOM on ESP32.
 */
#include "ReportGenerator.h"
#include "AppState.h"
#include "Standards.h"
#include "GoogleSync.h"
#include <LittleFS.h>
#include <SD_MMC.h>
#include <Arduino.h>
#include <stdio.h>
#include <string.h>

#define MAX_RESULTS 24
#define BASENAME_LEN 48

struct Row {
  char name[32];
  char value[24];
  char unit[16];
  bool passed;
  char clause[24];
};

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

static void makeBasename(char* out, unsigned cap) {
  unsigned long t = millis() / 1000;
  snprintf(out, cap, "Job_%lu", t);
}

bool ReportGenerator_begin(const char* job_id_or_null) {
  if (!s_fs_ok && !ReportGenerator_init()) return false;
  s_report.active = true;
  s_report.count = 0;
  if (job_id_or_null && job_id_or_null[0])
    strncpy(s_report.basename, job_id_or_null, BASENAME_LEN - 1);
  else
    makeBasename(s_report.basename, BASENAME_LEN);
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
  if (student_id && student_id[0])
    snprintf(line, sizeof(line), "<h1>SparkyCheck Verification Report</h1><p><strong>Job:</strong> %s &nbsp; <strong>Device:</strong> %s &nbsp; <strong>Student:</strong> %s &nbsp; <strong>Rules:</strong> %s</p>", basename, device_id ? device_id : "", student_id, rules_ver ? rules_ver : "");
  else
    snprintf(line, sizeof(line), "<h1>SparkyCheck Verification Report</h1><p><strong>Job:</strong> %s &nbsp; <strong>Device:</strong> %s &nbsp; <strong>Rules:</strong> %s</p>", basename, device_id ? device_id : "", rules_ver ? rules_ver : "");
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

  /* Field mode: also persist report copies on SD card when available. */
  if (AppState_isFieldMode()) {
    bool sd_ok = (SD_MMC.cardType() != CARD_NONE);
    if (!sd_ok) sd_ok = SD_MMC.begin("/sdcard", true);
    if (sd_ok) {
      writeReportToFs(SD_MMC, "/reports", s_report.basename, device_id,
                      s_report.student_id, rules_ver, s_report.rows, s_report.count);
    }
  }

  s_report.active = false;
  return true;
}

void ReportGenerator_getLastBasename(char* buf, unsigned buf_size) {
  if (buf_size) buf[0] = '\0';
  strncpy(buf, s_report.basename, buf_size - 1);
  if (buf_size) buf[buf_size - 1] = '\0';
}

int ReportGenerator_listReports(char* buf, unsigned buf_size) {
  if (!s_fs_ok || !buf || buf_size < 2) return 0;
  buf[0] = '\0';
  File root = LittleFS.open("/reports");
  if (!root || !root.isDirectory()) return 0;
  int n = 0;
  unsigned int used = 0;
  File f = root.openNextFile();
  while (f && n < 32) {
    String name = f.name();
    if (name.endsWith(".html")) {
      size_t len = name.length() - 5;
      if (len > 0 && used + len + 2 <= buf_size) {
        if (n) { buf[used++] = '\n'; buf[used] = '\0'; }
        memcpy(buf + used, name.c_str(), len);
        used += len;
        buf[used] = '\0';
        n++;
      }
    }
    f = root.openNextFile();
  }
  return n;
}
