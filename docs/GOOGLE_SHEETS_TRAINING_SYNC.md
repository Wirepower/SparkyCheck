# Training mode cloud sync (Google Sheets or SharePoint)

This optional feature lets SparkyCheck devices in **Training mode** post **live progress + results** to a webhook endpoint.

That endpoint can write rows to:

- **Google Sheets** (via Apps Script Web App), or
- **SharePoint List** (via Power Automate flow).

---

## Live sync behaviour

Device sends updates:

- when a test starts,
- on each **Next** step,
- on **Back** step,
- when result is confirmed,
- when session is saved.

Back-end should **upsert** the same row (not append every event) using:

- `device_id` + `session_id` (recommended key).

## Payload sent by device

Each sync event posts JSON with:

- `auth_token`
- `device_id` (renamable in settings; defaults to MAC-derived ID)
- `session_id`
- `cubicle_id`
- `mode` (`training`)
- `sync_event` (`test_started`, `step_next`, `step_back`, `result_confirmed`, `session_saved`, `ping`)
- `sync_target` (`auto`, `google`, `sharepoint`)
- `email_reporting_enabled` (`true` / `false`)
- `test_id`
- `test_key` (canonical key for test column mapping)
- `step_title`
- `step_index`
- `step_count`
- `has_result`
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

Canonical `test_key` values (hard-mapped in firmware):

- `earth_continuity_conductors`
- `insulation_resistance`
- `polarity`
- `earth_continuity_cpc`
- `correct_circuit_connections`
- `earth_fault_loop_impedance`
- `rcd_operation`

---

## Device setup

On device:

1. Go to **Settings → Firmware updates → Training sync setup (PIN)**.
2. Configure:
   - **Email report** channel (On/Off, optional)
   - **Cloud sync** channel (On/Off, optional)
   - **Sync target** (Auto / Google Sheets / SharePoint)
   - **Device ID label** (optional; set to cubicle number if desired)
   - **Sync endpoint URL** (Apps Script URL or Power Automate HTTP URL)
   - **Auth token** (optional shared secret)
   - **Cubicle ID** (e.g. `CUB-01`)
3. Enable whichever channel(s) you want. None are required.
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
  "last_event",
  "device_id",
  "session_id",
  "cubicle_id",
  "mode",
  "current_test",
  "current_step",
  "current_step_total",
  "earth_continuity_conductors",
  "insulation_resistance",
  "polarity",
  "earth_continuity_cpc",
  "correct_circuit_connections",
  "earth_fault_loop_impedance",
  "rcd_operation",
  "report_id",
  "rules_version",
  "firmware_version",
  "ts_ms",
  "last_clause"
];

const TEST_COLUMNS = {
  "earth_continuity_conductors": "earth_continuity_conductors",
  "insulation_resistance": "insulation_resistance",
  "polarity": "polarity",
  "earth_continuity_cpc": "earth_continuity_cpc",
  "correct_circuit_connections": "correct_circuit_connections",
  "earth_fault_loop_impedance": "earth_fault_loop_impedance",
  "rcd_operation": "rcd_operation"
};

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
    const rowNum = getOrCreateSessionRow_(sheet, data.device_id || "", data.session_id || "");
    const nowIso = new Date().toISOString();
    const update = {
      timestamp_iso: nowIso,
      last_event: data.sync_event || "",
      device_id: data.device_id || "",
      session_id: data.session_id || "",
      cubicle_id: data.cubicle_id || "",
      mode: data.mode || "",
      current_test: data.test_name || "",
      current_step: data.step_index || "",
      current_step_total: data.step_count || "",
      report_id: data.report_id || "",
      rules_version: data.rules_version || "",
      firmware_version: data.firmware_version || "",
      ts_ms: data.ts_ms || "",
      last_clause: data.clause || ""
    };

    if (data.has_result === true) {
      const col = TEST_COLUMNS[String(data.test_key || "")];
      if (col) update[col] = formatResult_(data);
    }

    writeRowFields_(sheet, rowNum, update);
    return jsonOut({ ok: true, spreadsheetId: ss.getId(), spreadsheetUrl: ss.getUrl(), row: rowNum }, 200);
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

function getHeaderMap_(sheet) {
  const values = sheet.getRange(1, 1, 1, HEADERS.length).getValues()[0];
  const map = {};
  values.forEach((h, i) => map[String(h)] = i + 1);
  return map;
}

function getOrCreateSessionRow_(sheet, deviceId, sessionId) {
  if (!deviceId || !sessionId) {
    sheet.appendRow(new Array(HEADERS.length).fill(""));
    return sheet.getLastRow();
  }
  const map = getHeaderMap_(sheet);
  const last = sheet.getLastRow();
  if (last < 2) {
    sheet.appendRow(new Array(HEADERS.length).fill(""));
    return 2;
  }

  const deviceCol = map["device_id"];
  const sessionCol = map["session_id"];
  const range = sheet.getRange(2, 1, last - 1, HEADERS.length).getValues();
  for (let i = 0; i < range.length; i++) {
    if (String(range[i][deviceCol - 1]) === String(deviceId) &&
        String(range[i][sessionCol - 1]) === String(sessionId)) {
      return i + 2;
    }
  }

  sheet.appendRow(new Array(HEADERS.length).fill(""));
  return sheet.getLastRow();
}

function writeRowFields_(sheet, rowNum, updateObj) {
  const map = getHeaderMap_(sheet);
  for (const key in updateObj) {
    if (!Object.prototype.hasOwnProperty.call(updateObj, key)) continue;
    if (!map[key]) continue;
    sheet.getRange(rowNum, map[key]).setValue(updateObj[key]);
  }
}

function formatResult_(data) {
  const v = data.value || "";
  const u = data.unit || "";
  const r = data.result || "";
  if (v && u) return `${v} ${u} (${r})`;
  if (v) return `${v} (${r})`;
  return r || "";
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
   - `last_event` (Single line text)
   - `device_id` (Single line text)
   - `session_id` (Single line text)
   - `cubicle_id` (Single line text)
   - `mode` (Single line text)
   - `current_test` (Single line text)
   - `current_step` (Number or Single line text)
   - `current_step_total` (Number or Single line text)
   - `report_id` (Single line text)
   - One column per test (for example `earth_continuity_conductors`, `insulation_resistance`, `polarity`, `earth_continuity_cpc`, `correct_circuit_connections`, `earth_fault_loop_impedance`, `rcd_operation`)
   - `rules_version` (Single line text)
   - `firmware_version` (Single line text)
   - `ts_ms` (Number or Single line text)
   - `last_clause` (Single line text)
2. In Power Automate:
   - Create **Instant cloud flow** with trigger **When an HTTP request is received**.
   - Add action **Get items** filtered by `device_id` + `session_id`.
   - If found, use **Update item** (live overwrite).
   - If not found, use **Create item** (new row).
   - Map JSON fields from trigger body to list columns, including:
     - progress fields (`sync_event`, `current_test`, `current_step`, `current_step_total`)
     - result columns (update specific test column when `has_result == true`, selected by `test_key`)
   - Optional: add condition to verify `auth_token`.
3. Save flow and copy generated HTTP POST URL.
4. Paste that URL into device **Sync endpoint URL**.

---

## Notes

- Devices do not call Microsoft Graph/Google APIs directly; they post JSON to your endpoint.
- This keeps firmware simple and lets you swap backend (Google/SharePoint) without firmware changes.
- Use `device_id + session_id` as your unique key so Back/Next updates modify the same row live.
- `session_id` is stable for the device runtime (new after reboot, or when firmware calls session reset).

