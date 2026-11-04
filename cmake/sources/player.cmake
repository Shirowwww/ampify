# Module player on Windows: Apple's player (AMP.Services.Player), the Windows media controls (SMTC) and the media
# keys behind Ampify.player, plus the queue capability (src/core/player_queue.hpp). The portable mapping and command routing
# are in src/core/player_model.*, tested on Linux; the WinRT calls are in win/, tested on Windows with fake objects.
list(APPEND AMPIFY_CORE_SOURCES src/core/player_model.cpp src/core/player_queue.cpp src/core/lyrics.cpp)
list(APPEND AMPIFY_WIN_SOURCES src/win/winrt_abi.cpp src/win/amp_player.cpp src/win/amp_item.cpp src/win/smtc_session.cpp src/win/player_bridge.cpp)
list(APPEND AMPIFY_TESTS_SOURCES tests/player_model_tests.cpp tests/lyrics_tests.cpp tests/player_modes_tests.cpp)
list(APPEND AMPIFY_TESTS_WIN_SOURCES tests/win/amp_player_tests.cpp)
