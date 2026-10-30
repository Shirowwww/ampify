# Windows only: the shared helpers (amk_win), the kernel DLL, the Wine harness, the XAML backend, the launcher, the
# installer and the test kit. The lists are always defined; CMakeLists.txt uses them only when targeting Windows.
# See the convention at the top of CMakeLists.txt.
list(APPEND AMK_WIN_SOURCES win/win_common.cpp win/package.cpp win/win_host.cpp win/hook.cpp win/ui_thread.cpp
    win/kit_command.cpp)

list(APPEND AMK_KERNEL_SOURCES kernel/kernel.cpp extensions/atmos-leveler/atmos_windows.cpp)

list(APPEND AMK_FAKE_AV_SOURCES tests/win/fake_avfoundation.cpp)

list(APPEND AMK_HOOK_HARNESS_SOURCES tests/win/hook_harness.cpp
    extensions/atmos-leveler/atmos_windows.cpp)

list(APPEND AMK_UI_THREAD_TEST_SOURCES tests/ui_thread_test.cpp)

list(APPEND AMK_XAML_SOURCES win/xaml_backend.cpp win/theme_service.cpp win/xaml_host.cpp win/ui_renderer.cpp win/ui_service.cpp)

list(APPEND AMK_LAUNCHER_SOURCES
    launcher/launcher.cpp
    launcher/injection.cpp)

list(APPEND AMK_SETUP_SOURCES setup/setup.cpp)

list(APPEND AMK_TESTKIT_SOURCES testkit/testkit.cpp testkit/etl.cpp)
