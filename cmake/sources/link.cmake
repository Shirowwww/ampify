# The module link of the AMK object (AMK.link, between an app and the extension with the same id): its JavaScript,
# its natives and its tests. See docs/extensions/API.md, "Organisation du code".
list(APPEND AMK_CORE_SOURCES
    core/prelude/link.cpp
    core/natives/link.cpp)
list(APPEND AMK_TESTS_SOURCES tests/link_tests.cpp)
