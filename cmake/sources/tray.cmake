# Module tray: Ampify.tray, items in Ampify's notification area icon (win/tray.*). The Windows services that offer the
# Ampify capabilities (win/win_services.*) and their Wine tests are listed here too.
list(APPEND AMPIFY_CORE_SOURCES core/prelude/tray.cpp core/natives/tray.cpp)
list(APPEND AMPIFY_WIN_SOURCES win/tray.cpp win/win_services.cpp)
list(APPEND AMPIFY_TESTS_SOURCES tests/tray_tests.cpp)
list(APPEND AMPIFY_TESTS_WIN_SOURCES tests/win_services_tests.cpp)
