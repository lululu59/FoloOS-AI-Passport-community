#pragma once

#include "word_bear_model.h"

typedef struct {
    const char *word;
    const char *part;
    const char *meaning;
    const char *example;
} word_bear_word_t;

const word_bear_word_t *word_bear_word_at(int index);
