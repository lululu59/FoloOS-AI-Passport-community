#!/usr/bin/env python3
"""Normalize true-pixel mascots and emit compact LVGL ARGB8888 assets."""

from __future__ import annotations

import argparse
from collections import deque
from pathlib import Path

from PIL import Image


LOGICAL_SIZE = 32
OUTPUT_SIZE = 64
INNER_SIZE = 29
BACKGROUND = (2, 8, 23)


def foreground_bounds(source: Image.Image) -> tuple[int, int, int, int]:
    """Find the meaningful sprite while ignoring small decorative sparkles."""
    probe = source.convert("RGB").resize((256, 256), Image.Resampling.BOX)
    hsv = probe.convert("HSV")
    mask = [value > 45 for _, _, value in hsv.getdata()]
    seen = bytearray(256 * 256)
    components: list[tuple[int, int, int, int, int]] = []

    for start, enabled in enumerate(mask):
        if not enabled or seen[start]:
            continue
        queue = deque([start])
        seen[start] = 1
        count = 0
        min_x = min_y = 255
        max_x = max_y = 0
        while queue:
            index = queue.popleft()
            y, x = divmod(index, 256)
            count += 1
            min_x = min(min_x, x)
            max_x = max(max_x, x)
            min_y = min(min_y, y)
            max_y = max(max_y, y)
            for ny in range(max(0, y - 1), min(256, y + 2)):
                for nx in range(max(0, x - 1), min(256, x + 2)):
                    candidate = ny * 256 + nx
                    if mask[candidate] and not seen[candidate]:
                        seen[candidate] = 1
                        queue.append(candidate)
        components.append((count, min_x, min_y, max_x, max_y))

    if not components:
        return (0, 0, source.width, source.height)

    largest = max(component[0] for component in components)
    kept = [component for component in components if component[0] >= largest * 0.04]
    min_x = min(component[1] for component in kept)
    min_y = min(component[2] for component in kept)
    max_x = max(component[3] for component in kept)
    max_y = max(component[4] for component in kept)

    scale_x = source.width / 256.0
    scale_y = source.height / 256.0
    padding = max(8, int(min(source.width, source.height) * 0.018))
    return (
        max(0, int(min_x * scale_x) - padding),
        max(0, int(min_y * scale_y) - padding),
        min(source.width, int((max_x + 1) * scale_x) + padding),
        min(source.height, int((max_y + 1) * scale_y) + padding),
    )


def remove_generated_background(crop: Image.Image) -> Image.Image:
    rgba = crop.convert("RGBA")
    pixels = []
    for red, green, blue, _ in rgba.getdata():
        distance = max(abs(red - BACKGROUND[0]),
                       abs(green - BACKGROUND[1]),
                       abs(blue - BACKGROUND[2]))
        if distance <= 14:
            alpha = 0
        elif distance >= 38:
            alpha = 255
        else:
            alpha = int((distance - 14) * 255 / 24)
        pixels.append((red, green, blue, alpha))
    rgba.putdata(pixels)
    return rgba


def normalize_icon(source_path: Path, output_path: Path) -> Image.Image:
    source = Image.open(source_path).convert("RGBA")
    alpha = source.getchannel("A")
    alpha_bounds = alpha.getbbox()
    if alpha_bounds is not None and alpha.getextrema()[0] == 0:
        crop = source.crop(alpha_bounds)
    else:
        crop = remove_generated_background(source.crop(foreground_bounds(source)))

    # These inputs have already passed through the pixel-art fixer.  A smooth
    # resample here would soften one-pixel eyes and mouths on the real LCD.
    crop.thumbnail((INNER_SIZE, INNER_SIZE), Image.Resampling.NEAREST)

    logical = Image.new("RGBA", (LOGICAL_SIZE, LOGICAL_SIZE), (0, 0, 0, 0))
    x = (LOGICAL_SIZE - crop.width) // 2
    y = LOGICAL_SIZE - 1 - crop.height
    logical.alpha_composite(crop, (x, y))
    output = logical.resize((OUTPUT_SIZE, OUTPUT_SIZE), Image.Resampling.NEAREST)
    output.save(output_path, optimize=True)
    return output


def argb8888_bytes(image: Image.Image) -> bytes:
    result = bytearray()
    for red, green, blue, alpha in image.convert("RGBA").getdata():
        result.extend((blue, green, red, alpha))
    return bytes(result)


def c_array(name: str, image: Image.Image) -> str:
    data = argb8888_bytes(image)
    lines = []
    for offset in range(0, len(data), 16):
        values = ", ".join(f"0x{value:02x}" for value in data[offset:offset + 16])
        lines.append(f"    {values},")
    body = "\n".join(lines)
    return f"""static const uint8_t {name}_map[] = {{
{body}
}};

const lv_image_dsc_t {name} = {{
    .header.magic = LV_IMAGE_HEADER_MAGIC,
    .header.cf = LV_COLOR_FORMAT_ARGB8888,
    .header.flags = 0,
    .header.w = {OUTPUT_SIZE},
    .header.h = {OUTPUT_SIZE},
    .header.stride = {OUTPUT_SIZE * 4},
    .data_size = sizeof({name}_map),
    .data = {name}_map,
}};
"""


def write_header(path: Path, names: list[str]) -> None:
    declarations = "\n".join(f"LV_IMAGE_DECLARE({name});" for name in names)
    path.write_text(
        "#pragma once\n\n#include \"lvgl.h\"\n\n" + declarations + "\n",
        encoding="utf-8",
    )


def write_source(path: Path, images: dict[str, Image.Image]) -> None:
    chunks = ["#include \"ui_icons.h\"\n"]
    chunks.extend(c_array(name, image) for name, image in images.items())
    path.write_text("\n".join(chunks), encoding="utf-8")


def write_preview(path: Path, images: dict[str, Image.Image]) -> None:
    preview = Image.new("RGB", (320, 72), BACKGROUND)
    positions = [12, 92, 172, 252]
    for x, image in zip(positions, images.values()):
        preview.paste(image, (x, 4), image)
    preview.resize((1280, 288), Image.Resampling.NEAREST).save(path, optimize=True)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--coding", type=Path, required=True)
    parser.add_argument("--pomodoro", type=Path, required=True)
    parser.add_argument("--settings", type=Path, required=True)
    parser.add_argument("--word-bear", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--c-file", type=Path, required=True)
    parser.add_argument("--h-file", type=Path, required=True)
    args = parser.parse_args()

    args.output_dir.mkdir(parents=True, exist_ok=True)
    sources = {
        "ui_icon_coding": args.coding,
        "ui_icon_pomodoro": args.pomodoro,
        "ui_icon_settings": args.settings,
        "ui_icon_word_bear": args.word_bear,
    }
    images = {}
    for name, source_path in sources.items():
        output_path = args.output_dir / f"{name}.png"
        images[name] = normalize_icon(source_path, output_path)

    write_header(args.h_file, list(images))
    write_source(args.c_file, images)
    write_preview(args.output_dir / "icon-set-preview.png", images)


if __name__ == "__main__":
    main()
