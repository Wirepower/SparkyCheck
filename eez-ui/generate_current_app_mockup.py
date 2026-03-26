import json
import pathlib
import uuid


def uid() -> str:
    return str(uuid.uuid4())


BUTTON_FLAGS = (
    "CLICKABLE|CLICK_FOCUSABLE|GESTURE_BUBBLE|PRESS_LOCK|SCROLL_CHAIN_HOR|"
    "SCROLL_CHAIN_VER|SCROLL_ELASTIC|SCROLL_MOMENTUM|SCROLL_ON_FOCUS|"
    "SCROLL_WITH_ARROW|SNAPPABLE"
)
LABEL_FLAGS = (
    "CLICK_FOCUSABLE|GESTURE_BUBBLE|PRESS_LOCK|SCROLLABLE|SCROLL_CHAIN_HOR|"
    "SCROLL_CHAIN_VER|SCROLL_ELASTIC|SCROLL_MOMENTUM|SCROLL_WITH_ARROW|SNAPPABLE"
)
SCREEN_FLAGS = (
    "CLICKABLE|CLICK_FOCUSABLE|GESTURE_BUBBLE|PRESS_LOCK|SCROLLABLE|"
    "SCROLL_CHAIN_HOR|SCROLL_CHAIN_VER|SCROLL_ELASTIC|SCROLL_MOMENTUM|"
    "SCROLL_WITH_ARROW|SNAPPABLE"
)


def style_ref():
    return {
        "objID": uid(),
        "useStyle": "default",
        "conditionalStyles": [],
        "childStyles": [],
    }


def local_style_empty():
    return {"objID": uid()}


def make_label(text, x, y, w=0, h=0, align_center=False):
    ls = local_style_empty()
    if align_center:
        ls = {"objID": uid(), "definition": {"MAIN": {"DEFAULT": {"align": "CENTER"}}}}
    return {
        "objID": uid(),
        "type": "LVGLLabelWidget",
        "left": x,
        "top": y,
        "width": w,
        "height": h,
        "customInputs": [],
        "customOutputs": [],
        "style": style_ref(),
        "timeline": [],
        "eventHandlers": [],
        "leftUnit": "px",
        "topUnit": "px",
        "widthUnit": "content" if w == 0 else "px",
        "heightUnit": "content" if h == 0 else "px",
        "children": [],
        "widgetFlags": LABEL_FLAGS,
        "hiddenFlagType": "literal",
        "clickableFlagType": "literal",
        "checkedStateType": "literal",
        "disabledStateType": "literal",
        "states": "",
        "useStyle": "default",
        "localStyles": ls,
        "text": text,
        "textType": "literal",
        "longMode": "WRAP",
        "recolor": False,
    }


def make_button(text, x, y, w=220, h=50):
    lbl = make_label(text, 0, 0, align_center=True)
    lbl.pop("useStyle", None)
    return {
        "objID": uid(),
        "type": "LVGLButtonWidget",
        "left": x,
        "top": y,
        "width": w,
        "height": h,
        "customInputs": [],
        "customOutputs": [],
        "style": style_ref(),
        "timeline": [],
        "eventHandlers": [],
        "leftUnit": "px",
        "topUnit": "px",
        "widthUnit": "px",
        "heightUnit": "px",
        "children": [lbl],
        "widgetFlags": BUTTON_FLAGS,
        "hiddenFlagType": "literal",
        "clickableFlag": True,
        "clickableFlagType": "literal",
        "checkedStateType": "literal",
        "disabledStateType": "literal",
        "states": "",
        "localStyles": local_style_empty(),
    }


def make_screen(children):
    return {
        "objID": uid(),
        "type": "LVGLScreenWidget",
        "left": 0,
        "top": 0,
        "width": 800,
        "height": 480,
        "customInputs": [],
        "customOutputs": [],
        "style": style_ref(),
        "timeline": [],
        "eventHandlers": [],
        "leftUnit": "px",
        "topUnit": "px",
        "widthUnit": "px",
        "heightUnit": "px",
        "children": children,
        "widgetFlags": SCREEN_FLAGS,
        "hiddenFlagType": "literal",
        "clickableFlag": True,
        "clickableFlagType": "literal",
        "checkedStateType": "literal",
        "disabledStateType": "literal",
        "states": "",
        "localStyles": local_style_empty(),
    }


def make_page(name, children):
    return {
        "objID": uid(),
        "components": [make_screen(children)],
        "connectionLines": [],
        "localVariables": [],
        "name": name,
        "left": 0,
        "top": 0,
        "width": 1280,
        "height": 720,
    }


def blocks(page_title, subtitle, button_labels, right_notes):
    children = [
        make_label(page_title, 18, 12),
        make_label(subtitle, 18, 42),
    ]
    y = 76
    for lbl in button_labels:
        children.append(make_button(lbl, 18, y, 350, 44))
        y += 52
    y2 = 86
    for note in right_notes:
        children.append(make_label(note, 400, y2))
        y2 += 30
    return children


def build_pages():
    return [
        make_page(
            "SCREEN_MODE_SELECT",
            blocks(
                "Select mode",
                "Boot-admin path selection",
                ["Training (apprentice/supervised)", "Field (qualified electrician)", "Continue"],
                ["Green-highlight selected mode", "Saved to NVS", "Default mode: Field"],
            ),
        ),
        make_page(
            "SCREEN_MAIN_MENU",
            blocks(
                "Main menu",
                "Current running app home",
                ["Start verification", "View reports", "Settings"],
                ["Mode indicator at top", "Buttons produce buzzer click (if enabled)"],
            ),
        ),
        make_page(
            "SCREEN_TEST_SELECT",
            blocks(
                "Select test",
                "All verification + SWP topics",
                [
                    "Earth continuity (conductors)",
                    "Insulation resistance",
                    "Polarity",
                    "Earth continuity (CPC)",
                    "Correct circuit connections",
                    "Earth fault loop impedance",
                    "RCD operation",
                    "SWP D/R (motor)",
                    "SWP D/R (appliance)",
                    "SWP D/R (heater/sheathed)",
                ],
                ["Back button top-right", "Training mode redirects to Student ID first"],
            ),
        ),
        make_page(
            "SCREEN_STUDENT_ID",
            blocks(
                "Student ID",
                "Digits only, normalized to S#######",
                ["1 2 3", "4 5 6", "7 8 9", "Del 0 Start", "Back"],
                ["Required before tests in Training mode", "Session resets at test start"],
            ),
        ),
        make_page(
            "SCREEN_TEST_FLOW",
            blocks(
                "Test flow",
                "Dynamic step page + result mode",
                ["Yes", "No", "OK", "Numeric keypad + units", "Confirm / End session"],
                [
                    "Shows clause, instruction, step index",
                    "RCD shows required criterion",
                    "Back supports step rewind and edits",
                ],
            ),
        ),
        make_page(
            "SCREEN_REPORT_SAVED",
            blocks(
                "Report saved",
                "CSV + HTML generated",
                ["OK"],
                ["Shows basename of saved report", "Training sync event: session_saved"],
            ),
        ),
        make_page(
            "SCREEN_REPORT_LIST",
            blocks(
                "Reports list",
                "Recent saved reports",
                ["Back"],
                ["Scrollable list area in current app logic"],
            ),
        ),
        make_page(
            "SCREEN_SETTINGS",
            blocks(
                "Settings",
                "PIN-gated in Training mode",
                [
                    "Screen rotation",
                    "WiFi connection",
                    "Buzzer (sound)",
                    "About",
                    "Firmware updates",
                    "Email settings",
                    "Mode change (boot hold)",
                    "Change PIN",
                    "Back",
                ],
                ["All changes show confirmation prompts", "Training settings unlock on PIN success"],
            ),
        ),
        make_page(
            "SCREEN_ROTATION",
            blocks(
                "Screen orientation",
                "Display rotation picker",
                ["Portrait", "Landscape", "Back"],
                ["Setting saved prompt shown on change"],
            ),
        ),
        make_page(
            "SCREEN_WIFI_LIST",
            blocks(
                "WiFi",
                "Scan and choose SSID",
                ["Scan", "SSID row(s)", "Back"],
                ["Connected SSID/IP shown", "Tap SSID opens password screen"],
            ),
        ),
        make_page(
            "SCREEN_WIFI_PASSWORD",
            blocks(
                "WiFi password",
                "On-screen keyboard entry",
                ["Keyboard rows", "Del", "Connect", "Back"],
                ["Connection result prompt shown (success/fail)"],
            ),
        ),
        make_page(
            "SCREEN_ABOUT",
            blocks(
                "About SparkyCheck",
                "Product and standards summary",
                ["Back"],
                ["Shows standards in current mode", "Shows rules version"],
            ),
        ),
        make_page(
            "SCREEN_UPDATES",
            blocks(
                "Firmware updates",
                "OTA status and controls",
                ["Check now", "Install now", "Toggle auto-check", "Toggle auto-install", "Training sync setup (PIN)", "Back"],
                ["Current firmware + status", "Pending version when available"],
            ),
        ),
        make_page(
            "SCREEN_TRAINING_SYNC",
            blocks(
                "Training sync",
                "Optional reporting channels",
                [
                    "Email On/Off",
                    "Cloud On/Off",
                    "Cycle target (Auto/Google/SharePoint)",
                    "Edit endpoint",
                    "Edit token",
                    "Edit cubicle",
                    "Edit device label",
                    "Send test ping",
                    "Back",
                ],
                ["Status line shown", "Field mode displays informational lockout"],
            ),
        ),
        make_page(
            "SCREEN_TRAINING_SYNC_EDIT",
            blocks(
                "Training sync edit",
                "Generic text keyboard editor",
                ["Keyboard rows", "Del", "Aa", "Save", "Back"],
                ["Used for endpoint/token/cubicle/device label"],
            ),
        ),
        make_page(
            "SCREEN_EMAIL_SETTINGS",
            blocks(
                "Email settings",
                "SMTP + recipient configuration",
                ["Edit SMTP server", "Edit port", "Edit sender email", "Edit SMTP password", "Edit recipient/teacher email", "Back"],
                ["Each save shows confirmation prompt"],
            ),
        ),
        make_page(
            "SCREEN_EMAIL_FIELD_EDIT",
            blocks(
                "Email field edit",
                "Per-field keyboard editor",
                ["Keyboard rows", "Del", "Save", "Back"],
                ["Password field masked in display"],
            ),
        ),
        make_page(
            "SCREEN_CHANGE_PIN",
            blocks(
                "Change PIN",
                "Two-step set + confirm",
                ["Numeric keypad", "Clear", "OK", "Back"],
                ["PIN min length 4", "Mismatch prompt shown on failure"],
            ),
        ),
        make_page(
            "SCREEN_PIN_ENTER",
            blocks(
                "Enter PIN",
                "Admin/settings gate",
                ["Numeric keypad", "Clear", "OK", "Back"],
                ["Boot-admin path enforces 3-attempt fallback"],
            ),
        ),
    ]


def main():
    root = pathlib.Path(__file__).resolve().parents[1]
    template_path = root / "eez-ui" / "SparkyCheck-Mockup-UI.eez-project"
    target_path = root / "eez-ui" / "SparkyCheck-CurrentApp-Mockup.eez-project"

    obj = json.loads(template_path.read_text())
    obj["objID"] = uid()
    obj["settings"]["general"]["objID"] = uid()
    obj["settings"]["general"]["description"] = (
        "SparkyCheck current application screen-by-screen mockup project for EEZ Studio."
    )
    obj["settings"]["general"]["keywords"] = "sparkycheck,current-ui,mockup,eez,lvgl"
    obj["settings"]["general"]["flowSupport"] = False

    obj["actions"] = []
    obj["variables"] = {"objID": uid(), "globalVariables": [], "structures": [], "enums": []}
    obj["userWidgets"] = []
    obj["userPages"] = build_pages()

    target_path.write_text(json.dumps(obj, indent=2))
    print(f"Generated {target_path}")


if __name__ == "__main__":
    main()

