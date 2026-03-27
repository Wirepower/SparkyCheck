#include "EmailTest.h"
#include "AppState.h"

#include <Arduino.h>
#include <WiFi.h>
#include <ESP_Mail_Client.h>
#include <stdio.h>
#include <string.h>
#include "boot_logo_embedded.h"

static void setErr(char* err, unsigned err_size, const char* msg) {
  if (!err || err_size == 0) return;
  snprintf(err, err_size, "%s", msg ? msg : "Unknown error");
}

/* Avoid large stack allocations in web/device callbacks. */
static char s_subjectBuf[220];
static char s_bodyHtmlBuf[3200];
static char s_testBodyBuf[900];
static char s_testHtmlCoreBuf[1200];
static uint8_t s_inlineLogoBmpBuf[70000];
static size_t s_inlineLogoBmpSize = 0;

static bool buildInlineBootLogoBmp120(uint8_t* out, size_t outSize, size_t* outWritten) {
  if (!out || !outWritten) return false;
  *outWritten = 0;

  const size_t srcWords = sizeof(kBootLogoRgb565) / sizeof(kBootLogoRgb565[0]);
  int srcW = (int)BOOT_LOGO_W;
  int srcH = (int)BOOT_LOGO_H;
  if ((size_t)srcW * (size_t)srcH != srcWords) {
    srcH = (srcW > 0) ? (int)(srcWords / (size_t)srcW) : 1;
    if (srcH <= 0) srcH = 1;
  }

  const int outW = 120;
  const int outH = (srcW > 0) ? (int)((srcH * outW) / srcW) : 120;
  if (outH <= 0) return false;

  const size_t rowBytes = (size_t)outW * 3u; /* 24-bit RGB BMP */
  const size_t pad = (4u - (rowBytes % 4u)) % 4u;
  const size_t imgBytes = (rowBytes + pad) * (size_t)outH;
  const size_t fileSize = 54u + imgBytes;
  if (outSize < fileSize) return false;

  /* BMP header (BITMAPFILEHEADER + BITMAPINFOHEADER, 54 bytes total). */
  uint8_t* p = out;
  memset(p, 0, 54);
  p[0] = 'B'; p[1] = 'M';
  p[2] = (uint8_t)(fileSize & 0xFF);
  p[3] = (uint8_t)((fileSize >> 8) & 0xFF);
  p[4] = (uint8_t)((fileSize >> 16) & 0xFF);
  p[5] = (uint8_t)((fileSize >> 24) & 0xFF);
  p[10] = 54; /* pixel data offset */
  p[14] = 40; /* DIB header size */
  /* width */
  p[18] = (uint8_t)(outW & 0xFF);
  p[19] = (uint8_t)((outW >> 8) & 0xFF);
  p[20] = (uint8_t)((outW >> 16) & 0xFF);
  p[21] = (uint8_t)((outW >> 24) & 0xFF);
  /* height */
  p[22] = (uint8_t)(outH & 0xFF);
  p[23] = (uint8_t)((outH >> 8) & 0xFF);
  p[24] = (uint8_t)((outH >> 16) & 0xFF);
  p[25] = (uint8_t)((outH >> 24) & 0xFF);
  p[26] = 1; /* planes LSB */
  p[27] = 0;
  p[28] = 24; /* bits per pixel */
  p[34] = (uint8_t)(imgBytes & 0xFF);
  p[35] = (uint8_t)((imgBytes >> 8) & 0xFF);
  p[36] = (uint8_t)((imgBytes >> 16) & 0xFF);
  p[37] = (uint8_t)((imgBytes >> 24) & 0xFF);

  uint8_t* pix = out + 54;
  for (int y = outH - 1; y >= 0; y--) {
    int sy = (outH > 0 && srcH > 0) ? (y * srcH) / outH : 0;
    for (int x = 0; x < outW; x++) {
      int sx = (outW > 0 && srcW > 0) ? (x * srcW) / outW : 0;
      size_t idx = (size_t)sy * (size_t)srcW + (size_t)sx;
      if (idx >= srcWords) idx = (srcWords > 0) ? (srcWords - 1) : 0;
      uint16_t c = (uint16_t)pgm_read_word(&kBootLogoRgb565[idx]);
      uint8_t r5 = (uint8_t)((c >> 11) & 0x1F);
      uint8_t g6 = (uint8_t)((c >> 5) & 0x3F);
      uint8_t b5 = (uint8_t)(c & 0x1F);
      uint8_t r = (uint8_t)((r5 * 255) / 31);
      uint8_t g = (uint8_t)((g6 * 255) / 63);
      uint8_t b = (uint8_t)((b5 * 255) / 31);
      /* BMP uses BGR order */
      *pix++ = b;
      *pix++ = g;
      *pix++ = r;
    }
    for (size_t k = 0; k < pad; k++) *pix++ = 0;
  }

  *outWritten = fileSize;
  return true;
}

static void readIdentity(char* deviceId, unsigned devSize, char* cubicleId, unsigned cubSize) {
  if (deviceId && devSize) {
    AppState_getDeviceIdOverride(deviceId, devSize);
    deviceId[devSize - 1] = '\0';
  }
  if (cubicleId && cubSize) {
    AppState_getTrainingSyncCubicleId(cubicleId, cubSize);
    cubicleId[cubSize - 1] = '\0';
  }
}

static void buildSubjectWithIdentity(const char* base, const char* deviceId, const char* cubicleId, char* out, unsigned outSize) {
  const char* b = (base && base[0]) ? base : "SparkyCheck Email";
  if (deviceId && deviceId[0] && cubicleId && cubicleId[0]) {
    snprintf(out, outSize, "%s - Device %s - Cubicle %s", b, deviceId, cubicleId);
  } else if (deviceId && deviceId[0]) {
    snprintf(out, outSize, "%s - Device %s", b, deviceId);
  } else if (cubicleId && cubicleId[0]) {
    snprintf(out, outSize, "%s - Cubicle %s", b, cubicleId);
  } else {
    snprintf(out, outSize, "%s", b);
  }
  if (out && outSize) out[outSize - 1] = '\0';
}

void EmailTest_buildBrandedHtml(const char* heading, const char* body_html, char* out, unsigned out_size) {
  if (!out || out_size == 0) return;
  const char* h = (heading && heading[0]) ? heading : "SparkyCheck Notification";
  const char* b = (body_html && body_html[0]) ? body_html : "";
  snprintf(
    out, out_size,
    "<!doctype html><html><body style='margin:0;padding:0;background:#f1f5f9;'>"
    "<table role='presentation' width='100%%' cellspacing='0' cellpadding='0' style='padding:24px 0;'>"
    "<tr><td align='center'>"
    "<table role='presentation' width='640' cellspacing='0' cellpadding='0' style='max-width:640px;background:#ffffff;border:1px solid #cbd5e1;border-radius:10px;overflow:hidden;font-family:Arial,sans-serif;color:#0f172a;'>"
    "<tr><td style='background:#1d3557;padding:16px 20px;color:#fff;'>"
    "<img src=\"sparky_logo.bmp\" width='120' height='45' style='display:block;max-width:120px;height:auto;border:0;margin:0 0 10px 0;' alt='SparkyCheck logo'/>"
    "<div style='font-size:22px;line-height:1.1;font-weight:700;color:#ffffff;'>SparkyCheck</div>"
    "<div style='font-size:12px;line-height:1.2;color:#cbd5e1;margin-top:2px;'>Professional Electrical Verification Companion</div>"
    "<div style='font-size:20px;font-weight:700;letter-spacing:0.2px;'>SparkyCheck</div>"
    "<div style='font-size:12px;opacity:0.9;'>Professional Electrical Verification Companion</div>"
    "</td></tr>"
    "<tr><td style='padding:20px;'>"
    "<h2 style='margin:0 0 12px 0;font-size:20px;color:#0f172a;'>%s</h2>"
    "<div style='font-size:14px;line-height:1.6;color:#1e293b;'>%s</div>"
    "</td></tr>"
    "<tr><td style='padding:14px 20px;background:#f8fafc;border-top:1px solid #e2e8f0;'>"
    "<div style='font-size:12px;color:#334155;'><strong>SparkyCheck</strong> | Created by Frank Offer (2026)</div>"
    "<div style='font-size:11px;color:#64748b;margin-top:4px;'>This email was generated by your SparkyCheck device.</div>"
    "</td></tr>"
    "</table></td></tr></table></body></html>",
    h, b
  );
  out[out_size - 1] = '\0';
}

bool EmailTest_sendNow(char* err, unsigned err_size) {
  char cubicleId[APP_STATE_TRAINING_SYNC_CUBICLE_LEN] = "";
  char deviceId[APP_STATE_DEVICE_ID_LEN] = "";
  readIdentity(deviceId, sizeof(deviceId), cubicleId, sizeof(cubicleId));
  char ip[24] = "";
  snprintf(ip, sizeof(ip), "%s", WiFi.localIP().toString().c_str());
  snprintf(s_testBodyBuf, sizeof(s_testBodyBuf),
           "Hello,\r\n\r\n"
           "This is a test email from SparkyCheck.\r\n\r\n"
           "If you received this, your SMTP settings are working.\r\n"
           "Device ID: %s\r\n"
           "Cubicle ID: %s\r\n"
           "Device IP: %s\r\n"
           "Mode: %s\r\n"
           "Timestamp(ms): %lu\r\n\r\n"
           "Regards,\r\nSparkyCheck\r\n",
           deviceId[0] ? deviceId : "(not set)",
           cubicleId[0] ? cubicleId : "(not set)",
           ip[0] ? ip : "(offline)",
           AppState_isFieldMode() ? "Field" : "Training",
           (unsigned long)millis());
  snprintf(s_testHtmlCoreBuf, sizeof(s_testHtmlCoreBuf),
           "<p>Hello,</p>"
           "<p>This is a <strong>test email from SparkyCheck</strong>.</p>"
           "<p>If you received this, your SMTP settings are working correctly.</p>"
           "<p><strong>Device ID:</strong> %s<br/>"
           "<strong>Cubicle ID:</strong> %s<br/>"
           "<strong>Device IP:</strong> %s<br/>"
           "<strong>Mode:</strong> %s<br/>"
           "<strong>Timestamp (ms):</strong> %lu</p>"
           "<p>Regards,<br/>SparkyCheck</p>",
           deviceId[0] ? deviceId : "(not set)",
           cubicleId[0] ? cubicleId : "(not set)",
           ip[0] ? ip : "(offline)",
           AppState_isFieldMode() ? "Field" : "Training",
           (unsigned long)millis());
  return EmailTest_sendCustomNow("SparkyCheck SMTP Test Email", "SMTP Test Email", s_testBodyBuf, s_testHtmlCoreBuf, err, err_size);
}

bool EmailTest_sendCustomNow(const char* subject_base,
                             const char* heading,
                             const char* body_text,
                             const char* body_html_core,
                             char* err,
                             unsigned err_size) {
  char smtpServer[APP_STATE_EMAIL_STR_LEN] = "";
  char smtpPort[APP_STATE_EMAIL_STR_LEN] = "";
  char smtpUser[APP_STATE_EMAIL_STR_LEN] = "";
  char smtpPass[APP_STATE_EMAIL_STR_LEN] = "";
  char reportTo[APP_STATE_EMAIL_STR_LEN] = "";
  char cubicleId[APP_STATE_TRAINING_SYNC_CUBICLE_LEN] = "";
  char deviceId[APP_STATE_DEVICE_ID_LEN] = "";
  AppState_getSmtpServer(smtpServer, sizeof(smtpServer));
  AppState_getSmtpPort(smtpPort, sizeof(smtpPort));
  AppState_getSmtpUser(smtpUser, sizeof(smtpUser));
  AppState_getSmtpPass(smtpPass, sizeof(smtpPass));
  AppState_getReportToEmail(reportTo, sizeof(reportTo));
  readIdentity(deviceId, sizeof(deviceId), cubicleId, sizeof(cubicleId));

  if (!smtpServer[0] || !smtpUser[0] || !smtpPass[0] || !reportTo[0]) {
    setErr(err, err_size, "Missing SMTP settings.");
    return false;
  }
  int port = atoi(smtpPort);
  if (port <= 0) port = 587;
  buildSubjectWithIdentity(subject_base, deviceId, cubicleId, s_subjectBuf, sizeof(s_subjectBuf));
  EmailTest_buildBrandedHtml(heading, body_html_core, s_bodyHtmlBuf, sizeof(s_bodyHtmlBuf));

  SMTPSession smtp;
  ESP_Mail_Session session;
  session.server.host_name = smtpServer;
  session.server.port = port;
  session.login.email = smtpUser;
  session.login.password = smtpPass;
  session.login.user_domain = "";
  session.time.ntp_server = F("pool.ntp.org,time.nist.gov");
  session.time.gmt_offset = 10;
  session.time.day_light_offset = 0;

  SMTP_Message message;
  message.sender.name = F("SparkyCheck");
  message.sender.email = smtpUser;
  message.subject = s_subjectBuf;
  message.addRecipient(F("Recipient"), reportTo);
  message.text.content = body_text ? body_text : "SparkyCheck notification.";
  message.text.charSet = F("us-ascii");
  message.text.transfer_encoding = Content_Transfer_Encoding::enc_7bit;
  message.html.content = s_bodyHtmlBuf;
  message.html.charSet = F("utf-8");
  message.html.transfer_encoding = Content_Transfer_Encoding::enc_7bit;
  message.priority = esp_mail_smtp_priority::esp_mail_smtp_priority_normal;

  /* Inline attachment for mail clients that block external/data URIs. */
  s_inlineLogoBmpSize = 0;
  if (buildInlineBootLogoBmp120(s_inlineLogoBmpBuf, sizeof(s_inlineLogoBmpBuf), &s_inlineLogoBmpSize)) {
    SMTP_Attachment logoAtt{};
    logoAtt.descr.filename = "sparky_logo.bmp";
    logoAtt.descr.mime = "image/bmp";
    logoAtt.descr.content_id = "sparky_logo";
    logoAtt.blob.data = s_inlineLogoBmpBuf;
    logoAtt.blob.size = s_inlineLogoBmpSize;
    message.addInlineImage(logoAtt);
  }

  smtp.debug(0);
  if (!smtp.connect(&session)) {
    setErr(err, err_size, smtp.errorReason().c_str());
    return false;
  }
  if (!MailClient.sendMail(&smtp, &message, true)) {
    setErr(err, err_size, smtp.errorReason().c_str());
    smtp.closeSession();
    return false;
  }
  smtp.closeSession();
  setErr(err, err_size, "OK");
  return true;
}

