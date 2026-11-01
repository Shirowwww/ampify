# The module link of the Ampify object (Ampify.link, between an app and the extension with the same id): its JavaScript,
# its natives and its tests. See docs/extensions/api.md, "Organisation du code".
list(APPEND AMPIFY_CORE_SOURCES
    src/core/prelude/link.cpp
    src/core/natives/link.cpp)
list(APPEND AMPIFY_TESTS_SOURCES tests/link_tests.cpp)
