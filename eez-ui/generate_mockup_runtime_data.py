import json
import pathlib
import re


ROOT = pathlib.Path(__file__).resolve().parents[1]
PROJECT_PATH = ROOT / "eez-ui" / "SparkyCheck-CurrentApp-Mockup.eez-project"
OUT_H = ROOT / "include" / "EezMockupData.h"
OUT_CPP = ROOT / "src" / "eez_mockup" / "EezMockupData.cpp"


def esc(text: str) -> str:
    return text.replace("\\", "\\\\").replace('"', '\\"').replace("\n", " ")


def main() -> None:
    obj = json.loads(PROJECT_PATH.read_text())

    screen_names = []
    for page in obj.get("userPages", []):
        name = page.get("name", "")
        if name.startswith("SCREEN_"):
            screen_names.append(name)

    # Preserve order and uniqueness.
    ordered = []
    seen = set()
    for name in screen_names:
        if name in seen:
            continue
        seen.add(name)
        ordered.append(name)
    screen_names = ordered

    pages = {}
    for page in obj.get("userPages", []):
        page_name = page.get("name", "")
        if page_name not in screen_names:
            continue
        components = page.get("components", [])
        screen_widget = next((c for c in components if c.get("type") == "LVGLScreenWidget"), None)
        if not screen_widget:
            continue

        action_target = {}
        for comp in components:
            if comp.get("type") != "LVGLActionComponent":
                continue
            aid = comp.get("objID", "")
            target = None
            for action in comp.get("actions", []):
                if action.get("action") == "CHANGE_SCREEN":
                    screen = action.get("screen", "")
                    if screen in screen_names:
                        target = screen
                        break
            action_target[aid] = target

        button_targets = {}
        for line in page.get("connectionLines", []):
            src = line.get("source", "")
            tgt = line.get("target", "")
            if not src or not tgt:
                continue
            resolved = action_target.get(tgt)
            if resolved:
                button_targets[src] = resolved

        labels = []
        buttons = []
        for child in screen_widget.get("children", []):
            child_type = child.get("type")
            if child_type == "LVGLLabelWidget":
                text = child.get("text", "")
                if not text:
                    continue
                labels.append(
                    {
                        "x": int(child.get("left", 0) or 0),
                        "y": int(child.get("top", 0) or 0),
                        "text": text,
                    }
                )
            elif child_type == "LVGLButtonWidget":
                text = ""
                for grandchild in child.get("children", []):
                    if grandchild.get("type") == "LVGLLabelWidget":
                        text = grandchild.get("text", "")
                        break
                buttons.append(
                    {
                        "x": int(child.get("left", 0) or 0),
                        "y": int(child.get("top", 0) or 0),
                        "w": int(child.get("width", 0) or 0),
                        "h": int(child.get("height", 0) or 0),
                        "text": text,
                        "target": button_targets.get(child.get("objID", "")),
                    }
                )

        pages[page_name] = {"labels": labels, "buttons": buttons}

    h_lines = [
        "#pragma once",
        "",
        "#include <stddef.h>",
        "#include <stdint.h>",
        '#include "Screens.h"',
        "",
        "typedef struct {",
        "  int16_t x;",
        "  int16_t y;",
        "  const char* text;",
        "} EezMockupLabel;",
        "",
        "typedef struct {",
        "  int16_t x;",
        "  int16_t y;",
        "  int16_t w;",
        "  int16_t h;",
        "  const char* text;",
        "  ScreenId target;",
        "} EezMockupButton;",
        "",
        "typedef struct {",
        "  ScreenId id;",
        "  const char* name;",
        "  const EezMockupLabel* labels;",
        "  size_t labelCount;",
        "  const EezMockupButton* buttons;",
        "  size_t buttonCount;",
        "} EezMockupScreen;",
        "",
        "const EezMockupScreen* EezMockupData_findScreen(ScreenId id);",
    ]

    cpp_lines = ['#include "EezMockupData.h"', "", "namespace {"]

    for screen_name in screen_names:
        page_data = pages.get(screen_name, {"labels": [], "buttons": []})
        slug = re.sub(r"[^a-zA-Z0-9_]+", "_", screen_name.lower())

        cpp_lines.append(f"static const EezMockupLabel kLabels_{slug}[] = {{")
        for label in page_data["labels"]:
            cpp_lines.append(
                f'  {{ {label["x"]}, {label["y"]}, "{esc(label["text"])}" }},'
            )
        cpp_lines.append("};")

        cpp_lines.append(f"static const EezMockupButton kButtons_{slug}[] = {{")
        for button in page_data["buttons"]:
            target = button["target"] if button["target"] else screen_name
            cpp_lines.append(
                f'  {{ {button["x"]}, {button["y"]}, {button["w"]}, {button["h"]}, "{esc(button["text"])}", {target} }},'
            )
        cpp_lines.append("};")
        cpp_lines.append("")

    cpp_lines.append("static const EezMockupScreen kScreens[] = {")
    for screen_name in screen_names:
        slug = re.sub(r"[^a-zA-Z0-9_]+", "_", screen_name.lower())
        cpp_lines.append(
            f'  {{ {screen_name}, "{screen_name}", kLabels_{slug}, sizeof(kLabels_{slug}) / sizeof(kLabels_{slug}[0]), '
            f'kButtons_{slug}, sizeof(kButtons_{slug}) / sizeof(kButtons_{slug}[0]) }},'
        )
    cpp_lines.append("};")
    cpp_lines.append("")
    cpp_lines.append("}  // namespace")
    cpp_lines.append("")
    cpp_lines.append("const EezMockupScreen* EezMockupData_findScreen(ScreenId id) {")
    cpp_lines.append("  for (size_t i = 0; i < sizeof(kScreens) / sizeof(kScreens[0]); i++) {")
    cpp_lines.append("    if (kScreens[i].id == id) return &kScreens[i];")
    cpp_lines.append("  }")
    cpp_lines.append("  return nullptr;")
    cpp_lines.append("}")

    OUT_H.parent.mkdir(parents=True, exist_ok=True)
    OUT_CPP.parent.mkdir(parents=True, exist_ok=True)
    OUT_H.write_text("\n".join(h_lines) + "\n")
    OUT_CPP.write_text("\n".join(cpp_lines) + "\n")
    print(f"Generated {OUT_H}")
    print(f"Generated {OUT_CPP}")


if __name__ == "__main__":
    main()
