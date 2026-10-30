# Module player on Windows: Apple's player (AMP.Services.Player), the Windows media controls (SMTC) and the media
# keys behind AMK.player, plus the queue capability (core/player_queue.hpp). The portable mapping and command routing
# are in core/player_model.*, tested on Linux; the WinRT calls are in win/, tested under Wine with fake objects.
list(APPEND AMK_CORE_SOURCES core/player_model.cpp core/player_queue.cpp)
list(APPEND AMK_WIN_SOURCES win/winrt_abi.cpp win/amp_player.cpp win/smtc_session.cpp win/player_bridge.cpp)
list(APPEND AMK_TESTS_SOURCES tests/player_model_tests.cpp)
list(APPEND AMK_TESTS_WIN_SOURCES tests/win/amp_player_tests.cpp)
