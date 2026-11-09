# Module link: Ampify.link, between an app and the extension with the same id.
list(APPEND AMPIFY_CORE_SOURCES
    src/core/prelude/link.cpp
    src/core/natives/link.cpp)
list(APPEND AMPIFY_TESTS_SOURCES tests/link_tests.cpp)
