# Training mode Google Sheets sync (optional)

This feature lets SparkyCheck devices in **Training mode** write test results to a shared Google Sheet, so each cubicle/device can be tracked centrally.

---

## What gets uploaded

Each completed test session sends one row containing:

- Device ID (derived from ESP32 MAC)
- Cubicle ID (configured per device)
- Mode (training)
- Report/job ID
- Test name
- Value + unit
- PASS/FAIL
- Clause
- Rules version
- Firmware version
- Timestamp (milliseconds since boot)

---

## Device setup

On device:

1. Go to **Settings → Firmware updates → Training sync setup (PIN)**.
2. Configure:
   - **Google endpoint URL** (Apps Script web app URL)
   - **Auth token** (optional shared secret)
   - **Cubicle ID** (e.g. `CUB-01`)
3. Toggle **Sync on**.
4. Tap **Send test ping**.

Sync only runs in **Training mode** and only when WiFi is connected.

---

## Google side setup (creates sheet automatically)

1. Go to [script.new](https://script.new) while signed into your Google account.
2. Replace code with the script below.
3. Save project (for example `SparkyCheck Training Sync`).
4. Deploy:
   - **Deploy → New deployment**
   - Type: **Web app**
   - Execute as: **Me**
   - Who has access: **Anyone**
5. Copy the Web app URL and paste it into device **Google endpoint URL**.

> If you use an auth token, set `AUTH_TOKEN` in the script and the same token on each device.

### Apps Script (copy-paste)

```javascript
const SPREADSHEET_NAME = "SparkyCheck Training Results";
const SHEET_NAME = "Results";
const AUTH_TOKEN = ""; // Optional shared secret. Leave "" to disable token check.

const HEADERS = [
  "timestamp_iso",
  "ts_ms",
  "device_id",
  "cubicle_id",
  "mode",
  "report_id",
  "test_name",
  "value",
  "unit",
  "result",
  "passed",
  "clause",
  "rules_version",
  "firmware_version"
];

function doPost(e) {
  try {
    if (!e || !e.postData || !e.postData.contents) {
      return jsonOut({ ok: false, error: "empty body" }, 400);
    }
    const data = JSON.parse(e.postData.contents);
    if (AUTH_TOKEN && data.auth_token !== AUTH_TOKEN) {
      return jsonOut({ ok: false, error: "unauthorized" }, 401);
    }

    const ss = getOrCreateSpreadsheet_();
    const sheet = getOrCreateSheet_(ss, SHEET_NAME);
    ensureHeaders_(sheet);

    const nowIso = new Date().toISOString();
    sheet.appendRow([
      nowIso,
      data.ts_ms || "",
      data.device_id || "",
      data.cubicle_id || "",
      data.mode || "",
      data.report_id || "",
      data.test_name || "",
      data.value || "",
      data.unit || "",
      data.result || "",
      data.passed === true ? "true" : (data.passed === false ? "false" : ""),
      data.clause || "",
      data.rules_version || "",
      data.firmware_version || ""
    ]);

    return jsonOut({
      ok: true,
      spreadsheetId: ss.getId(),
      spreadsheetUrl: ss.getUrl(),
      sheetName: sheet.getName()
    }, 200);
  } catch (err) {
    return jsonOut({ ok: false, error: String(err) }, 500);
  }
}

function getOrCreateSpreadsheet_() {
  const props = PropertiesService.getScriptProperties();
  const existingId = props.getProperty("SPREADSHEET_ID");
  if (existingId) {
    return SpreadsheetApp.openById(existingId);
  }
  const ss = SpreadsheetApp.create(SPREADSHEET_NAME);
  props.setProperty("SPREADSHEET_ID", ss.getId());
  return ss;
}

function getOrCreateSheet_(ss, name) {
  const existing = ss.getSheetByName(name);
  if (existing) return existing;
  return ss.insertSheet(name);
}

function ensureHeaders_(sheet) {
  const range = sheet.getRange(1, 1, 1, HEADERS.length);
  const values = range.getValues()[0];
  const hasHeaders = values.some(v => String(v || "").trim() !== "");
  if (!hasHeaders) {
    range.setValues([HEADERS]);
  }
}

function jsonOut(obj, statusCode) {
  // Apps Script web apps do not allow custom HTTP status in all contexts.
  // Keep status code in payload for clients that need it.
  obj.status = statusCode;
  return ContentService
    .createTextOutput(JSON.stringify(obj))
    .setMimeType(ContentService.MimeType.JSON);
}
```

