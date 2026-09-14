#pragma once

#include <stdbool.h>
#include <stdint.h>

#define WORD_BEAR_SCHEMA_VERSION 2U
#define WORD_BEAR_WORD_COUNT 100
#define WORD_BEAR_GROUP_SIZE 20
#define WORD_BEAR_GROUP_COUNT (WORD_BEAR_WORD_COUNT / WORD_BEAR_GROUP_SIZE)
#define WORD_BEAR_MASTER_STREAK 3U

enum {
    WORD_BEAR_FLAG_MASTERED = 1U << 0,
    WORD_BEAR_FLAG_WRONG_ACTIVE = 1U << 1,
    WORD_BEAR_FLAG_WRONG_HISTORY = 1U << 2,
};

typedef enum {
    WORD_BEAR_CATEGORY_UNLEARNED = 0,
    WORD_BEAR_CATEGORY_LEARNING,
    WORD_BEAR_CATEGORY_DUE,
    WORD_BEAR_CATEGORY_WRONG,
    WORD_BEAR_CATEGORY_MASTERED,
} word_bear_category_t;

typedef enum {
    WORD_BEAR_SESSION_NEW = 0,
    WORD_BEAR_SESSION_DUE,
    WORD_BEAR_SESSION_WRONG,
    WORD_BEAR_SESSION_MASTERED,
    WORD_BEAR_SESSION_GROUP_REVIEW,
} word_bear_session_t;

typedef struct {
    uint32_t last_day;
    uint32_t due_day;
    uint16_t learn_count;
    uint16_t correct_count;
    uint16_t wrong_count;
    uint8_t correct_streak;
    uint8_t flags;
} word_bear_progress_t;

int word_bear_step(int current, int delta);
int word_bear_group_start(int group);
int word_bear_group_for_word(int index);
bool word_bear_group_complete(const word_bear_progress_t *progress, int group);
void word_bear_record_answer(word_bear_progress_t *progress, bool correct,
                             uint32_t day);
bool word_bear_anchor_day(word_bear_progress_t *progress, uint32_t day);
word_bear_category_t word_bear_category(const word_bear_progress_t *progress,
                                        uint32_t day);
bool word_bear_matches_session(const word_bear_progress_t *progress, int index,
                               word_bear_session_t session, int active_group,
                               uint32_t day);
unsigned word_bear_count_matching(const word_bear_progress_t *progress,
                                  word_bear_session_t session,
                                  int active_group, uint32_t day);
unsigned word_bear_mastered_count(const word_bear_progress_t *progress);
void word_bear_migrate_mastered_mask(word_bear_progress_t *progress,
                                     uint32_t mastered_mask, uint32_t day);
