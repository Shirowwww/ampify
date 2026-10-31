# Module crypto: Ampify.crypto.md5 (core/md5.*), and the POST side of Ampify.http whose tests share the file.
list(APPEND AMPIFY_CORE_SOURCES core/md5.cpp core/prelude/crypto.cpp core/natives/crypto.cpp)
list(APPEND AMPIFY_TESTS_SOURCES tests/crypto_http_tests.cpp)
