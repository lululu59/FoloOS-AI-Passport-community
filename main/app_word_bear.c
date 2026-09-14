#include "app.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "coding_bridge.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "lvgl.h"
#include "nvs.h"
#include "ui_pixel.h"
#include "ui_sfx.h"
#include "word_bear_model.h"
#include "word_bear_words.h"

#define WORD_BEAR_NAMESPACE "word_bear"
#define WORD_BEAR_STORE_KEY "progress_v2"
#define WORD_BEAR_MASK_KEY  "mask"
#define WORD_BEAR_INDEX_KEY "index"
#define WORD_BEAR_STORE_MAGIC 0x57425232UL
#define WORD_BEAR_VISITED_BYTES ((WORD_BEAR_WORD_COUNT + 7) / 8)

#define DECLARE_WORD_AUDIO(name) \
    extern const uint8_t name##_wav_start[] asm("_binary_" #name "_wav_start"); \
    extern const uint8_t name##_wav_end[] asm("_binary_" #name "_wav_end")

DECLARE_WORD_AUDIO(curious);
DECLARE_WORD_AUDIO(focus);
DECLARE_WORD_AUDIO(improve);
DECLARE_WORD_AUDIO(create);
DECLARE_WORD_AUDIO(learn);
DECLARE_WORD_AUDIO(brave);
DECLARE_WORD_AUDIO(calm);
DECLARE_WORD_AUDIO(dream);
DECLARE_WORD_AUDIO(enjoy);
DECLARE_WORD_AUDIO(friend);
DECLARE_WORD_AUDIO(grow);
DECLARE_WORD_AUDIO(happy);
DECLARE_WORD_AUDIO(kind);
DECLARE_WORD_AUDIO(listen);
DECLARE_WORD_AUDIO(practice);
DECLARE_WORD_AUDIO(quiet);
DECLARE_WORD_AUDIO(remember);
DECLARE_WORD_AUDIO(simple);
DECLARE_WORD_AUDIO(strong);
DECLARE_WORD_AUDIO(wonder);

typedef struct {
    const uint8_t *start;
    const uint8_t *end;
} fallback_audio_t;

#define FALLBACK_AUDIO(name) { name##_wav_start, name##_wav_end }

static const fallback_audio_t FALLBACK_AUDIO_FILES[WORD_BEAR_GROUP_SIZE] = {
    FALLBACK_AUDIO(curious), FALLBACK_AUDIO(focus),
    FALLBACK_AUDIO(improve), FALLBACK_AUDIO(create),
    FALLBACK_AUDIO(learn), FALLBACK_AUDIO(brave),
    FALLBACK_AUDIO(calm), FALLBACK_AUDIO(dream),
    FALLBACK_AUDIO(enjoy), FALLBACK_AUDIO(friend),
    FALLBACK_AUDIO(grow), FALLBACK_AUDIO(happy),
    FALLBACK_AUDIO(kind), FALLBACK_AUDIO(listen),
    FALLBACK_AUDIO(practice), FALLBACK_AUDIO(quiet),
    FALLBACK_AUDIO(remember), FALLBACK_AUDIO(simple),
    FALLBACK_AUDIO(strong), FALLBACK_AUDIO(wonder),
};

typedef struct {
    uint32_t magic;
    uint32_t revision;
    uint32_t known_day;
    uint16_t schema;
    uint16_t word_count;
    uint16_t current_index;
    uint8_t active_group;
    uint8_t reserved;
    word_bear_progress_t progress[WORD_BEAR_WORD_COUNT];
} word_bear_store_t;

typedef enum {
    WORD_BEAR_VIEW_PLAN = 0,
    WORD_BEAR_VIEW_CARD,
    WORD_BEAR_VIEW_ANSWER,
    WORD_BEAR_VIEW_GROUP_COMPLETE,
} word_bear_view_t;

static const char *TAG = "word_bear";
static portMUX_TYPE s_store_lock = portMUX_INITIALIZER_UNLOCKED;
static word_bear_store_t s_store;
static bool s_loaded;
static word_bear_store_t *s_restore;
static uint8_t s_restore_received[WORD_BEAR_VISITED_BYTES];

static lv_obj_t *s_screen;
static lv_obj_t *s_bear;
static lv_obj_t *s_left_eye;
static lv_obj_t *s_right_eye;
static lv_obj_t *s_plan_rows[4];
static lv_obj_t *s_plan_labels[4];
static lv_obj_t *s_word;
static lv_obj_t *s_part;
static lv_obj_t *s_meaning;
static lv_obj_t *s_example;
static lv_obj_t *s_stats;
static lv_timer_t *s_timer;

static word_bear_view_t s_view;
static word_bear_session_t s_session;
static uint8_t s_visited[WORD_BEAR_VISITED_BYTES];
static uint8_t s_plan_selection;
static uint8_t s_complete_selection;
static uint16_t s_index;
static unsigned s_session_total;
static unsigned s_session_done;
static unsigned s_anim_frame;
static unsigned s_speaking_ticks;

_Static_assert(sizeof(word_bear_store_t) + sizeof(uint32_t) < 2048,
               "word bear NVS blob exceeds its 2 KiB budget");

static uint32_t checksum(const void *data, size_t size)
{
    const uint8_t *bytes = data;
    uint32_t value = 2166136261UL;
    for (size_t index = 0; index < size; ++index) {
        value = (value ^ bytes[index]) * 16777619UL;
    }
    return value;
}

static void store_reset(void)
{
    memset(&s_store, 0, sizeof(s_store));
    s_store.magic = WORD_BEAR_STORE_MAGIC;
    s_store.schema = WORD_BEAR_SCHEMA_VERSION;
    s_store.word_count = WORD_BEAR_WORD_COUNT;
}

static bool save_progress(void)
{
    const size_t data_size = sizeof(s_store);
    uint8_t *blob = malloc(data_size + sizeof(uint32_t));
    if (blob == NULL) return false;

    taskENTER_CRITICAL(&s_store_lock);
    memcpy(blob, &s_store, data_size);
    taskEXIT_CRITICAL(&s_store_lock);
    uint32_t value = checksum(blob, data_size);
    memcpy(blob + data_size, &value, sizeof(value));

    nvs_handle_t handle;
    esp_err_t result = nvs_open(WORD_BEAR_NAMESPACE, NVS_READWRITE, &handle);
    if (result == ESP_OK) {
        result = nvs_set_blob(handle, WORD_BEAR_STORE_KEY, blob,
                              data_size + sizeof(value));
        if (result == ESP_OK) result = nvs_commit(handle);
        nvs_close(handle);
    }
    free(blob);
    if (result != ESP_OK) {
        ESP_LOGW(TAG, "progress save failed: %s", esp_err_to_name(result));
        return false;
    }
    return true;
}

static bool load_v2(nvs_handle_t handle)
{
    size_t blob_size = 0U;
    if (nvs_get_blob(handle, WORD_BEAR_STORE_KEY, NULL, &blob_size) != ESP_OK ||
        blob_size < offsetof(word_bear_store_t, progress) + sizeof(uint32_t) ||
        blob_size > sizeof(word_bear_store_t) + sizeof(uint32_t)) {
        return false;
    }

    uint8_t *blob = malloc(blob_size);
    if (blob == NULL) return false;
    bool valid = nvs_get_blob(handle, WORD_BEAR_STORE_KEY, blob, &blob_size) == ESP_OK;
    uint32_t saved_checksum = 0U;
    if (valid) {
        memcpy(&saved_checksum, blob + blob_size - sizeof(saved_checksum),
               sizeof(saved_checksum));
        valid = saved_checksum == checksum(blob, blob_size - sizeof(saved_checksum));
    }

    word_bear_store_t loaded = { 0 };
    if (valid) {
        size_t data_size = blob_size - sizeof(saved_checksum);
        size_t copy_size = data_size < sizeof(loaded) ? data_size : sizeof(loaded);
        memcpy(&loaded, blob, copy_size);
        size_t stored_records = data_size > offsetof(word_bear_store_t, progress)
                                    ? (data_size - offsetof(word_bear_store_t, progress)) /
                                          sizeof(word_bear_progress_t)
                                    : 0U;
        valid = loaded.magic == WORD_BEAR_STORE_MAGIC &&
                loaded.schema == WORD_BEAR_SCHEMA_VERSION &&
                loaded.word_count == stored_records && stored_records > 0U &&
                stored_records <= WORD_BEAR_WORD_COUNT;
    }
    free(blob);
    if (!valid) return false;

    loaded.word_count = WORD_BEAR_WORD_COUNT;
    if (loaded.current_index >= WORD_BEAR_WORD_COUNT) loaded.current_index = 0U;
    if (loaded.active_group >= WORD_BEAR_GROUP_COUNT) loaded.active_group = 0U;
    s_store = loaded;
    return true;
}

static void load_progress(void)
{
    if (s_loaded) return;
    s_loaded = true;
    store_reset();

    nvs_handle_t handle;
    if (nvs_open(WORD_BEAR_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) return;
    if (!load_v2(handle)) {
        uint32_t legacy_mask = 0U;
        uint8_t legacy_index = 0U;
        bool has_mask = nvs_get_u32(handle, WORD_BEAR_MASK_KEY,
                                    &legacy_mask) == ESP_OK;
        (void)nvs_get_u8(handle, WORD_BEAR_INDEX_KEY, &legacy_index);
        if (has_mask) {
            word_bear_migrate_mastered_mask(s_store.progress, legacy_mask,
                                             s_store.known_day);
            s_store.current_index = legacy_index < WORD_BEAR_GROUP_SIZE
                                        ? legacy_index : 0U;
            ++s_store.revision;
        }
    }
    nvs_close(handle);
    if (s_store.revision > 0U) (void)save_progress();
}

static lv_obj_t *pixel_box(lv_obj_t *parent, int x, int y, int w, int h,
                           uint32_t color)
{
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(box, x, y);
    lv_obj_set_size(box, w, h);
    lv_obj_set_style_pad_all(box, 0, 0);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_set_style_radius(box, 0, 0);
    lv_obj_set_style_bg_color(box, lv_color_hex(color), 0);
    return box;
}

static void draw_bear(int y)
{
    const uint32_t caramel = 0xD8752B;
    const uint32_t brown = 0x71351E;
    const uint32_t cream = 0xFFE4B3;
    const uint32_t blush = 0xFF7486;
    const uint32_t mint = 0x5FE6C7;
    const uint32_t mint_dark = 0x0D8F80;

    s_bear = lv_obj_create(s_screen);
    lv_obj_remove_flag(s_bear, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(s_bear, 58, y);
    lv_obj_set_size(s_bear, 124, 80);
    lv_obj_set_style_pad_all(s_bear, 0, 0);
    lv_obj_set_style_border_width(s_bear, 0, 0);
    lv_obj_set_style_bg_opa(s_bear, LV_OPA_TRANSP, 0);

    pixel_box(s_bear, 12, 8, 28, 25, brown);
    pixel_box(s_bear, 82, 8, 28, 25, brown);
    pixel_box(s_bear, 17, 12, 19, 18, caramel);
    pixel_box(s_bear, 86, 12, 19, 18, caramel);
    pixel_box(s_bear, 24, 13, 74, 54, caramel);
    pixel_box(s_bear, 17, 27, 88, 32, caramel);
    pixel_box(s_bear, 47, 39, 28, 23, cream);
    s_left_eye = pixel_box(s_bear, 37, 33, 7, 10, brown);
    s_right_eye = pixel_box(s_bear, 79, 33, 7, 10, brown);
    pixel_box(s_bear, 58, 45, 7, 6, brown);
    pixel_box(s_bear, 38, 48, 7, 4, blush);
    pixel_box(s_bear, 78, 48, 7, 4, blush);
    pixel_box(s_bear, 24, 60, 74, 18, caramel);
    pixel_box(s_bear, 37, 59, 49, 20, mint_dark);
    pixel_box(s_bear, 40, 57, 43, 18, mint);
    pixel_box(s_bear, 70, 57, 13, 18, 0xDFFFF6);
    pixel_box(s_bear, 49, 62, 25, 4, 0x25BDA8);
}

static lv_obj_t *centered_label(const char *text, int y,
                                const lv_font_t *font, uint32_t color)
{
    lv_obj_t *label = ui_pixel_label(s_screen, text, font, color);
    lv_obj_set_pos(label, 10, y);
    lv_obj_set_width(label, 220);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    return label;
}

static void clear_screen(void)
{
    lv_obj_clean(s_screen);
    s_bear = NULL;
    s_left_eye = NULL;
    s_right_eye = NULL;
    memset(s_plan_rows, 0, sizeof(s_plan_rows));
    memset(s_plan_labels, 0, sizeof(s_plan_labels));
    s_word = NULL;
    s_part = NULL;
    s_meaning = NULL;
    s_example = NULL;
    s_stats = NULL;
}

static void update_plan_selection(void)
{
    for (int index = 0; index < 4; ++index) {
        bool selected = index == s_plan_selection;
        lv_obj_set_style_border_width(s_plan_rows[index], selected ? 2 : 0, 0);
        lv_obj_set_style_text_color(s_plan_labels[index],
                                    lv_color_hex(selected ? UI_WHITE : UI_MUTED), 0);
    }
}

static void show_plan(void)
{
    clear_screen();
    s_view = WORD_BEAR_VIEW_PLAN;
    char text[64];

    snprintf(text, sizeof(text), "单词熊  第%d/%d组",
             s_store.active_group + 1, WORD_BEAR_GROUP_COUNT);
    centered_label(text, 3, ui_menu_font(), UI_WHITE);
    snprintf(text, sizeof(text), "总进度 %u/%d 已掌握",
             word_bear_mastered_count(s_store.progress), WORD_BEAR_WORD_COUNT);
    centered_label(text, 31, ui_cn_font(), UI_PRIMARY);
    draw_bear(43);

    unsigned counts[4] = {
        word_bear_count_matching(s_store.progress, WORD_BEAR_SESSION_WRONG,
                                 s_store.active_group, s_store.known_day),
        word_bear_count_matching(s_store.progress, WORD_BEAR_SESSION_DUE,
                                 s_store.active_group, s_store.known_day),
        word_bear_count_matching(s_store.progress, WORD_BEAR_SESSION_NEW,
                                 s_store.active_group, s_store.known_day),
        word_bear_count_matching(s_store.progress, WORD_BEAR_SESSION_MASTERED,
                                 s_store.active_group, s_store.known_day),
    };
    const char *names[] = { "错词复习", "当日复习", "新词学习", "掌握复查" };
    for (int index = 0; index < 4; ++index) {
        int y = 127 + index * 40;
        s_plan_rows[index] = pixel_box(s_screen, 12, y, 216, 34, UI_SURFACE);
        lv_obj_set_style_border_color(s_plan_rows[index], lv_color_hex(UI_ACCENT), 0);
        snprintf(text, sizeof(text), "%s  %u", names[index], counts[index]);
        s_plan_labels[index] = ui_pixel_label(s_plan_rows[index], text,
                                               ui_cn_font(), UI_WHITE);
        lv_obj_align(s_plan_labels[index], LV_ALIGN_LEFT_MID, 10, 0);
    }
    update_plan_selection();
    centered_label("上下选择  OK确认  长按上返回", 292, ui_cn_font(), UI_MUTED);
}

static bool visited(int index)
{
    return (s_visited[index / 8] & (1U << (index % 8))) != 0U;
}

static void mark_visited(int index)
{
    s_visited[index / 8] |= (uint8_t)(1U << (index % 8));
}

static int next_candidate(void)
{
    for (int index = 0; index < WORD_BEAR_WORD_COUNT; ++index) {
        if (!visited(index) &&
            word_bear_matches_session(s_store.progress, index, s_session,
                                      s_store.active_group, s_store.known_day)) {
            return index;
        }
    }
    return -1;
}

static const char *session_name(word_bear_session_t session)
{
    static const char *const NAMES[] = {
        "新词", "当日复习", "错词", "掌握复查", "本组复习",
    };
    return session >= WORD_BEAR_SESSION_NEW &&
           session <= WORD_BEAR_SESSION_GROUP_REVIEW ? NAMES[session] : "学习";
}

static void show_card_front(void)
{
    clear_screen();
    s_view = WORD_BEAR_VIEW_CARD;
    const word_bear_word_t *entry = word_bear_word_at(s_index);
    char text[64];

    snprintf(text, sizeof(text), "第%d组  %s  %u/%u",
             word_bear_group_for_word(s_index) + 1, session_name(s_session),
             s_session_done + 1U, s_session_total);
    centered_label(text, 3, ui_cn_font(), UI_PRIMARY);
    draw_bear(27);
    s_word = centered_label(entry->word, 116, ui_menu_font(), UI_WHITE);
    s_part = centered_label(entry->part, 144, ui_cn_font(), UI_MUTED);
    s_meaning = centered_label("先想一想它的意思", 176, ui_cn_font(), UI_PRIMARY);
    s_example = centered_label("OK 手动查看答案", 207, ui_cn_font(), UI_MUTED);
    snprintf(text, sizeof(text), "学习%u  对%u  错%u  连对%u",
             s_store.progress[s_index].learn_count,
             s_store.progress[s_index].correct_count,
             s_store.progress[s_index].wrong_count,
             s_store.progress[s_index].correct_streak);
    s_stats = centered_label(text, 252, ui_cn_font(), UI_WHITE);
    centered_label("OK查看答案  长按上返回", 291, ui_cn_font(), UI_MUTED);
}

static void show_answer(void)
{
    s_view = WORD_BEAR_VIEW_ANSWER;
    const word_bear_word_t *entry = word_bear_word_at(s_index);
    lv_label_set_text(s_meaning, entry->meaning);
    lv_label_set_text(s_example, entry->example);
    lv_obj_set_width(s_example, 216);
    lv_obj_set_height(s_example, 44);
    lv_label_set_long_mode(s_example, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(s_example, lv_color_hex(UI_WHITE), 0);
    lv_label_set_text(s_stats, "↑ 会    ↓ 不会    OK 发音");
    lv_obj_set_style_text_color(s_stats, lv_color_hex(UI_ACCENT), 0);
}

static void update_complete_selection(lv_obj_t *first, lv_obj_t *second)
{
    lv_obj_set_style_border_width(first, s_complete_selection == 0U ? 2 : 0, 0);
    lv_obj_set_style_border_width(second, s_complete_selection == 1U ? 2 : 0, 0);
}

static void show_group_complete(void)
{
    clear_screen();
    s_view = WORD_BEAR_VIEW_GROUP_COMPLETE;
    char text[64];
    centered_label("本组完成！", 6, ui_menu_font(), UI_ACCENT);
    draw_bear(48);
    snprintf(text, sizeof(text), "第%d组 20词已学习", s_store.active_group + 1);
    centered_label(text, 146, ui_cn_font(), UI_WHITE);
    snprintf(text, sizeof(text), "总掌握 %u/%d",
             word_bear_mastered_count(s_store.progress), WORD_BEAR_WORD_COUNT);
    centered_label(text, 172, ui_cn_font(), UI_PRIMARY);

    lv_obj_t *first = pixel_box(s_screen, 20, 207, 200, 36, UI_SURFACE);
    lv_obj_t *second = pixel_box(s_screen, 20, 252, 200, 36, UI_SURFACE);
    lv_obj_set_style_border_color(first, lv_color_hex(UI_ACCENT), 0);
    lv_obj_set_style_border_color(second, lv_color_hex(UI_ACCENT), 0);
    lv_obj_t *first_label = ui_pixel_label(first,
        s_store.active_group + 1U < WORD_BEAR_GROUP_COUNT ? "下一组" : "返回计划",
        ui_cn_font(), UI_WHITE);
    lv_obj_t *second_label = ui_pixel_label(second, "继续复习本组",
                                             ui_cn_font(), UI_WHITE);
    lv_obj_align(first_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_align(second_label, LV_ALIGN_CENTER, 0, 0);
    update_complete_selection(first, second);
    s_plan_rows[0] = first;
    s_plan_rows[1] = second;
    centered_label("上下选择  OK确认", 296, ui_cn_font(), UI_MUTED);
}

static void start_session(word_bear_session_t session)
{
    memset(s_visited, 0, sizeof(s_visited));
    s_session = session;
    s_session_total = word_bear_count_matching(s_store.progress, session,
                                                s_store.active_group,
                                                s_store.known_day);
    s_session_done = 0U;
    int candidate = next_candidate();
    if (candidate < 0) {
        if (session == WORD_BEAR_SESSION_NEW &&
            word_bear_group_complete(s_store.progress, s_store.active_group)) {
            show_group_complete();
        } else {
            show_plan();
        }
        return;
    }
    s_index = (uint16_t)candidate;
    show_card_front();
}

static void send_current_progress(void)
{
    if (!coding_bridge_is_connected()) return;
    word_bear_progress_t progress;
    uint32_t revision;
    taskENTER_CRITICAL(&s_store_lock);
    progress = s_store.progress[s_index];
    revision = s_store.revision;
    taskEXIT_CRITICAL(&s_store_lock);
    (void)coding_bridge_send_word_bear_item("word_bear_progress", revision,
                                            s_index, word_bear_word_at(s_index),
                                            &progress);
}

static void answer_current(bool correct)
{
    taskENTER_CRITICAL(&s_store_lock);
    word_bear_record_answer(&s_store.progress[s_index], correct,
                            s_store.known_day);
    s_store.current_index = s_index;
    ++s_store.revision;
    taskEXIT_CRITICAL(&s_store_lock);
    mark_visited(s_index);
    ++s_session_done;
    (void)save_progress();
    send_current_progress();

    int candidate = next_candidate();
    if (candidate >= 0) {
        s_index = (uint16_t)candidate;
        show_card_front();
    } else if (s_session == WORD_BEAR_SESSION_NEW &&
               word_bear_group_complete(s_store.progress,
                                        s_store.active_group)) {
        s_complete_selection = 0U;
        show_group_complete();
    } else {
        show_plan();
    }
}

static void play_word_audio(void)
{
    const word_bear_word_t *entry = word_bear_word_at(s_index);
    s_speaking_ticks = 8U;
    if (coding_bridge_is_connected() &&
        coding_bridge_request_word_audio(s_index, entry->word)) {
        return;
    }
    if (s_index < WORD_BEAR_GROUP_SIZE) {
        const fallback_audio_t *audio = &FALLBACK_AUDIO_FILES[s_index];
        ui_sfx_play_wav(audio->start, (size_t)(audio->end - audio->start));
    } else if (s_stats != NULL) {
        lv_label_set_text(s_stats, "电脑离线  当前词无发音");
        lv_obj_set_style_text_color(s_stats, lv_color_hex(UI_DANGER), 0);
    }
}

static void animate(lv_timer_t *timer)
{
    (void)timer;
    ++s_anim_frame;
    if (s_left_eye != NULL && s_right_eye != NULL) {
        bool blink = s_anim_frame % 12U == 0U;
        lv_obj_set_height(s_left_eye, blink ? 2 : 10);
        lv_obj_set_height(s_right_eye, blink ? 2 : 10);
        lv_obj_set_y(s_left_eye, blink ? 39 : 33);
        lv_obj_set_y(s_right_eye, blink ? 39 : 33);
    }
    if (s_speaking_ticks > 0U) --s_speaking_ticks;
    if (s_bear != NULL && s_speaking_ticks > 0U) {
        int base_y = s_view == WORD_BEAR_VIEW_PLAN ? 43 :
                     s_view == WORD_BEAR_VIEW_GROUP_COMPLETE ? 48 : 27;
        lv_obj_set_y(s_bear, base_y + ((s_anim_frame & 1U) ? -2 : 0));
    }
}

void app_word_bear_set_day(uint32_t day)
{
    if (day == 0U) return;
    load_progress();
    bool changed = false;
    taskENTER_CRITICAL(&s_store_lock);
    if (s_store.known_day != day) {
        s_store.known_day = day;
        changed = true;
    }
    for (int index = 0; index < WORD_BEAR_WORD_COUNT; ++index) {
        changed |= word_bear_anchor_day(&s_store.progress[index], day);
    }
    if (changed) ++s_store.revision;
    taskEXIT_CRITICAL(&s_store_lock);
    if (changed) (void)save_progress();
}

void app_word_bear_sync_request(void)
{
    load_progress();
    word_bear_store_t *snapshot = malloc(sizeof(*snapshot));
    if (snapshot == NULL) return;
    taskENTER_CRITICAL(&s_store_lock);
    *snapshot = s_store;
    taskEXIT_CRITICAL(&s_store_lock);

    bool complete = coding_bridge_send_word_bear_sync_begin(
        snapshot->revision, snapshot->known_day, snapshot->active_group);
    for (int index = 0; complete && index < WORD_BEAR_WORD_COUNT; ++index) {
        complete = coding_bridge_send_word_bear_item(
            "word_bear_sync_item", snapshot->revision, index,
            word_bear_word_at(index), &snapshot->progress[index]);
    }
    if (complete) {
        (void)coding_bridge_send_word_bear_sync_end(snapshot->revision);
    }
    free(snapshot);
}

void app_word_bear_restore_begin(unsigned count, uint32_t day, int active_group)
{
    free(s_restore);
    s_restore = NULL;
    memset(s_restore_received, 0, sizeof(s_restore_received));
    if (count != WORD_BEAR_WORD_COUNT || active_group < 0 ||
        active_group >= WORD_BEAR_GROUP_COUNT) {
        return;
    }
    s_restore = calloc(1, sizeof(*s_restore));
    if (s_restore == NULL) return;
    s_restore->magic = WORD_BEAR_STORE_MAGIC;
    s_restore->schema = WORD_BEAR_SCHEMA_VERSION;
    s_restore->word_count = WORD_BEAR_WORD_COUNT;
    s_restore->known_day = day;
    s_restore->active_group = (uint8_t)active_group;
    s_restore->current_index = (uint16_t)word_bear_group_start(active_group);
}

void app_word_bear_restore_item(int id, uint16_t learn_count,
                                uint16_t correct_count, uint16_t wrong_count,
                                uint32_t last_day, uint32_t due_day,
                                uint8_t correct_streak, uint8_t flags)
{
    if (s_restore == NULL || id < 0 || id >= WORD_BEAR_WORD_COUNT) return;
    word_bear_progress_t *progress = &s_restore->progress[id];
    progress->learn_count = learn_count;
    progress->correct_count = correct_count;
    progress->wrong_count = wrong_count;
    progress->last_day = last_day;
    progress->due_day = due_day;
    progress->correct_streak = correct_streak;
    progress->flags = flags & (WORD_BEAR_FLAG_MASTERED |
                               WORD_BEAR_FLAG_WRONG_ACTIVE |
                               WORD_BEAR_FLAG_WRONG_HISTORY);
    s_restore_received[id / 8] |= (uint8_t)(1U << (id % 8));
}

void app_word_bear_restore_end(void)
{
    if (s_restore == NULL) return;
    for (int id = 0; id < WORD_BEAR_WORD_COUNT; ++id) {
        if ((s_restore_received[id / 8] & (1U << (id % 8))) == 0U) {
            free(s_restore);
            s_restore = NULL;
            return;
        }
    }
    load_progress();
    taskENTER_CRITICAL(&s_store_lock);
    s_restore->revision = s_store.revision + 1U;
    s_store = *s_restore;
    taskEXIT_CRITICAL(&s_store_lock);
    free(s_restore);
    s_restore = NULL;
    (void)save_progress();
    app_word_bear_sync_request();
}

void app_word_bear_enter(void)
{
    load_progress();
    s_anim_frame = 0U;
    s_speaking_ticks = 0U;
    s_index = s_store.current_index;
    s_screen = ui_pixel_bare_screen_create();
    show_plan();
    s_timer = lv_timer_create(animate, 140, NULL);
    lv_screen_load(s_screen);
}

void app_word_bear_exit(void)
{
    (void)save_progress();
    if (s_timer != NULL) {
        lv_timer_delete(s_timer);
        s_timer = NULL;
    }
    if (s_screen != NULL) {
        lv_obj_delete(s_screen);
        s_screen = NULL;
    }
    s_bear = NULL;
    s_left_eye = NULL;
    s_right_eye = NULL;
}

void app_word_bear_key(bsp_btn_t button, bsp_btn_ev_t event)
{
    if (event != BSP_BTN_CLICK) return;

    if (s_view == WORD_BEAR_VIEW_PLAN) {
        if (button == BSP_BTN_UP) {
            s_plan_selection = (uint8_t)((s_plan_selection + 3U) % 4U);
            update_plan_selection();
        } else if (button == BSP_BTN_DOWN) {
            s_plan_selection = (uint8_t)((s_plan_selection + 1U) % 4U);
            update_plan_selection();
        } else if (button == BSP_BTN_OK) {
            static const word_bear_session_t SESSIONS[] = {
                WORD_BEAR_SESSION_WRONG, WORD_BEAR_SESSION_DUE,
                WORD_BEAR_SESSION_NEW, WORD_BEAR_SESSION_MASTERED,
            };
            ui_sfx_play(UI_SFX_CONFIRM);
            start_session(SESSIONS[s_plan_selection]);
        }
        return;
    }

    if (s_view == WORD_BEAR_VIEW_CARD && button == BSP_BTN_OK) {
        ui_sfx_play(UI_SFX_CONFIRM);
        show_answer();
    } else if (s_view == WORD_BEAR_VIEW_ANSWER) {
        if (button == BSP_BTN_UP) {
            ui_sfx_play(UI_SFX_CONFIRM);
            answer_current(true);
        } else if (button == BSP_BTN_DOWN) {
            answer_current(false);
        } else if (button == BSP_BTN_OK) {
            play_word_audio();
        }
    } else if (s_view == WORD_BEAR_VIEW_GROUP_COMPLETE) {
        if (button == BSP_BTN_UP || button == BSP_BTN_DOWN) {
            s_complete_selection ^= 1U;
            update_complete_selection(s_plan_rows[0], s_plan_rows[1]);
        } else if (button == BSP_BTN_OK && s_complete_selection == 1U) {
            ui_sfx_play(UI_SFX_CONFIRM);
            start_session(WORD_BEAR_SESSION_GROUP_REVIEW);
        } else if (button == BSP_BTN_OK) {
            ui_sfx_play(UI_SFX_CONFIRM);
            if (s_store.active_group + 1U < WORD_BEAR_GROUP_COUNT) {
                taskENTER_CRITICAL(&s_store_lock);
                ++s_store.active_group;
                s_store.current_index = (uint16_t)word_bear_group_start(
                    s_store.active_group);
                ++s_store.revision;
                taskEXIT_CRITICAL(&s_store_lock);
                (void)save_progress();
                start_session(WORD_BEAR_SESSION_NEW);
            } else {
                show_plan();
            }
        }
    }
}
