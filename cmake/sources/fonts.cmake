# Module fonts: Ampify.fonts, the font roles of the applied theme (src/core/font_roles.*). The theme engine resolves the
# roles a theme sets (src/core/theme/fonts.*, package theme) and its service publishes them.
list(APPEND AMPIFY_CORE_SOURCES src/core/font_roles.cpp src/core/prelude/fonts.cpp src/core/natives/fonts.cpp)
list(APPEND AMPIFY_TESTS_SOURCES tests/fonts_tests.cpp)
