#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "word_bear_model.h"
#include "word_bear_words.h"

int main(void)
{
    assert(WORD_BEAR_WORD_COUNT == 100);
    assert(WORD_BEAR_GROUP_COUNT == 5);
    assert(word_bear_group_start(4) == 80);
    assert(word_bear_group_for_word(99) == 4);
    assert(strcmp(word_bear_word_at(0)->word, "curious") == 0);
    assert(strcmp(word_bear_word_at(99)->word, "future") == 0);
    assert(word_bear_step(0, -1) == WORD_BEAR_WORD_COUNT - 1);
    assert(word_bear_step(WORD_BEAR_WORD_COUNT - 1, 1) == 0);

    word_bear_progress_t words[WORD_BEAR_WORD_COUNT] = { 0 };
    assert(word_bear_category(&words[0], 100) == WORD_BEAR_CATEGORY_UNLEARNED);
    assert(word_bear_count_matching(words, WORD_BEAR_SESSION_NEW, 0, 100) == 20);
    assert(!word_bear_group_complete(words, 0));

    word_bear_record_answer(&words[0], false, 100);
    assert(words[0].learn_count == 1);
    assert(words[0].wrong_count == 1);
    assert((words[0].flags & WORD_BEAR_FLAG_WRONG_ACTIVE) != 0);
    assert((words[0].flags & WORD_BEAR_FLAG_WRONG_HISTORY) != 0);
    assert(word_bear_category(&words[0], 100) == WORD_BEAR_CATEGORY_WRONG);

    word_bear_record_answer(&words[0], true, 100);
    assert(words[0].correct_streak == 1);
    assert(words[0].due_day == 101);
    word_bear_record_answer(&words[0], true, 101);
    assert(words[0].due_day == 104);
    word_bear_record_answer(&words[0], true, 104);
    assert((words[0].flags & WORD_BEAR_FLAG_MASTERED) != 0);
    assert((words[0].flags & WORD_BEAR_FLAG_WRONG_ACTIVE) == 0);
    assert((words[0].flags & WORD_BEAR_FLAG_WRONG_HISTORY) != 0);
    assert(words[0].due_day == 111);
    assert(word_bear_category(&words[0], 111) == WORD_BEAR_CATEGORY_MASTERED);
    assert(word_bear_matches_session(words, 0, WORD_BEAR_SESSION_MASTERED, 0,
                                     111));

    for (int index = 1; index < WORD_BEAR_GROUP_SIZE; ++index) {
        word_bear_record_answer(&words[index], true, 100);
    }
    assert(word_bear_group_complete(words, 0));
    assert(word_bear_count_matching(words, WORD_BEAR_SESSION_NEW, 0, 100) == 0);
    assert(word_bear_count_matching(words, WORD_BEAR_SESSION_NEW, 1, 100) == 20);

    word_bear_progress_t migrated[WORD_BEAR_WORD_COUNT] = { 0 };
    word_bear_migrate_mastered_mask(migrated, (1UL << 3) | (1UL << 19), 200);
    assert(word_bear_mastered_count(migrated) == 2);
    assert(migrated[3].learn_count == WORD_BEAR_MASTER_STREAK);
    assert(migrated[19].due_day == 207);
    assert(migrated[20].learn_count == 0);

    word_bear_progress_t offline_migrated[WORD_BEAR_WORD_COUNT] = { 0 };
    word_bear_migrate_mastered_mask(offline_migrated, 1UL, 0);
    assert(offline_migrated[0].last_day == 0);
    assert(offline_migrated[0].due_day == 0);
    assert(word_bear_anchor_day(&offline_migrated[0], 300));
    assert(offline_migrated[0].last_day == 300);
    assert(offline_migrated[0].due_day == 307);
    assert(!word_bear_anchor_day(&offline_migrated[0], 301));

    word_bear_progress_t offline_wrong = { 0 };
    word_bear_record_answer(&offline_wrong, false, 0);
    assert(word_bear_anchor_day(&offline_wrong, 400));
    assert(offline_wrong.last_day == 400);
    assert(offline_wrong.due_day == 400);
    return 0;
}
