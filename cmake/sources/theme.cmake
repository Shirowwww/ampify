# Theme engine: target ampify_theme, and the checker ampify_theme_check. See the convention at the top of CMakeLists.txt.
list(APPEND AMPIFY_THEME_SOURCES
    core/theme/builtins.cpp
    core/theme/color.cpp
    core/theme/colour_keys.cpp
    core/theme/css.cpp
    core/theme/engine.cpp
    core/theme/files.cpp
    core/theme/ini.cpp
    core/theme/inspector.cpp
    core/theme/library.cpp
    core/theme/properties.cpp
    core/theme/selector.cpp
    core/theme/session.cpp
    core/theme/theme.cpp
    core/theme/theme_settings.cpp
    core/theme/commands.cpp
    core/theme/fonts.cpp
    core/theme/hover.cpp
    core/theme/layout.cpp
    core/theme/scheme_choice.cpp
    core/theme/snippets.cpp
    core/theme/spicetify.cpp
    core/theme/worker.cpp)

list(APPEND AMPIFY_THEME_CHECK_SOURCES tools/theme_check.cpp)
