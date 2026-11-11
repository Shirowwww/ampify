# AMPIFY_TESTS_WIN_SOURCES is added when targeting Windows. A package may list its own tests in its own file with
# list(APPEND AMPIFY_TESTS_SOURCES ...).
list(APPEND AMPIFY_TESTS_SOURCES
    tests/main.cpp
    tests/core_tests.cpp
    tests/settings_catalog_tests.cpp
    tests/text_util_tests.cpp
    tests/atmos_tests.cpp
    tests/apple_music_tests.cpp
    tests/policy_tests.cpp
    tests/cli_tests.cpp
    tests/json_tests.cpp
    tests/manifest_tests.cpp
    tests/js_runtime_tests.cpp
    tests/script_extension_tests.cpp
    tests/loader_tests.cpp
    tests/pipeline_tests.cpp
    tests/theme_tests.cpp
    tests/replay_tests.cpp
    tests/theme_names_tests.cpp
    tests/replay_report_tests.cpp
    tests/replay_cli_tests.cpp
    tests/replay_contrast_tests.cpp
    tests/replay_removal_tests.cpp
    tests/theme_color_animation_tests.cpp
    tests/gain_board_tests.cpp
    tests/sidebar_hold_tests.cpp
    tests/song_room_tests.cpp
    tests/presence_tests.cpp
    tests/playbar_place_tests.cpp
    tests/progress_mark_tests.cpp
    tests/hover_readout_tests.cpp
    tests/merged_snippets_tests.cpp
    tests/lyrics_window_tests.cpp
    tests/caption_clearance_tests.cpp
    tests/sidebar_groups_tests.cpp
    tests/sidebar_entry_tests.cpp
    tests/topbar_tests.cpp
    tests/value_codec_tests.cpp
    tests/player_watch_tests.cpp
    tests/ui_model_tests.cpp
    tests/ui_element_tests.cpp
    tests/theme_script_tests.cpp
    tests/reload_tests.cpp
    tests/signature_tests.cpp
    tests/watch_tests.cpp
    tests/kit_tests.cpp
    tests/api_module_tests.cpp
    tests/api_extras_tests.cpp
    tests/parity_tests.cpp
    tests/ui_watchdog_tests.cpp
    tests/orphan_agent_tests.cpp
    tests/reliability_tests.cpp
    tests/off_notice_tests.cpp)

list(APPEND AMPIFY_TESTS_WIN_SOURCES tests/win_host_tests.cpp tests/win_common_tests.cpp tests/win/kernel_claim_tests.cpp
    tests/win/ui_thread_tests.cpp tests/win/hook_lock_tests.cpp)
