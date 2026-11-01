# Windows only: the shared helpers (ampify_win), the kernel DLL, the Wine harness, the XAML backend, the launcher, the
# installer and the test kit. The lists are always defined; CMakeLists.txt uses them only when targeting Windows.
# See the convention at the top of CMakeLists.txt.
list(APPEND AMPIFY_WIN_SOURCES win/win_common.cpp win/package.cpp win/win_host.cpp win/hook.cpp win/ui_thread.cpp
    win/kit_command.cpp win/wic_image.cpp)

list(APPEND AMPIFY_KERNEL_SOURCES kernel/kernel.cpp extensions/atmos-leveler/atmos_windows.cpp)

list(APPEND AMPIFY_FAKE_AV_SOURCES tests/win/fake_avfoundation.cpp)

list(APPEND AMPIFY_HOOK_HARNESS_SOURCES tests/win/hook_harness.cpp
    extensions/atmos-leveler/atmos_windows.cpp)

list(APPEND AMPIFY_UI_THREAD_TEST_SOURCES tests/ui_thread_test.cpp)

list(APPEND AMPIFY_XAML_SOURCES win/xaml_backend.cpp win/theme_animations.cpp win/theme_service.cpp win/xaml_host.cpp win/ui_renderer.cpp win/ui_service.cpp
    win/apple_player.cpp)

list(APPEND AMPIFY_LAUNCHER_SOURCES
    launcher/launcher.cpp
    launcher/injection.cpp)

list(APPEND AMPIFY_SETUP_SOURCES setup/setup.cpp)

list(APPEND AMPIFY_TESTKIT_SOURCES testkit/testkit.cpp testkit/etl.cpp)
