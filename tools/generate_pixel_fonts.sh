#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
CONVERTER="${LV_FONT_CONV:-lv_font_conv}"
FONT="${FOLOOS_FONT:-$ROOT/third_party/fusion-pixel-font/fusion-pixel-12px-proportional-zh_hans.otf}"
COMMON_SYMBOLS='、。！？：％，；“”‘’（）【】《》—…·「」『』〈〉￥'

SYMBOLS=$(find "$ROOT/main" -maxdepth 1 -name '*.c' \
    ! -name 'lv_font*.c' ! -name 'ui_icons.c' ! -name 'ui_menu_chrome.c' \
    -print0 | xargs -0 perl -CSD -ne \
    'while (/(\p{Han})/g) { $h{$1}=1 } END { print sort keys %h }')

# 16 px is used by live bridge text.  It needs the complete CJK Unified
# Ideographs block because transcripts and Codex approval questions are not
# known at firmware-build time.  The 20 px menu font stays a compact subset.
"$CONVERTER" --format lvgl --bpp 1 --size 16 \
    --font "$FONT" --range 0x20-0x7e,0x25b6,0x4e00-0x9fff --symbols "$COMMON_SYMBOLS" \
    --no-kerning --no-compress --lv-font-name lv_font_cn_16 \
    -o "$ROOT/main/lv_font_cn_16.c"

"$CONVERTER" --format lvgl --bpp 1 --size 20 \
    --font "$FONT" --range 0x20-0x7e,0x25b6 --symbols "${SYMBOLS}${COMMON_SYMBOLS}" \
    --no-kerning --no-compress --lv-font-name lv_font_cn_20 \
    -o "$ROOT/main/lv_font_cn_20.c"
