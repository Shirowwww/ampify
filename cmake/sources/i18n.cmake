# Module i18n: AMK.i18n, and the French and English texts of the kit (core/i18n.*) and of the command line
# (cli/messages.cpp).
list(APPEND AMK_CORE_SOURCES core/i18n.cpp core/prelude/i18n.cpp core/natives/i18n.cpp)
list(APPEND AMK_CLI_PORTABLE_SOURCES cli/messages.cpp)
list(APPEND AMK_TESTS_SOURCES tests/i18n_tests.cpp)
