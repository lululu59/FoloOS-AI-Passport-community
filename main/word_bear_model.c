#include "word_bear_model.h"

#include <limits.h>
#include <stddef.h>

_Static_assert(sizeof(word_bear_progress_t) == 16,
               "word progress must remain compact");
_Static_assert(WORD_BEAR_WORD_COUNT % WORD_BEAR_GROUP_SIZE == 0,
               "word groups must be complete");

static uint16_t increment_u16(uint16_t value)
{
    return value == UINT16_MAX ? value : (uint16_t)(value + 1U);
}

static uint32_t review_interval(uint8_t streak)
{
    static const uint8_t DAYS[] = { 1, 3, 7, 14, 30 };
    unsigned index = streak == 0U ? 0U : (unsigned)streak - 1U;
    if (index >= sizeof(DAYS)) index = sizeof(DAYS) - 1U;
    return DAYS[index];
}

int word_bear_step(int current, int delta)
{
    int next = (current + delta) % WORD_BEAR_WORD_COUNT;
    return next < 0 ? next + WORD_BEAR_WORD_COUNT : next;
}

int word_bear_group_start(int group)
{
    if (group < 0) return 0;
    if (group >= WORD_BEAR_GROUP_COUNT) group = WORD_BEAR_GROUP_COUNT - 1;
    return group * WORD_BEAR_GROUP_SIZE;
}

int word_bear_group_for_word(int index)
{
    if (index < 0 || index >= WORD_BEAR_WORD_COUNT) return -1;
    return index / WORD_BEAR_GROUP_SIZE;
}

bool word_bear_group_complete(const word_bear_progress_t *progress, int group)
{
    if (progress == NULL || group < 0 || group >= WORD_BEAR_GROUP_COUNT) {
        return false;
    }
    int start = word_bear_group_start(group);
    for (int index = start; index < start + WORD_BEAR_GROUP_SIZE; ++index) {
        if (progress[index].learn_count == 0U) return false;
    }
    return true;
}

void word_bear_record_answer(word_bear_progress_t *progress, bool correct,
                             uint32_t day)
{
    if (progress == NULL) return;
    progress->learn_count = increment_u16(progress->learn_count);
    progress->last_day = day;

    if (!correct) {
        progress->wrong_count = increment_u16(progress->wrong_count);
        progress->correct_streak = 0U;
        progress->flags &= (uint8_t)~WORD_BEAR_FLAG_MASTERED;
        progress->flags |= WORD_BEAR_FLAG_WRONG_ACTIVE |
                           WORD_BEAR_FLAG_WRONG_HISTORY;
        progress->due_day = day;
        return;
    }

    progress->correct_count = increment_u16(progress->correct_count);
    if (progress->correct_streak < UINT8_MAX) ++progress->correct_streak;
    if (progress->correct_streak >= WORD_BEAR_MASTER_STREAK) {
        progress->flags |= WORD_BEAR_FLAG_MASTERED;
        progress->flags &= (uint8_t)~WORD_BEAR_FLAG_WRONG_ACTIVE;
    }
    progress->due_day = day == 0U ? 0U :
                        day + review_interval(progress->correct_streak);
}

bool word_bear_anchor_day(word_bear_progress_t *progress, uint32_t day)
{
    if (progress == NULL || day == 0U || progress->learn_count == 0U ||
        progress->last_day != 0U) {
        return false;
    }
    progress->last_day = day;
    if ((progress->flags & WORD_BEAR_FLAG_WRONG_ACTIVE) != 0U) {
        progress->due_day = day;
    } else {
        progress->due_day = day + review_interval(progress->correct_streak);
    }
    return true;
}

word_bear_category_t word_bear_category(const word_bear_progress_t *progress,
                                        uint32_t day)
{
    if (progress == NULL || progress->learn_count == 0U) {
        return WORD_BEAR_CATEGORY_UNLEARNED;
    }
    if ((progress->flags & WORD_BEAR_FLAG_WRONG_ACTIVE) != 0U) {
        return WORD_BEAR_CATEGORY_WRONG;
    }
    if ((progress->flags & WORD_BEAR_FLAG_MASTERED) != 0U) {
        return WORD_BEAR_CATEGORY_MASTERED;
    }
    if (day != 0U && progress->due_day != 0U && progress->due_day <= day) {
        return WORD_BEAR_CATEGORY_DUE;
    }
    return WORD_BEAR_CATEGORY_LEARNING;
}

bool word_bear_matches_session(const word_bear_progress_t *progress, int index,
                               word_bear_session_t session, int active_group,
                               uint32_t day)
{
    if (progress == NULL || index < 0 || index >= WORD_BEAR_WORD_COUNT) {
        return false;
    }
    const word_bear_progress_t *item = &progress[index];
    bool in_group = word_bear_group_for_word(index) == active_group;
    switch (session) {
        case WORD_BEAR_SESSION_NEW:
            return in_group && item->learn_count == 0U;
        case WORD_BEAR_SESSION_DUE:
            return item->learn_count > 0U &&
                   (item->flags & (WORD_BEAR_FLAG_MASTERED |
                                   WORD_BEAR_FLAG_WRONG_ACTIVE)) == 0U &&
                   day != 0U && item->due_day != 0U && item->due_day <= day;
        case WORD_BEAR_SESSION_WRONG:
            return (item->flags & WORD_BEAR_FLAG_WRONG_ACTIVE) != 0U;
        case WORD_BEAR_SESSION_MASTERED:
            return (item->flags & WORD_BEAR_FLAG_MASTERED) != 0U &&
                   day != 0U && item->due_day != 0U && item->due_day <= day;
        case WORD_BEAR_SESSION_GROUP_REVIEW:
            return in_group && item->learn_count > 0U;
        default:
            return false;
    }
}

unsigned word_bear_count_matching(const word_bear_progress_t *progress,
                                  word_bear_session_t session,
                                  int active_group, uint32_t day)
{
    if (progress == NULL) return 0U;
    unsigned count = 0U;
    for (int index = 0; index < WORD_BEAR_WORD_COUNT; ++index) {
        if (word_bear_matches_session(progress, index, session, active_group,
                                      day)) {
            ++count;
        }
    }
    return count;
}

unsigned word_bear_mastered_count(const word_bear_progress_t *progress)
{
    if (progress == NULL) return 0U;
    unsigned count = 0U;
    for (int index = 0; index < WORD_BEAR_WORD_COUNT; ++index) {
        if ((progress[index].flags & WORD_BEAR_FLAG_MASTERED) != 0U) ++count;
    }
    return count;
}

void word_bear_migrate_mastered_mask(word_bear_progress_t *progress,
                                     uint32_t mastered_mask, uint32_t day)
{
    if (progress == NULL) return;
    for (int index = 0; index < 20; ++index) {
        if ((mastered_mask & (1UL << index)) == 0U) continue;
        progress[index].learn_count = WORD_BEAR_MASTER_STREAK;
        progress[index].correct_count = WORD_BEAR_MASTER_STREAK;
        progress[index].correct_streak = WORD_BEAR_MASTER_STREAK;
        progress[index].flags |= WORD_BEAR_FLAG_MASTERED;
        progress[index].last_day = day;
        progress[index].due_day = day == 0U ? 0U : day + 7U;
    }
}
