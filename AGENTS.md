# FoloOS contributor instructions

Read README.md, DEVELOPMENT.md and docs/AI_HARDWARE_DEVELOPMENT_GUIDE.md first.
Use ESP-IDF 5.5.3 with target esp32c3. Hardware constants live in
components/bsp/include/bsp_pins.h. The board has 8 MB Flash and no PSRAM.

Keep hardware logic in components/bsp and application code in main/app_*.c.
Follow main/app.h: enter, exit and key. Append new applications to APPS[];
preserve the existing four entries and their indices. Long UP returns home.
Use four-space C indentation, snake_case, static for internal symbols and s_
for file-local state. Never block the LVGL/button callback with audio or networking.

Before changes, inspect git status and preserve existing edits. Run idf.py build
and applicable existing tests. Report build/host results separately from physical
device checks. Keep tests silent; do not flash a device unless requested.
Do not commit credentials, pairing state, personal paths, logs or generated builds.
