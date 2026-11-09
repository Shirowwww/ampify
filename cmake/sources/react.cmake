# Module react: Ampify.React, ReactDOM, hooks, components, ready.
list(APPEND AMPIFY_CORE_SOURCES
    src/core/prelude/react.cpp
    src/core/natives/react.cpp)

list(APPEND AMPIFY_TESTS_SOURCES tests/react_tests.cpp)
