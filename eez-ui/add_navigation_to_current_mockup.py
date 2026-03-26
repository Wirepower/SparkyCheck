import json
import pathlib
import uuid


def uid() -> str:
    return str(uuid.uuid4())


NAV_MAP = {
    "SCREEN_MODE_SELECT": {
        "Continue": "SCREEN_MAIN_MENU",
    },
    "SCREEN_MAIN_MENU": {
        "Start verification": "SCREEN_TEST_SELECT",
        "View reports": "SCREEN_REPORT_LIST",
        "Settings": "SCREEN_SETTINGS",
    },
    "SCREEN_TEST_SELECT": {
        "Back": "SCREEN_MAIN_MENU",
        "Earth continuity (conductors)": "SCREEN_TEST_FLOW",
        "Insulation resistance": "SCREEN_TEST_FLOW",
        "Polarity": "SCREEN_TEST_FLOW",
        "Earth continuity (CPC)": "SCREEN_TEST_FLOW",
        "Correct circuit connections": "SCREEN_TEST_FLOW",
        "Earth fault loop impedance": "SCREEN_TEST_FLOW",
        "RCD operation": "SCREEN_TEST_FLOW",
        "SWP D/R (motor)": "SCREEN_TEST_FLOW",
        "SWP D/R (appliance)": "SCREEN_TEST_FLOW",
        "SWP D/R (heater/sheathed)": "SCREEN_TEST_FLOW",
    },
    "SCREEN_STUDENT_ID": {
        "Start": "SCREEN_TEST_FLOW",
        "Back": "SCREEN_TEST_SELECT",
    },
    "SCREEN_TEST_FLOW": {
        "Back": "SCREEN_TEST_SELECT",
        "End session": "SCREEN_REPORT_SAVED",
    },
    "SCREEN_REPORT_SAVED": {
        "OK": "SCREEN_MAIN_MENU",
    },
    "SCREEN_REPORT_LIST": {
        "Back": "SCREEN_MAIN_MENU",
    },
    "SCREEN_SETTINGS": {
        "Screen rotation": "SCREEN_ROTATION",
        "WiFi connection": "SCREEN_WIFI_LIST",
        "About": "SCREEN_ABOUT",
        "Firmware updates": "SCREEN_UPDATES",
        "Email settings": "SCREEN_EMAIL_SETTINGS",
        "Change PIN": "SCREEN_CHANGE_PIN",
        "Back": "SCREEN_MAIN_MENU",
    },
    "SCREEN_ROTATION": {
        "Portrait": "SCREEN_SETTINGS",
        "Landscape": "SCREEN_SETTINGS",
        "Back": "SCREEN_SETTINGS",
    },
    "SCREEN_WIFI_LIST": {
        "Back": "SCREEN_SETTINGS",
    },
    "SCREEN_WIFI_PASSWORD": {
        "Connect": "SCREEN_WIFI_LIST",
        "Back": "SCREEN_WIFI_LIST",
    },
    "SCREEN_ABOUT": {
        "Back": "SCREEN_SETTINGS",
    },
    "SCREEN_UPDATES": {
        "Training sync setup (PIN)": "SCREEN_PIN_ENTER",
        "Back": "SCREEN_SETTINGS",
    },
    "SCREEN_TRAINING_SYNC": {
        "Edit endpoint": "SCREEN_TRAINING_SYNC_EDIT",
        "Edit token": "SCREEN_TRAINING_SYNC_EDIT",
        "Edit cubicle": "SCREEN_TRAINING_SYNC_EDIT",
        "Edit device label": "SCREEN_TRAINING_SYNC_EDIT",
        "Back": "SCREEN_UPDATES",
    },
    "SCREEN_TRAINING_SYNC_EDIT": {
        "Save": "SCREEN_TRAINING_SYNC",
        "Back": "SCREEN_TRAINING_SYNC",
    },
    "SCREEN_EMAIL_SETTINGS": {
        "Edit SMTP server": "SCREEN_EMAIL_FIELD_EDIT",
        "Edit port": "SCREEN_EMAIL_FIELD_EDIT",
        "Edit sender email": "SCREEN_EMAIL_FIELD_EDIT",
        "Edit SMTP password": "SCREEN_EMAIL_FIELD_EDIT",
        "Edit recipient/teacher email": "SCREEN_EMAIL_FIELD_EDIT",
        "Back": "SCREEN_SETTINGS",
    },
    "SCREEN_EMAIL_FIELD_EDIT": {
        "Save": "SCREEN_EMAIL_SETTINGS",
        "Back": "SCREEN_EMAIL_SETTINGS",
    },
    "SCREEN_CHANGE_PIN": {
        "OK": "SCREEN_SETTINGS",
        "Back": "SCREEN_SETTINGS",
    },
    "SCREEN_PIN_ENTER": {
        "OK": "SCREEN_SETTINGS",
        "Back": "SCREEN_SETTINGS",
    },
}


def get_button_label(widget: dict) -> str:
    if widget.get("type") != "LVGLButtonWidget":
        return ""
    for child in widget.get("children", []):
        if child.get("type") == "LVGLLabelWidget":
            return child.get("text", "")
    return ""


def main():
    root = pathlib.Path(__file__).resolve().parents[1]
    project_path = root / "eez-ui" / "SparkyCheck-CurrentApp-Mockup.eez-project"
    obj = json.loads(project_path.read_text())

    for page in obj.get("userPages", []):
        page_name = page.get("name", "")
        if page_name not in NAV_MAP:
            continue

        targets = NAV_MAP[page_name]
        components = page.get("components", [])
        screen = next((c for c in components if c.get("type") == "LVGLScreenWidget"), None)
        if not screen:
            continue

        # Remove old action components/lines for deterministic regen.
        components[:] = [c for c in components if c.get("type") != "LVGLActionComponent"]
        page["connectionLines"] = []

        action_y = 420
        action_x = 20

        for child in screen.get("children", []):
            label = get_button_label(child)
            if not label:
                continue
            target_screen = targets.get(label)
            if not target_screen:
                continue

            action_id = uid()
            action = {
                "objID": action_id,
                "type": "LVGLActionComponent",
                "left": action_x,
                "top": action_y,
                "width": 240,
                "height": 54,
                "customInputs": [],
                "customOutputs": [],
                "actions": [
                    {
                        "objID": uid(),
                        "action": "CHANGE_SCREEN",
                        "screen": target_screen,
                        "fadeMode": "FADE_IN",
                        "speed": 200,
                        "delay": 0,
                    }
                ],
            }
            components.insert(0, action)
            page["connectionLines"].append(
                {
                    "objID": uid(),
                    "source": child.get("objID"),
                    "output": "CLICKED",
                    "target": action_id,
                    "input": "@seqin",
                }
            )
            action_x += 250
            if action_x > 980:
                action_x = 20
                action_y -= 58

    project_path.write_text(json.dumps(obj, indent=2))
    print(f"Updated navigation in {project_path}")


if __name__ == "__main__":
    main()

