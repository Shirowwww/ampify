# The module react of the Ampify object (Ampify.React, ReactDOM, hooks, components, ready): its JavaScript, its natives
# and its tests. See docs/extensions/api.md, "Organisation du code".
list(APPEND AMPIFY_CORE_SOURCES
    core/prelude/react.cpp
    core/natives/react.cpp)

list(APPEND AMPIFY_TESTS_SOURCES tests/react_tests.cpp)
