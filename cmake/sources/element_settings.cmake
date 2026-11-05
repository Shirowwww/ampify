# The settings card of an element as portable logic: groups, search, reset values, and the settings file that shares
# one element's setup (src/core/element_settings.cpp) and its tests. See the convention at the top of CMakeLists.txt.
list(APPEND AMPIFY_CORE_SOURCES src/core/element_settings.cpp)
list(APPEND AMPIFY_TESTS_SOURCES tests/element_settings_tests.cpp tests/manifest_settings_tests.cpp)
