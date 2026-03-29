import pathlib

Import("env")

# SPDX-License-Identifier: MIT
# Patch 4.3B board header: touch + CH422G skip I2C host init (Arduino Wire owns bus 0).

ROOT = pathlib.Path(env["PROJECT_DIR"])


def patch_board():
    matches = list(ROOT.glob(".pio/libdeps/**/BOARD_WAVESHARE_ESP32_S3_TOUCH_LCD_4_3_B.h"))
    if not matches:
        return
    board = matches[0]
    text = board.read_text(encoding="utf-8")
    orig = text
    text = text.replace(
        "ESP_PANEL_BOARD_TOUCH_BUS_SKIP_INIT_HOST        (0)",
        "ESP_PANEL_BOARD_TOUCH_BUS_SKIP_INIT_HOST        (1)",
    )
    text = text.replace(
        "ESP_PANEL_BOARD_EXPANDER_SKIP_INIT_HOST     (0)",
        "ESP_PANEL_BOARD_EXPANDER_SKIP_INIT_HOST     (1)",
    )
    if text != orig:
        board.write_text(text, encoding="utf-8")
        print("SparkyCheck: patched 4.3B board I2C SKIP_INIT_HOST for shared Wire bus.")


patch_board()
