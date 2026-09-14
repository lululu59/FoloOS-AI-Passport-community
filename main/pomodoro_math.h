#pragma once

#include <stdint.h>

#define POMODORO_SESSION_SECONDS (25 * 60)

int pomodoro_seconds_left(uint64_t now_ms, uint64_t deadline_ms);
int pomodoro_progress_percent(int remaining_seconds);
int pomodoro_pet_stage(uint32_t completed_sessions);
int pomodoro_stage_progress(uint32_t completed_sessions);
