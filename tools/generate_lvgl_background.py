#!/usr/bin/env python3
"""Normalize a generated 3:4 menu chrome image and emit an LVGL RGB565 asset."""

from __future__ import annotations

import argparse
from pathlib import Path

from PIL import Image, ImageEnhance


WIDTH = 240
HEIGHT = 320


def rgb565_bytes(image: Image.Image) -> bytes:
    result = bytearray()
    for red, green, blue in image.convert("RGB").getdata():
        value = ((red & 0xF8) << 8) | ((green & 0xFC) << 3) | (blue >> 3)
        result.extend((value & 0xFF, value >> 8))
    return bytes(result)


def write_source(path: Path, image: Image.Image) -> None:
    data = rgb565_bytes(image)
    lines = []
    for offset in range(0, len(data), 16):
        values = ", ".join(f"0x{value:02x}" for value in data[offset:offset + 16])
        lines.append(f"    {values},")
    body = "\n".join(lines)
    path.write_text(
        f'''#include "ui_menu_chrome.h"

static const uint8_t ui_menu_chrome_map[] = {{
{body}
}};

const lv_image_dsc_t ui_menu_chrome = {{
    .header.magic = LV_IMAGE_HEADER_MAGIC,
    .header.cf = LV_COLOR_FORMAT_RGB565,
    .header.flags = 0,
    .header.w = {WIDTH},
    .header.h = {HEIGHT},
    .header.stride = {WIDTH * 2},
    .data_size = sizeof(ui_menu_chrome_map),
    .data = ui_menu_chrome_map,
}};
''',
        encoding="utf-8",
    )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--preview", type=Path, required=True)
    parser.add_argument("--c-file", type=Path, required=True)
    parser.add_argument("--h-file", type=Path, required=True)
    args = parser.parse_args()

    source = Image.open(args.source).convert("RGB")
    source = source.resize((WIDTH, HEIGHT), Image.Resampling.BOX)
    source = ImageEnhance.Brightness(source).enhance(1.45)
    source = ImageEnhance.Color(source).enhance(1.20)
    source = source.quantize(colors=32, method=Image.Quantize.MAXCOVERAGE,
                             dither=Image.Dither.NONE).convert("RGB")
    source.save(args.preview, optimize=True)
    write_source(args.c_file, source)
    args.h_file.write_text(
        '#pragma once\n\n#include "lvgl.h"\n\nLV_IMAGE_DECLARE(ui_menu_chrome);\n',
        encoding="utf-8",
    )


if __name__ == "__main__":
    main()
