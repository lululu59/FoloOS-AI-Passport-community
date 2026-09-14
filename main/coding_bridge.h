#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "coding_flow.h"
#include "esp_err.h"
#include "word_bear_model.h"
#include "word_bear_words.h"

esp_err_t coding_bridge_init(void);
void coding_bridge_send_record_start(void);
void coding_bridge_send_record_stop(void);
bool coding_bridge_send_voice(const char *text);
void coding_bridge_send_approval(coding_decision_t decision);
void coding_bridge_send_cancel(void);
void coding_bridge_request_projects(void);
void coding_bridge_select_project(const char *id);
void coding_bridge_select_thread(const char *id);
void coding_bridge_create_thread(void);
bool coding_bridge_is_connected(void);
bool coding_bridge_request_word_audio(int id, const char *word);
bool coding_bridge_send_word_bear_sync_begin(uint32_t revision, uint32_t day,
                                             int active_group);
bool coding_bridge_send_word_bear_item(const char *type, uint32_t revision,
                                       int id, const word_bear_word_t *word,
                                       const word_bear_progress_t *progress);
bool coding_bridge_send_word_bear_sync_end(uint32_t revision);
