# Module crypto: AMK.crypto.md5 (core/md5.*), and the POST side of AMK.http whose tests share the file.
list(APPEND AMK_CORE_SOURCES core/md5.cpp core/prelude/crypto.cpp core/natives/crypto.cpp)
list(APPEND AMK_TESTS_SOURCES tests/crypto_http_tests.cpp)
