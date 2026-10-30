# Theme engine: target amk_theme, and the checker amk_theme_check. See the convention at the top of CMakeLists.txt.
list(APPEND AMK_THEME_SOURCES
    core/theme/color.cpp
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

list(APPEND AMK_THEME_CHECK_SOURCES tools/theme_check.cpp)
