# The module react of the AMK object (AMK.React, ReactDOM, hooks, components, ready): its JavaScript, its natives
# and its tests. See docs/extensions/API.md, "Organisation du code".
list(APPEND AMK_CORE_SOURCES
    core/prelude/react.cpp
    core/natives/react.cpp)

list(APPEND AMK_TESTS_SOURCES tests/react_tests.cpp)
