# Module i18n: Ampify.i18n, and the English texts of Ampify (core/i18n.*) and of the command line
# (cli/messages.cpp).
list(APPEND AMPIFY_CORE_SOURCES core/i18n.cpp core/prelude/i18n.cpp core/natives/i18n.cpp)
list(APPEND AMPIFY_CLI_PORTABLE_SOURCES cli/messages.cpp)
list(APPEND AMPIFY_TESTS_SOURCES tests/i18n_tests.cpp)
