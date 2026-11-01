# Module tray: Ampify.tray, items in Ampify's notification area icon (src/win/tray.*). The Windows services that offer the
# Ampify capabilities (src/win/win_services.*) and their Windows tests are listed here too.
list(APPEND AMPIFY_CORE_SOURCES src/core/prelude/tray.cpp src/core/natives/tray.cpp)
list(APPEND AMPIFY_WIN_SOURCES src/win/tray.cpp src/win/win_services.cpp)
list(APPEND AMPIFY_TESTS_SOURCES tests/tray_tests.cpp)
list(APPEND AMPIFY_TESTS_WIN_SOURCES tests/win_services_tests.cpp)
