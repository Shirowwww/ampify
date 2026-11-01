# Module fonts: Ampify.fonts, the font roles of the applied theme (core/font_roles.*). The theme engine resolves the
# roles a theme sets (core/theme/fonts.*, package theme) and its service publishes them.
list(APPEND AMPIFY_CORE_SOURCES core/font_roles.cpp core/prelude/fonts.cpp core/natives/fonts.cpp)
list(APPEND AMPIFY_TESTS_SOURCES tests/fonts_tests.cpp)
