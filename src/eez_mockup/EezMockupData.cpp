#include "EezMockupData.h"

namespace {
static const EezMockupLabel kLabels_screen_mode_select[] = {
  { 18, 12, "Select mode" },
  { 18, 42, "Boot-admin path selection" },
  { 400, 86, "Green-highlight selected mode" },
  { 400, 116, "Saved to NVS" },
  { 400, 146, "Default mode: Field" },
};
static const EezMockupButton kButtons_screen_mode_select[] = {
  { 18, 76, 350, 44, "Training (apprentice/supervised)", SCREEN_MODE_SELECT },
  { 18, 128, 350, 44, "Field (qualified electrician)", SCREEN_MODE_SELECT },
  { 18, 180, 350, 44, "Continue", SCREEN_MAIN_MENU },
};

static const EezMockupLabel kLabels_screen_main_menu[] = {
  { 18, 12, "Main menu" },
  { 18, 42, "Current running app home" },
  { 400, 86, "Mode indicator at top" },
  { 400, 116, "Buttons produce buzzer click (if enabled)" },
};
static const EezMockupButton kButtons_screen_main_menu[] = {
  { 18, 76, 350, 44, "Start verification", SCREEN_TEST_SELECT },
  { 18, 128, 350, 44, "View reports", SCREEN_REPORT_LIST },
  { 18, 180, 350, 44, "Settings", SCREEN_SETTINGS },
};

static const EezMockupLabel kLabels_screen_test_select[] = {
  { 18, 12, "Select test" },
  { 18, 42, "All verification + SWP topics" },
  { 400, 86, "Back button top-right" },
  { 400, 116, "Training mode redirects to Student ID first" },
};
static const EezMockupButton kButtons_screen_test_select[] = {
  { 18, 76, 350, 44, "Earth continuity (conductors)", SCREEN_TEST_FLOW },
  { 18, 128, 350, 44, "Insulation resistance", SCREEN_TEST_FLOW },
  { 18, 180, 350, 44, "Polarity", SCREEN_TEST_FLOW },
  { 18, 232, 350, 44, "Earth continuity (CPC)", SCREEN_TEST_FLOW },
  { 18, 284, 350, 44, "Correct circuit connections", SCREEN_TEST_FLOW },
  { 18, 336, 350, 44, "Earth fault loop impedance", SCREEN_TEST_FLOW },
  { 18, 388, 350, 44, "RCD operation", SCREEN_TEST_FLOW },
  { 18, 440, 350, 44, "SWP D/R (motor)", SCREEN_TEST_FLOW },
  { 18, 492, 350, 44, "SWP D/R (appliance)", SCREEN_TEST_FLOW },
  { 18, 544, 350, 44, "SWP D/R (heater/sheathed)", SCREEN_TEST_FLOW },
  /* Top-right Back — matches Screens.cpp hit area (legacy delegate uses same rect). */
  { 718, 8, 72, 28, "Back", SCREEN_MAIN_MENU },
};

static const EezMockupLabel kLabels_screen_student_id[] = {
  { 18, 12, "Student ID" },
  { 18, 42, "Digits only, normalized to S#######" },
  { 400, 86, "Required before tests in Training mode" },
  { 400, 116, "Session resets at test start" },
};
static const EezMockupButton kButtons_screen_student_id[] = {
  { 18, 76, 350, 44, "1 2 3", SCREEN_STUDENT_ID },
  { 18, 128, 350, 44, "4 5 6", SCREEN_STUDENT_ID },
  { 18, 180, 350, 44, "7 8 9", SCREEN_STUDENT_ID },
  { 18, 232, 350, 44, "Del 0 Start", SCREEN_STUDENT_ID },
  { 18, 284, 350, 44, "Back", SCREEN_TEST_SELECT },
};

static const EezMockupLabel kLabels_screen_test_flow[] = {
  { 18, 12, "Test flow" },
  { 18, 42, "Dynamic step page + result mode" },
  { 400, 86, "Shows clause, instruction, step index" },
  { 400, 116, "RCD shows required criterion" },
  { 400, 146, "Back supports step rewind and edits" },
};
static const EezMockupButton kButtons_screen_test_flow[] = {
  { 18, 76, 350, 44, "Yes", SCREEN_TEST_FLOW },
  { 18, 128, 350, 44, "No", SCREEN_TEST_FLOW },
  { 18, 180, 350, 44, "OK", SCREEN_TEST_FLOW },
  { 18, 232, 350, 44, "Numeric keypad + units", SCREEN_TEST_FLOW },
  { 18, 284, 350, 44, "Confirm / End session", SCREEN_TEST_FLOW },
};

static const EezMockupLabel kLabels_screen_report_saved[] = {
  { 18, 12, "Report saved" },
  { 18, 42, "CSV + HTML generated" },
  { 400, 86, "Shows basename of saved report" },
  { 400, 116, "Training sync event: session_saved" },
};
static const EezMockupButton kButtons_screen_report_saved[] = {
  { 18, 76, 350, 44, "OK", SCREEN_MAIN_MENU },
};

static const EezMockupLabel kLabels_screen_report_list[] = {
  { 18, 12, "Reports list" },
  { 18, 42, "Recent saved reports" },
  { 400, 86, "Scrollable list area in current app logic" },
};
static const EezMockupButton kButtons_screen_report_list[] = {
  { 18, 76, 350, 44, "Back", SCREEN_MAIN_MENU },
};

static const EezMockupLabel kLabels_screen_settings[] = {
  { 18, 12, "Settings" },
  { 18, 42, "PIN-gated in Training mode" },
  { 400, 86, "All changes show confirmation prompts" },
  { 400, 116, "Training settings unlock on PIN success" },
};
static const EezMockupButton kButtons_screen_settings[] = {
  { 18, 76, 350, 44, "Screen rotation", SCREEN_ROTATION },
  { 18, 128, 350, 44, "WiFi connection", SCREEN_WIFI_LIST },
  { 18, 180, 350, 44, "Buzzer (sound)", SCREEN_SETTINGS },
  { 18, 232, 350, 44, "About", SCREEN_ABOUT },
  { 18, 284, 350, 44, "Firmware updates", SCREEN_UPDATES },
  { 18, 336, 350, 44, "Training sync (PIN)", SCREEN_PIN_ENTER },
  { 18, 388, 350, 44, "Email settings", SCREEN_EMAIL_SETTINGS },
  { 18, 440, 350, 44, "Mode change (boot hold)", SCREEN_SETTINGS },
  { 18, 492, 350, 44, "Restart device", SCREEN_SETTINGS },
  { 18, 544, 350, 44, "Change PIN", SCREEN_CHANGE_PIN },
  { 18, 596, 350, 44, "Back", SCREEN_MAIN_MENU },
};

static const EezMockupLabel kLabels_screen_rotation[] = {
  { 18, 12, "Screen orientation" },
  { 18, 42, "Display rotation picker" },
  { 400, 86, "Setting saved prompt shown on change" },
};
static const EezMockupButton kButtons_screen_rotation[] = {
  { 18, 76, 350, 44, "Portrait", SCREEN_SETTINGS },
  { 18, 128, 350, 44, "Landscape", SCREEN_SETTINGS },
  { 18, 180, 350, 44, "Back", SCREEN_SETTINGS },
};

static const EezMockupLabel kLabels_screen_wifi_list[] = {
  { 18, 12, "WiFi" },
  { 18, 42, "Scan and choose SSID" },
  { 400, 86, "Connected SSID/IP shown" },
  { 400, 116, "Tap SSID opens password screen" },
};
static const EezMockupButton kButtons_screen_wifi_list[] = {
  { 18, 76, 350, 44, "Scan", SCREEN_WIFI_LIST },
  { 18, 128, 350, 44, "SSID row(s)", SCREEN_WIFI_LIST },
  { 18, 180, 350, 44, "Back", SCREEN_SETTINGS },
};

static const EezMockupLabel kLabels_screen_wifi_password[] = {
  { 18, 12, "WiFi password" },
  { 18, 42, "On-screen keyboard entry" },
  { 400, 86, "Connection result prompt shown (success/fail)" },
};
static const EezMockupButton kButtons_screen_wifi_password[] = {
  { 18, 76, 350, 44, "Keyboard rows", SCREEN_WIFI_PASSWORD },
  { 18, 128, 350, 44, "Del", SCREEN_WIFI_PASSWORD },
  { 18, 180, 350, 44, "Connect", SCREEN_WIFI_LIST },
  { 18, 232, 350, 44, "Back", SCREEN_WIFI_LIST },
};

static const EezMockupLabel kLabels_screen_about[] = {
  { 18, 12, "About SparkyCheck" },
  { 18, 42, "Product and standards summary" },
  { 400, 86, "Shows standards in current mode" },
  { 400, 116, "Shows rules version" },
};
static const EezMockupButton kButtons_screen_about[] = {
  { 18, 76, 350, 44, "Back", SCREEN_SETTINGS },
};

static const EezMockupLabel kLabels_screen_updates[] = {
  { 18, 12, "Firmware updates" },
  { 18, 42, "OTA status and controls" },
  { 400, 86, "Current firmware + status" },
  { 400, 116, "Pending version when available" },
};
static const EezMockupButton kButtons_screen_updates[] = {
  { 18, 76, 350, 44, "Check now", SCREEN_UPDATES },
  { 18, 128, 350, 44, "Install now", SCREEN_UPDATES },
  { 18, 180, 350, 44, "Toggle auto-check", SCREEN_UPDATES },
  { 18, 232, 350, 44, "Toggle auto-install", SCREEN_UPDATES },
  { 18, 284, 350, 44, "Back", SCREEN_SETTINGS },
};

static const EezMockupLabel kLabels_screen_training_sync[] = {
  { 18, 12, "Training sync" },
  { 18, 42, "Optional reporting channels" },
  { 400, 86, "Status line shown" },
  { 400, 116, "Field mode displays informational lockout" },
};
static const EezMockupButton kButtons_screen_training_sync[] = {
  { 18, 76, 350, 44, "Email On/Off", SCREEN_TRAINING_SYNC },
  { 18, 128, 350, 44, "Cloud On/Off", SCREEN_TRAINING_SYNC },
  { 18, 180, 350, 44, "Cycle target (Auto/Google/SharePoint)", SCREEN_TRAINING_SYNC },
  { 18, 232, 350, 44, "Edit endpoint", SCREEN_TRAINING_SYNC_EDIT },
  { 18, 284, 350, 44, "Edit token", SCREEN_TRAINING_SYNC_EDIT },
  { 18, 336, 350, 44, "Edit cubicle", SCREEN_TRAINING_SYNC_EDIT },
  { 18, 388, 350, 44, "Edit device label", SCREEN_TRAINING_SYNC_EDIT },
  { 18, 440, 350, 44, "Send test ping", SCREEN_TRAINING_SYNC },
  { 18, 492, 350, 44, "Back", SCREEN_SETTINGS },
};

static const EezMockupLabel kLabels_screen_training_sync_edit[] = {
  { 18, 12, "Training sync edit" },
  { 18, 42, "Generic text keyboard editor" },
  { 400, 86, "Used for endpoint/token/cubicle/device label" },
};
static const EezMockupButton kButtons_screen_training_sync_edit[] = {
  { 18, 76, 350, 44, "Keyboard rows", SCREEN_TRAINING_SYNC_EDIT },
  { 18, 128, 350, 44, "Del", SCREEN_TRAINING_SYNC_EDIT },
  { 18, 180, 350, 44, "Aa", SCREEN_TRAINING_SYNC_EDIT },
  { 18, 232, 350, 44, "Save", SCREEN_TRAINING_SYNC },
  { 18, 284, 350, 44, "Back", SCREEN_TRAINING_SYNC },
};

static const EezMockupLabel kLabels_screen_email_settings[] = {
  { 18, 12, "Email settings" },
  { 18, 42, "SMTP + recipient configuration" },
  { 400, 86, "Each save shows confirmation prompt" },
};
static const EezMockupButton kButtons_screen_email_settings[] = {
  { 18, 76, 350, 44, "Edit SMTP server", SCREEN_EMAIL_FIELD_EDIT },
  { 18, 128, 350, 44, "Edit port", SCREEN_EMAIL_FIELD_EDIT },
  { 18, 180, 350, 44, "Edit sender email", SCREEN_EMAIL_FIELD_EDIT },
  { 18, 232, 350, 44, "Edit SMTP password", SCREEN_EMAIL_FIELD_EDIT },
  { 18, 284, 350, 44, "Edit recipient/teacher email", SCREEN_EMAIL_FIELD_EDIT },
  { 18, 336, 350, 44, "Back", SCREEN_SETTINGS },
};

static const EezMockupLabel kLabels_screen_email_field_edit[] = {
  { 18, 12, "Email field edit" },
  { 18, 42, "Per-field keyboard editor" },
  { 400, 86, "Password field masked in display" },
};
static const EezMockupButton kButtons_screen_email_field_edit[] = {
  { 18, 76, 350, 44, "Keyboard rows", SCREEN_EMAIL_FIELD_EDIT },
  { 18, 128, 350, 44, "Del", SCREEN_EMAIL_FIELD_EDIT },
  { 18, 180, 350, 44, "Save", SCREEN_EMAIL_SETTINGS },
  { 18, 232, 350, 44, "Back", SCREEN_EMAIL_SETTINGS },
};

static const EezMockupLabel kLabels_screen_change_pin[] = {
  { 18, 12, "Change PIN" },
  { 18, 42, "Two-step set + confirm" },
  { 400, 86, "PIN min length 4" },
  { 400, 116, "Mismatch prompt shown on failure" },
};
static const EezMockupButton kButtons_screen_change_pin[] = {
  { 18, 76, 350, 44, "Numeric keypad", SCREEN_CHANGE_PIN },
  { 18, 128, 350, 44, "Clear", SCREEN_CHANGE_PIN },
  { 18, 180, 350, 44, "OK", SCREEN_SETTINGS },
  { 18, 232, 350, 44, "Back", SCREEN_SETTINGS },
};

static const EezMockupLabel kLabels_screen_pin_enter[] = {
  { 18, 12, "Enter PIN" },
  { 18, 42, "Admin/settings gate" },
  { 400, 86, "Boot-admin path enforces 3-attempt fallback" },
};
static const EezMockupButton kButtons_screen_pin_enter[] = {
  { 18, 76, 350, 44, "Numeric keypad", SCREEN_PIN_ENTER },
  { 18, 128, 350, 44, "Clear", SCREEN_PIN_ENTER },
  { 18, 180, 350, 44, "OK", SCREEN_SETTINGS },
  { 18, 232, 350, 44, "Back", SCREEN_SETTINGS },
};

static const EezMockupScreen kScreens[] = {
  { SCREEN_MODE_SELECT, "SCREEN_MODE_SELECT", kLabels_screen_mode_select, sizeof(kLabels_screen_mode_select) / sizeof(kLabels_screen_mode_select[0]), kButtons_screen_mode_select, sizeof(kButtons_screen_mode_select) / sizeof(kButtons_screen_mode_select[0]) },
  { SCREEN_MAIN_MENU, "SCREEN_MAIN_MENU", kLabels_screen_main_menu, sizeof(kLabels_screen_main_menu) / sizeof(kLabels_screen_main_menu[0]), kButtons_screen_main_menu, sizeof(kButtons_screen_main_menu) / sizeof(kButtons_screen_main_menu[0]) },
  { SCREEN_TEST_SELECT, "SCREEN_TEST_SELECT", kLabels_screen_test_select, sizeof(kLabels_screen_test_select) / sizeof(kLabels_screen_test_select[0]), kButtons_screen_test_select, sizeof(kButtons_screen_test_select) / sizeof(kButtons_screen_test_select[0]) },
  { SCREEN_STUDENT_ID, "SCREEN_STUDENT_ID", kLabels_screen_student_id, sizeof(kLabels_screen_student_id) / sizeof(kLabels_screen_student_id[0]), kButtons_screen_student_id, sizeof(kButtons_screen_student_id) / sizeof(kButtons_screen_student_id[0]) },
  { SCREEN_TEST_FLOW, "SCREEN_TEST_FLOW", kLabels_screen_test_flow, sizeof(kLabels_screen_test_flow) / sizeof(kLabels_screen_test_flow[0]), kButtons_screen_test_flow, sizeof(kButtons_screen_test_flow) / sizeof(kButtons_screen_test_flow[0]) },
  { SCREEN_REPORT_SAVED, "SCREEN_REPORT_SAVED", kLabels_screen_report_saved, sizeof(kLabels_screen_report_saved) / sizeof(kLabels_screen_report_saved[0]), kButtons_screen_report_saved, sizeof(kButtons_screen_report_saved) / sizeof(kButtons_screen_report_saved[0]) },
  { SCREEN_REPORT_LIST, "SCREEN_REPORT_LIST", kLabels_screen_report_list, sizeof(kLabels_screen_report_list) / sizeof(kLabels_screen_report_list[0]), kButtons_screen_report_list, sizeof(kButtons_screen_report_list) / sizeof(kButtons_screen_report_list[0]) },
  { SCREEN_SETTINGS, "SCREEN_SETTINGS", kLabels_screen_settings, sizeof(kLabels_screen_settings) / sizeof(kLabels_screen_settings[0]), kButtons_screen_settings, sizeof(kButtons_screen_settings) / sizeof(kButtons_screen_settings[0]) },
  { SCREEN_ROTATION, "SCREEN_ROTATION", kLabels_screen_rotation, sizeof(kLabels_screen_rotation) / sizeof(kLabels_screen_rotation[0]), kButtons_screen_rotation, sizeof(kButtons_screen_rotation) / sizeof(kButtons_screen_rotation[0]) },
  { SCREEN_WIFI_LIST, "SCREEN_WIFI_LIST", kLabels_screen_wifi_list, sizeof(kLabels_screen_wifi_list) / sizeof(kLabels_screen_wifi_list[0]), kButtons_screen_wifi_list, sizeof(kButtons_screen_wifi_list) / sizeof(kButtons_screen_wifi_list[0]) },
  { SCREEN_WIFI_PASSWORD, "SCREEN_WIFI_PASSWORD", kLabels_screen_wifi_password, sizeof(kLabels_screen_wifi_password) / sizeof(kLabels_screen_wifi_password[0]), kButtons_screen_wifi_password, sizeof(kButtons_screen_wifi_password) / sizeof(kButtons_screen_wifi_password[0]) },
  { SCREEN_ABOUT, "SCREEN_ABOUT", kLabels_screen_about, sizeof(kLabels_screen_about) / sizeof(kLabels_screen_about[0]), kButtons_screen_about, sizeof(kButtons_screen_about) / sizeof(kButtons_screen_about[0]) },
  { SCREEN_UPDATES, "SCREEN_UPDATES", kLabels_screen_updates, sizeof(kLabels_screen_updates) / sizeof(kLabels_screen_updates[0]), kButtons_screen_updates, sizeof(kButtons_screen_updates) / sizeof(kButtons_screen_updates[0]) },
  { SCREEN_TRAINING_SYNC, "SCREEN_TRAINING_SYNC", kLabels_screen_training_sync, sizeof(kLabels_screen_training_sync) / sizeof(kLabels_screen_training_sync[0]), kButtons_screen_training_sync, sizeof(kButtons_screen_training_sync) / sizeof(kButtons_screen_training_sync[0]) },
  { SCREEN_TRAINING_SYNC_EDIT, "SCREEN_TRAINING_SYNC_EDIT", kLabels_screen_training_sync_edit, sizeof(kLabels_screen_training_sync_edit) / sizeof(kLabels_screen_training_sync_edit[0]), kButtons_screen_training_sync_edit, sizeof(kButtons_screen_training_sync_edit) / sizeof(kButtons_screen_training_sync_edit[0]) },
  { SCREEN_EMAIL_SETTINGS, "SCREEN_EMAIL_SETTINGS", kLabels_screen_email_settings, sizeof(kLabels_screen_email_settings) / sizeof(kLabels_screen_email_settings[0]), kButtons_screen_email_settings, sizeof(kButtons_screen_email_settings) / sizeof(kButtons_screen_email_settings[0]) },
  { SCREEN_EMAIL_FIELD_EDIT, "SCREEN_EMAIL_FIELD_EDIT", kLabels_screen_email_field_edit, sizeof(kLabels_screen_email_field_edit) / sizeof(kLabels_screen_email_field_edit[0]), kButtons_screen_email_field_edit, sizeof(kButtons_screen_email_field_edit) / sizeof(kButtons_screen_email_field_edit[0]) },
  { SCREEN_CHANGE_PIN, "SCREEN_CHANGE_PIN", kLabels_screen_change_pin, sizeof(kLabels_screen_change_pin) / sizeof(kLabels_screen_change_pin[0]), kButtons_screen_change_pin, sizeof(kButtons_screen_change_pin) / sizeof(kButtons_screen_change_pin[0]) },
  { SCREEN_PIN_ENTER, "SCREEN_PIN_ENTER", kLabels_screen_pin_enter, sizeof(kLabels_screen_pin_enter) / sizeof(kLabels_screen_pin_enter[0]), kButtons_screen_pin_enter, sizeof(kButtons_screen_pin_enter) / sizeof(kButtons_screen_pin_enter[0]) },
};

}  // namespace

const EezMockupScreen* EezMockupData_findScreen(ScreenId id) {
  for (size_t i = 0; i < sizeof(kScreens) / sizeof(kScreens[0]); i++) {
    if (kScreens[i].id == id) return &kScreens[i];
  }
  return nullptr;
}
