# Training mode cloud sync (Google Sheets or SharePoint)

This optional feature lets SparkyCheck devices in **Training mode** post completed test results to a webhook endpoint.

That endpoint can write rows to:

- **Google Sheets** (via Apps Script Web App), or
- **SharePoint List** (via Power Automate flow).

---

## Payload sent by device

Each completed test posts JSON with:

- `auth_token`
- `device_id`
- `cubicle_id`
- `mode` (`training`)
- `report_id`
- `test_name`
- `value`
- `unit`
- `result` (`PASS` / `FAIL`)
- `passed` (`true` / `false`)
- `clause`
- `rules_version`
- `firmware_version`
- `ts_ms`

---

## Device setup

On device:

1. Go to **Settings → Firmware updates → Training sync setup (PIN)**.
2. Configure:
   - **Sync endpoint URL** (Apps Script URL or Power Automate HTTP URL)
   - **Auth token** (optional shared secret)
   - **Cubicle ID** (e.g. `CUB-01`)
3. Toggle **Sync on**.
4. Tap **Send test ping**.

Sync only runs in **Training mode** and only when WiFi is connected.

---

## Option A: Google Sheets endpoint (auto-create sheet + headers)

1. Go to [script.new](https://script.new) while signed into your Google account.
2. Replace code with the script below.
3. Save project (for example `SparkyCheck Training Sync`).
4. Deploy:
   - **Deploy → New deployment**
   - Type: **Web app**
   - Execute as: **Me**
   - Who has access: **Anyone**
5. Copy Web app URL and paste it into device **Sync endpoint URL**.

> If using token auth, set `AUTH_TOKEN` in script and the same value on devices.

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

    return jsonOut({ ok: true, spreadsheetId: ss.getId(), spreadsheetUrl: ss.getUrl() }, 200);
  } catch (err) {
    return jsonOut({ ok: false, error: String(err) }, 500);
  }
}

function getOrCreateSpreadsheet_() {
  const props = PropertiesService.getScriptProperties();
  const existingId = props.getProperty("SPREADSHEET_ID");
  if (existingId) return SpreadsheetApp.openById(existingId);
  const ss = SpreadsheetApp.create(SPREADSHEET_NAME);
  props.setProperty("SPREADSHEET_ID", ss.getId());
  return ss;
}

function getOrCreateSheet_(ss, name) {
  const existing = ss.getSheetByName(name);
  return existing ? existing : ss.insertSheet(name);
}

function ensureHeaders_(sheet) {
  const range = sheet.getRange(1, 1, 1, HEADERS.length);
  const values = range.getValues()[0];
  const hasHeaders = values.some(v => String(v || "").trim() !== "");
  if (!hasHeaders) range.setValues([HEADERS]);
}

function jsonOut(obj, statusCode) {
  obj.status = statusCode;
  return ContentService.createTextOutput(JSON.stringify(obj)).setMimeType(ContentService.MimeType.JSON);
}
```

---

## Option B: SharePoint List endpoint (Power Automate)

Recommended approach: use Power Automate as the webhook bridge.

1. In SharePoint, create a list (for example `SparkyCheckTrainingResults`) with columns:
   - `timestamp_iso` (Single line text)
   - `device_id` (Single line text)
   - `cubicle_id` (Single line text)
   - `mode` (Single line text)
   - `report_id` (Single line text)
   - `test_name` (Single line text)
   - `value` (Single line text)
   - `unit` (Single line text)
   - `result` (Single line text)
   - `passed` (Yes/No or Single line text)
   - `clause` (Single line text)
   - `rules_version` (Single line text)
   - `firmware_version` (Single line text)
   - `ts_ms` (Number or Single line text)
2. In Power Automate:
   - Create **Instant cloud flow** with trigger **When an HTTP request is received**.
   - Add action **Create item** (SharePoint) to your site/list.
   - Map JSON fields from trigger body to list columns.
   - Optional: add condition to verify `auth_token`.
3. Save flow and copy generated HTTP POST URL.
4. Paste that URL into device **Sync endpoint URL**.

---

## Notes

- Devices do not call Microsoft Graph/Google APIs directly; they post JSON to your endpoint.
- This keeps firmware simple and lets you swap backend (Google/SharePoint) without firmware changes.

