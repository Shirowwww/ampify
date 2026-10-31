# Unit tests: target ampify_tests (when AMPIFY_BUILD_TESTS is on). AMPIFY_TESTS_WIN_SOURCES is added when targeting
# Windows. A package may list its own tests in its own file with list(APPEND AMPIFY_TESTS_SOURCES ...). See the
# convention at the top of CMakeLists.txt.
list(APPEND AMPIFY_TESTS_SOURCES
    tests/main.cpp
    tests/core_tests.cpp
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
    tests/gain_board_tests.cpp
    tests/player_watch_tests.cpp
    tests/ui_model_tests.cpp
    tests/ui_element_tests.cpp
    tests/theme_script_tests.cpp
    tests/reload_tests.cpp
    tests/signature_tests.cpp
    tests/watch_tests.cpp
    tests/kit_tests.cpp
    tests/api_module_tests.cpp
    tests/parity_tests.cpp)

list(APPEND AMPIFY_TESTS_WIN_SOURCES tests/win_host_tests.cpp)
