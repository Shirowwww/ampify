# Module tray: AMK.tray, items in the kit's notification area icon (win/tray.*). The Windows services that offer the
# kit capabilities (win/win_services.*) and their Wine tests are listed here too.
list(APPEND AMK_CORE_SOURCES core/prelude/tray.cpp core/natives/tray.cpp)
list(APPEND AMK_WIN_SOURCES win/tray.cpp win/win_services.cpp)
list(APPEND AMK_TESTS_SOURCES tests/tray_tests.cpp)
list(APPEND AMK_TESTS_WIN_SOURCES tests/win_services_tests.cpp)
