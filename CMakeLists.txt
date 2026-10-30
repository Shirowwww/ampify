cmake_minimum_required(VERSION 3.20)
project(AppleMusicModKit VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(AMK_BUILD_TESTS "Build the portable unit tests" ON)

# Bench build, used only by the Windows test bench in CI (.github/workflows/windows-bench.yml). It accepts any
# installed Apple Music build, so the bench can check the injection on the version the runner has. Never ship it.
option(AMK_BENCH "Bench build: accepts any Apple Music build. Never distribute." OFF)
if(AMK_BENCH)
    message(WARNING "AMK_BENCH=ON : build de banc, toute version d'Apple Music est acceptée. Ne pas diffuser.")
    add_compile_definitions(AMK_BENCH_ANY_VERSION)
    set(AMK_BUILD_TESTS OFF)  # the unit tests check the shipping version policy, which the bench build relaxes
endif()

set(AMK_WARNINGS $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic>)

# QuickJS 2021-03-27, vendored unchanged (MIT, see vendor/quickjs/LICENSE). Compiled as C with its own
# warnings silenced: those files are upstream code, and we only change them by replacing the release.
add_library(amk_quickjs STATIC
    vendor/quickjs/quickjs.c
    vendor/quickjs/cutils.c
    vendor/quickjs/libregexp.c
    vendor/quickjs/libunicode.c)
target_compile_options(amk_quickjs PRIVATE $<$<NOT:$<C_COMPILER_ID:MSVC>>:-w>)
target_compile_definitions(amk_quickjs PRIVATE "CONFIG_VERSION=\"2021-03-27\"")
target_include_directories(amk_quickjs SYSTEM PUBLIC vendor/quickjs)
# QuickJS uses pthreads for Atomics. With MinGW, winpthreads provides them, linked statically.
if(MINGW)
    target_link_libraries(amk_quickjs PUBLIC pthread)
endif()
# MSVC has neither the GCC extensions nor the POSIX headers that QuickJS includes. cmake/msvc maps them: a header
# that is force-included in every QuickJS file, and small pthread.h, sys/time.h and stdatomic.h over SRWLOCK,
# CONDITION_VARIABLE, GetSystemTimeAsFileTime and the Interlocked intrinsics. The vendored files are not changed.
if(MSVC)
    target_compile_options(amk_quickjs PRIVATE /w "/FI${CMAKE_CURRENT_SOURCE_DIR}/cmake/msvc/quickjs_msvc.h")
    target_compile_definitions(amk_quickjs PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(amk_quickjs SYSTEM PRIVATE cmake/msvc/include)
endif()

# Portable kernel: settings, journal, safe mode, version gate, extension host,
# Apple Music identity, injection policy, DRM guard, package watch, kit file layout, test report, JSON, manifests,
# the JavaScript runtime and the extension loader.
add_library(amk_core STATIC
    core/settings.cpp
    core/journal.cpp
    core/safe_mode.cpp
    core/version_gate.cpp
    core/extension.cpp
    core/apple_music.cpp
    core/build_signature.cpp
    core/injection_policy.cpp
    core/drm_guard.cpp
    core/json.cpp
    core/manifest.cpp
    core/js_runtime.cpp
    core/amk_prelude.cpp
    core/script_extension.cpp
    core/extension_loader.cpp
    core/gain_board.cpp
    core/player_watch.cpp
    core/ui_model.cpp
    core/reload.cpp
    core/package_watch.cpp
    core/kit_layout.cpp
    core/run_report.cpp)
target_compile_options(amk_core PRIVATE ${AMK_WARNINGS})
target_link_libraries(amk_core PUBLIC amk_quickjs)

# Theme engine: colour schemes, resources, CSS-like rules, cascade, apply and restore, live reload, inspector.
# Pure C++ over the TreeBackend interface, so it builds and tests on any host. The Windows backend implements that interface.
add_library(amk_theme STATIC
    core/theme/color.cpp
    core/theme/css.cpp
    core/theme/engine.cpp
    core/theme/files.cpp
    core/theme/ini.cpp
    core/theme/inspector.cpp
    core/theme/library.cpp
    core/theme/properties.cpp
    core/theme/selector.cpp
    core/theme/session.cpp
    core/theme/theme.cpp
    core/theme/theme_settings.cpp
    core/theme/commands.cpp)
target_link_libraries(amk_theme PUBLIC amk_core)
target_compile_options(amk_theme PRIVATE ${AMK_WARNINGS})

# Command-line checker for a theme folder: prints the problems and the rules that would apply. Runs on any host.
add_executable(amk_theme_check tools/theme_check.cpp)
target_link_libraries(amk_theme_check PRIVATE amk_theme)
target_compile_options(amk_theme_check PRIVATE ${AMK_WARNINGS})

# The amk command: theme list, theme set (--scheme), watch. Edits settings.txt; the kernel applies the change.
add_executable(amk tools/amk.cpp)
target_link_libraries(amk PRIVATE amk_theme)
target_compile_options(amk PRIVATE ${AMK_WARNINGS})

# Atmos leveler: BS.1770 loudness meter, gain ramp, spatial object pass and gain policy. Pure C++, no Windows API.
add_library(atmos_leveler STATIC
    extensions/atmos-leveler/loudness_meter.cpp
    extensions/atmos-leveler/gain_ramp.cpp
    extensions/atmos-leveler/object_pass.cpp
    extensions/atmos-leveler/format_detection.cpp
    extensions/atmos-leveler/leveler.cpp)
target_compile_options(atmos_leveler PRIVATE ${AMK_WARNINGS})

# XAML theme backend (win/xaml_backend.cpp, win/theme_service.cpp). Off by default: it needs the Windows App SDK C++/WinRT
# headers, which MinGW does not ship, and it has not been compiled or run yet. See docs/tests/M8-themes.md.
option(AMK_XAML "Build the XAML theme backend in the kernel (needs the Windows App SDK C++/WinRT headers)" OFF)
set(AMK_WINAPPSDK_INCLUDE "" CACHE PATH "Folder that holds winrt/Microsoft.UI.Xaml.h, from the Windows App SDK NuGet package")

# Windows only: the kernel DLL, the launcher, the installer, the test kit, and the helpers they share.
# Built when targeting Windows. Every executable is static, so it runs without any DLL beside it.
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    add_library(amk_win STATIC win/win_common.cpp win/package.cpp win/win_host.cpp win/hook.cpp win/ui_thread.cpp)
    target_compile_options(amk_win PRIVATE ${AMK_WARNINGS})
    target_compile_definitions(amk_win PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(amk_win PUBLIC ole32 shell32 user32 advapi32 psapi winhttp amk_core)

    # Loaded into AppleMusic.exe and AMPLibraryAgent.exe by the launcher.
    add_library(amk_kernel MODULE kernel/kernel.cpp extensions/atmos-leveler/atmos_windows.cpp)
    set_target_properties(amk_kernel PROPERTIES PREFIX "" OUTPUT_NAME amk_kernel)
    target_link_libraries(amk_kernel PRIVATE amk_core amk_win atmos_leveler)
    target_compile_options(amk_kernel PRIVATE ${AMK_WARNINGS})

    # Wine harness (opt-in): hooks on fake COM objects and a fake AVFoundationCF.dll, run under Wine.
    option(AMK_BUILD_HARNESS "Build the Wine hook harness" OFF)
    if(AMK_BUILD_HARNESS)
        # Named like Apple's DLL, so the import table says "AVFoundationCF.dll" as Apple Music's would.
        add_library(amk_fake_av SHARED tests/win/fake_avfoundation.cpp)
        set_target_properties(amk_fake_av PROPERTIES PREFIX "" OUTPUT_NAME AVFoundationCF)
        target_compile_options(amk_fake_av PRIVATE ${AMK_WARNINGS})

        add_executable(amk_hook_harness tests/win/hook_harness.cpp
            extensions/atmos-leveler/atmos_windows.cpp)
        target_link_libraries(amk_hook_harness PRIVATE amk_core amk_win atmos_leveler amk_fake_av)
        target_compile_options(amk_hook_harness PRIVATE ${AMK_WARNINGS})
    endif()

    # Checks the interface-thread hook with a real window. Run it under Wine with a display, or on the Windows bench.
    add_executable(amk_ui_thread_test tests/ui_thread_test.cpp)
    target_link_libraries(amk_ui_thread_test PRIVATE amk_win)
    target_compile_options(amk_ui_thread_test PRIVATE ${AMK_WARNINGS})

    if(AMK_XAML)
        target_sources(amk_kernel PRIVATE win/xaml_backend.cpp win/theme_service.cpp)
        target_compile_definitions(amk_kernel PRIVATE AMK_WITH_XAML=1)
        if(AMK_WINAPPSDK_INCLUDE)
            target_include_directories(amk_kernel PRIVATE "${AMK_WINAPPSDK_INCLUDE}")
        endif()
        target_link_libraries(amk_kernel PRIVATE amk_theme)
    endif()

    # Starts Apple Music, holds its first process while the kernel loads, then follows the package.
    add_executable(amk_launcher WIN32
        launcher/launcher.cpp
        launcher/injection.cpp)
    target_link_libraries(amk_launcher PRIVATE amk_core amk_win)
    target_compile_options(amk_launcher PRIVATE ${AMK_WARNINGS})

    # Installs the kit and uninstalls it. The same file is copied into the install folder as the uninstaller.
    add_executable(amk_setup WIN32 setup/setup.cpp)
    set_target_properties(amk_setup PROPERTIES OUTPUT_NAME amk_setup)
    target_link_libraries(amk_setup PRIVATE amk_core amk_win)
    target_compile_options(amk_setup PRIVATE ${AMK_WARNINGS})

    # The M5 test kit, started by a double-click: installs, launches Apple Music, traces, writes the report.
    add_executable(amk_testkit WIN32 testkit/testkit.cpp testkit/etl.cpp)
    set_target_properties(amk_testkit PROPERTIES OUTPUT_NAME AMK-Test)
    target_link_libraries(amk_testkit PRIVATE amk_core amk_win)
    target_compile_options(amk_testkit PRIVATE ${AMK_WARNINGS})
endif()

# Portable part of the command line `amk`: JSON, SHA-256, manifests, catalog, kit layout, backups.
add_library(amk_cli_portable STATIC
    cli/json.cpp
    cli/sha256.cpp
    cli/element.cpp
    cli/catalog.cpp
    cli/kit.cpp
    cli/backup.cpp
    cli/archive.cpp
    cli/watch.cpp
    cli/release.cpp
    cli/zip_writer.cpp)
target_link_libraries(amk_cli_portable PUBLIC amk_core)
target_compile_options(amk_cli_portable PRIVATE ${AMK_WARNINGS})

# The command line `amk`: commands, platform glue, and the marketplace.
set(AMK_CLI_SOURCES
    cli/amk.cpp
    cli/commands.cpp
    cli/marketplace.cpp
    cli/activation.cpp
    cli/author.cpp
    cli/sdk_types.cpp
    cli/develop.cpp
    cli/diagnose.cpp
    cli/transfer.cpp
    cli/test_runner.cpp)
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    list(APPEND AMK_CLI_SOURCES cli/platform.cpp cli/platform_win.cpp)
else()
    list(APPEND AMK_CLI_SOURCES cli/platform.cpp cli/platform_posix.cpp)
endif()
add_executable(amk ${AMK_CLI_SOURCES})
target_link_libraries(amk PRIVATE amk_cli_portable amk_core)
target_compile_options(amk PRIVATE ${AMK_WARNINGS})
target_compile_definitions(amk PRIVATE AMK_VERSION="${PROJECT_VERSION}")
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_link_libraries(amk PRIVATE shell32)
endif()

if(AMK_BUILD_TESTS)
    enable_testing()
    add_executable(amk_tests
        tests/main.cpp
        tests/core_tests.cpp
        tests/atmos_tests.cpp
        tests/apple_music_tests.cpp
        tests/policy_tests.cpp
        tests/cli_tests.cpp
        tests/json_tests.cpp
        tests/manifest_tests.cpp
        tests/js_runtime_tests.cpp
        tests/script_extension_tests.cpp
        tests/loader_tests.cpp
        tests/pipeline_tests.cpp
        tests/theme_tests.cpp
        tests/gain_board_tests.cpp
        tests/player_watch_tests.cpp
        tests/ui_model_tests.cpp
        tests/ui_element_tests.cpp
        tests/theme_script_tests.cpp
        tests/reload_tests.cpp
        tests/signature_tests.cpp
        tests/watch_tests.cpp
        tests/kit_tests.cpp)
    target_link_libraries(amk_tests PRIVATE amk_core atmos_leveler amk_theme amk_cli_portable)
    target_compile_options(amk_tests PRIVATE ${AMK_WARNINGS})
    if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        target_sources(amk_tests PRIVATE tests/win_host_tests.cpp)
        target_link_libraries(amk_tests PRIVATE amk_win)
    endif()
    target_compile_definitions(amk_tests PRIVATE AMK_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    add_test(NAME amk_tests COMMAND amk_tests)

    # Python tests of the validator, the packer and the command line (tools/test_cli.py runs the amk binary).
    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        add_test(NAME amk_tools COMMAND ${Python3_EXECUTABLE} -m unittest discover -s ${CMAKE_SOURCE_DIR}/tools -v)
        set_tests_properties(amk_tools PROPERTIES ENVIRONMENT "AMK_BIN=$<TARGET_FILE:amk>")
    endif()
endif()
