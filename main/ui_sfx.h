#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    UI_SFX_MOVE = 0,
    UI_SFX_CONFIRM,
    UI_SFX_APPROVAL_SPEECH,
    UI_SFX_DONE_SPEECH,
} ui_sfx_t;

void ui_sfx_init(bool audio_available);
void ui_sfx_set_volume(uint8_t percent);
void ui_sfx_play(ui_sfx_t effect);
void ui_sfx_play_wav(const uint8_t *wav, size_t wav_size);
