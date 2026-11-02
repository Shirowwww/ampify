# Theme engine: target ampify_theme. See the convention at the top of CMakeLists.txt.
list(APPEND AMPIFY_THEME_SOURCES
    src/core/theme/animation.cpp
    src/core/theme/builtins.cpp
    src/core/theme/color.cpp
    src/core/theme/colour_keys.cpp
    src/core/theme/css.cpp
    src/core/theme/engine.cpp
    src/core/theme/files.cpp
    src/core/theme/ini.cpp
    src/core/theme/inspector.cpp
    src/core/theme/library.cpp
    src/core/theme/properties.cpp
    src/core/theme/selector.cpp
    src/core/theme/session.cpp
    src/core/theme/theme.cpp
    src/core/theme/theme_settings.cpp
    src/core/theme/fonts.cpp
    src/core/theme/hover.cpp
    src/core/theme/layout.cpp
    src/core/theme/report.cpp
    src/core/theme/scheme_choice.cpp
    src/core/theme/snippets.cpp
    src/core/theme/spicetify.cpp
    src/core/theme/worker.cpp)

list(APPEND AMPIFY_TESTS_SOURCES tests/animation_tests.cpp)

