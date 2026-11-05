# The mini player: its model, layout, placement, colours and feed (portable, tested on Linux), and its window, a XAML
# window on its own interface thread (src/win/mini_window.*, AMPIFY_XAML_SOURCES). See src/core/mini_player.hpp.
list(APPEND AMPIFY_CORE_SOURCES src/core/mini_player.cpp)
list(APPEND AMPIFY_TESTS_SOURCES tests/mini_player_tests.cpp)
list(APPEND AMPIFY_XAML_SOURCES src/win/mini_window.cpp)
