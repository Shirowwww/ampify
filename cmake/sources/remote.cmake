# Module remote: Ampify.remote and the local remote control. Portable HTTP parser and security rules in
# src/core/remote_http.*, the routes in src/core/remote_protocol.*, the 127.0.0.1 server in src/win/remote_server.*.
list(APPEND AMPIFY_CORE_SOURCES src/core/remote_http.cpp src/core/remote_protocol.cpp src/core/prelude/remote.cpp src/core/natives/remote.cpp)
list(APPEND AMPIFY_WIN_SOURCES src/win/remote_server.cpp)
list(APPEND AMPIFY_TESTS_SOURCES tests/remote_tests.cpp)
