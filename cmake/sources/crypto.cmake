# Module crypto: Ampify.crypto.md5 (src/core/md5.*), and the POST side of Ampify.http whose tests share the file.
# Base64 (src/core/base64.*) and form bodies (src/core/form_data.*) serve Ampify.http.postForm and Ampify.player.artwork.
list(APPEND AMPIFY_CORE_SOURCES src/core/md5.cpp src/core/base64.cpp src/core/form_data.cpp src/core/prelude/crypto.cpp src/core/natives/crypto.cpp)
list(APPEND AMPIFY_TESTS_SOURCES tests/crypto_http_tests.cpp tests/http_async_tests.cpp tests/form_data_tests.cpp)
