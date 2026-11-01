# Module discord: Ampify.discord (Rich Presence). Portable framing in src/core/discord_frame.*, the pipe client in
# src/win/discord_ipc.*. See docs/extensions/api.md, "Organisation du code".
list(APPEND AMPIFY_CORE_SOURCES src/core/discord_frame.cpp src/core/prelude/discord.cpp src/core/natives/discord.cpp)
list(APPEND AMPIFY_WIN_SOURCES src/win/discord_ipc.cpp)
list(APPEND AMPIFY_TESTS_SOURCES tests/discord_tests.cpp)
