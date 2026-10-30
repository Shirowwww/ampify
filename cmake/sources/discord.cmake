# Module discord: AMK.discord (Rich Presence). Portable framing in core/discord_frame.*, the pipe client in
# win/discord_ipc.*. See docs/extensions/API.md, "Organisation du code".
list(APPEND AMK_CORE_SOURCES core/discord_frame.cpp core/prelude/discord.cpp core/natives/discord.cpp)
list(APPEND AMK_WIN_SOURCES win/discord_ipc.cpp)
list(APPEND AMK_TESTS_SOURCES tests/discord_tests.cpp)
