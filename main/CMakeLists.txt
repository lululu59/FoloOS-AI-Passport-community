# Disabled applications and their embedded assets must not enter the image.
set(optional_app_sources "")
set(app_audio "audio/approval.wav" "audio/done.wav")
if(CONFIG_FOLOOS_APP_POMODORO)
    list(APPEND optional_app_sources "app_pomodoro.c" "pomodoro_math.c")
endif()
if(CONFIG_FOLOOS_APP_WORD_BEAR)
    list(APPEND optional_app_sources "app_word_bear.c" "word_bear_model.c" "word_bear_words.c")
    list(APPEND app_audio
        "audio/word_bear/curious.wav"
        "audio/word_bear/focus.wav"
        "audio/word_bear/improve.wav"
        "audio/word_bear/create.wav"
        "audio/word_bear/learn.wav"
        "audio/word_bear/brave.wav"
        "audio/word_bear/calm.wav"
        "audio/word_bear/dream.wav"
        "audio/word_bear/enjoy.wav"
        "audio/word_bear/friend.wav"
        "audio/word_bear/grow.wav"
        "audio/word_bear/happy.wav"
        "audio/word_bear/kind.wav"
        "audio/word_bear/listen.wav"
        "audio/word_bear/practice.wav"
        "audio/word_bear/quiet.wav"
        "audio/word_bear/remember.wav"
        "audio/word_bear/simple.wav"
        "audio/word_bear/strong.wav"
        "audio/word_bear/wonder.wav"
    )
endif()

idf_component_register(
    SRCS ${optional_app_sources} "main.c"
         "ui_pixel.c"
         "ui_pixel_math.c"
         "coding_flow.c"
         "coding_bridge.c"
         "wireless_bridge.c"
         "app_coding.c"
         "app_settings.c"
         "ui_icons.c"
         "ui_menu_chrome.c"
         "ui_sfx.c"
         "lv_font_cn_16.c"
         "lv_font_cn_20.c"
    INCLUDE_DIRS "."
    REQUIRES bsp esp_driver_usb_serial_jtag esp_timer json mbedtls app_update
             esp_wifi esp_netif esp_event nvs_flash
             esp_http_server
    EMBED_FILES ${app_audio}
)

# lv_font_conv emits a dual include guard.  ESP-IDF exposes LVGL as lvgl.h,
# so select the simple include branch for the generated font sources.
target_compile_definitions(${COMPONENT_LIB} PRIVATE LV_LVGL_H_INCLUDE_SIMPLE)
