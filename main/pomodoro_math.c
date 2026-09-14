#include "pomodoro_math.h"

int pomodoro_seconds_left(uint64_t now_ms, uint64_t deadline_ms)
{
    if (deadline_ms <= now_ms) return 0;
    uint64_t remaining_ms = deadline_ms - now_ms;
    return (int)((remaining_ms + 999) / 1000);
}

int pomodoro_progress_percent(int remaining_seconds)
{
    if (remaining_seconds <= 0) return 100;
    if (remaining_seconds >= POMODORO_SESSION_SECONDS) return 0;
    return (POMODORO_SESSION_SECONDS - remaining_seconds) * 100 /
           POMODORO_SESSION_SECONDS;
}

int pomodoro_pet_stage(uint32_t completed_sessions)
{
    if (completed_sessions >= 12) return 2;
    if (completed_sessions >= 4) return 1;
    return 0;
}

int pomodoro_stage_progress(uint32_t completed_sessions)
{
    if (completed_sessions >= 12) return 100;
    if (completed_sessions >= 4) {
        return (int)((completed_sessions - 4) * 100 / 8);
    }
    return (int)(completed_sessions * 25);
}
