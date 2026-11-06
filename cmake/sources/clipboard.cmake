# Module clipboard: Ampify.clipboard, text copied to and read from the Windows clipboard (src/win/clipboard.*).
list(APPEND AMPIFY_CORE_SOURCES src/core/prelude/clipboard.cpp src/core/natives/clipboard.cpp)
list(APPEND AMPIFY_WIN_SOURCES src/win/clipboard.cpp)
list(APPEND AMPIFY_TESTS_SOURCES tests/clipboard_tests.cpp)
