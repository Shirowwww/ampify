# Module remote: AMK.remote and the local remote control. Portable parser and router in core/remote_protocol.*, the
# 127.0.0.1 server in win/remote_server.*.
list(APPEND AMK_CORE_SOURCES core/remote_protocol.cpp core/prelude/remote.cpp core/natives/remote.cpp)
list(APPEND AMK_WIN_SOURCES win/remote_server.cpp)
list(APPEND AMK_TESTS_SOURCES tests/remote_tests.cpp)
