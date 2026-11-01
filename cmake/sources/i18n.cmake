# Module i18n: Ampify.i18n, and the English texts of Ampify (src/core/i18n.*) and of the command line
# (src/cli/messages.cpp).
list(APPEND AMPIFY_CORE_SOURCES src/core/i18n.cpp src/core/prelude/i18n.cpp src/core/natives/i18n.cpp)
list(APPEND AMPIFY_CLI_PORTABLE_SOURCES src/cli/messages.cpp)
list(APPEND AMPIFY_TESTS_SOURCES tests/i18n_tests.cpp)
