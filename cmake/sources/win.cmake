# Windows only: the shared helpers (ampify_win), the kernel DLL, the XAML backend, the launcher and the installer. The
# lists are always defined; CMakeLists.txt uses them only when targeting Windows.
# See the convention at the top of CMakeLists.txt.
list(APPEND AMPIFY_WIN_SOURCES src/win/win_common.cpp src/win/package.cpp src/win/win_host.cpp src/win/hook.cpp
    src/win/ui_thread.cpp src/win/kit_command.cpp src/win/wic_image.cpp src/win/background.cpp)

list(APPEND AMPIFY_KERNEL_SOURCES src/kernel/kernel.cpp extensions/atmos-leveler/atmos_windows.cpp)

list(APPEND AMPIFY_UI_THREAD_TEST_SOURCES tests/ui_thread_test.cpp)

list(APPEND AMPIFY_XAML_SOURCES src/win/xaml_backend.cpp src/win/xaml_properties.cpp src/win/xaml_states.cpp
    src/win/xaml_values.cpp src/win/theme_animations.cpp src/win/theme_service.cpp src/win/xaml_host.cpp src/win/ui_renderer.cpp src/win/ui_service.cpp
    src/win/apple_player.cpp)

list(APPEND AMPIFY_LAUNCHER_SOURCES
    src/launcher/launcher.cpp
    src/launcher/injection.cpp)

list(APPEND AMPIFY_SETUP_SOURCES src/setup/setup.cpp)
