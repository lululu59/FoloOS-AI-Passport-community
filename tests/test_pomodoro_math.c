#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "pomodoro_math.h"

int main(void)
{
    assert(pomodoro_seconds_left(1000, 26000) == 25);
    assert(pomodoro_seconds_left(1001, 26000) == 25);
    assert(pomodoro_seconds_left(25999, 26000) == 1);
    assert(pomodoro_seconds_left(26000, 26000) == 0);

    assert(pomodoro_progress_percent(POMODORO_SESSION_SECONDS) == 0);
    assert(pomodoro_progress_percent(POMODORO_SESSION_SECONDS / 2) == 50);
    assert(pomodoro_progress_percent(0) == 100);

    assert(pomodoro_pet_stage(0) == 0);
    assert(pomodoro_pet_stage(4) == 1);
    assert(pomodoro_pet_stage(12) == 2);
    assert(pomodoro_stage_progress(2) == 50);
    assert(pomodoro_stage_progress(8) == 50);
    assert(pomodoro_stage_progress(20) == 100);

    puts("pomodoro math tests passed");
    return 0;
}
