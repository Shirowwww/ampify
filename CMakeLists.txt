cmake_minimum_required(VERSION 3.20)
project(AppleMusicModKit VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(AMK_BUILD_TESTS "Build the portable unit tests" ON)

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
# QuickJS uses pthreads for Atomics. On Windows, MinGW provides them through winpthreads, linked statically.
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_link_libraries(amk_quickjs PUBLIC pthread)
endif()

# Portable kernel: settings, journal, safe mode, version gate, extension host,
# Apple Music identity, injection policy, DRM guard, JSON, manifests, the JavaScript runtime
# and the extension loader.
add_library(amk_core STATIC
    core/settings.cpp
    core/journal.cpp
    core/safe_mode.cpp
    core/version_gate.cpp
    core/extension.cpp
    core/apple_music.cpp
    core/injection_policy.cpp
    core/drm_guard.cpp
    core/json.cpp
    core/manifest.cpp
    core/js_runtime.cpp
    core/amk_prelude.cpp
    core/script_extension.cpp
    core/extension_loader.cpp)
target_compile_options(amk_core PRIVATE ${AMK_WARNINGS})
target_link_libraries(amk_core PUBLIC amk_quickjs)

# Atmos leveler: BS.1770 loudness meter and gain policy. Pure C++, no Windows API.
add_library(atmos_leveler STATIC
    extensions/atmos-leveler/loudness_meter.cpp
    extensions/atmos-leveler/leveler.cpp)
target_compile_options(atmos_leveler PRIVATE ${AMK_WARNINGS})

# Windows only: the kernel DLL, the launcher, and the helpers they share. Built when targeting Windows.
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    add_library(amk_win STATIC win/win_common.cpp win/win_host.cpp)
    target_compile_options(amk_win PRIVATE ${AMK_WARNINGS})
    target_compile_definitions(amk_win PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(amk_win PUBLIC ole32 shell32 user32)

    # Loaded into AppleMusic.exe and AMPLibraryAgent.exe by the launcher.
    add_library(amk_kernel MODULE kernel/kernel.cpp)
    set_target_properties(amk_kernel PROPERTIES PREFIX "" OUTPUT_NAME amk_kernel)
    target_link_libraries(amk_kernel PRIVATE amk_core amk_win)
    target_compile_options(amk_kernel PRIVATE ${AMK_WARNINGS})

    # Starts Apple Music suspended, injects the kernel, resumes it, then watches for AMPLibraryAgent.exe.
    add_executable(amk_launcher WIN32
        launcher/launcher.cpp
        launcher/package.cpp
        launcher/injection.cpp)
    target_link_libraries(amk_launcher PRIVATE amk_core amk_win)
    target_compile_options(amk_launcher PRIVATE ${AMK_WARNINGS})
endif()

if(AMK_BUILD_TESTS)
    enable_testing()
    add_executable(amk_tests
        tests/main.cpp
        tests/core_tests.cpp
        tests/atmos_tests.cpp
        tests/apple_music_tests.cpp
        tests/policy_tests.cpp
        tests/json_tests.cpp
        tests/manifest_tests.cpp
        tests/js_runtime_tests.cpp
        tests/script_extension_tests.cpp
        tests/loader_tests.cpp)
    target_link_libraries(amk_tests PRIVATE amk_core atmos_leveler)
    target_compile_options(amk_tests PRIVATE ${AMK_WARNINGS})
    if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        target_sources(amk_tests PRIVATE tests/win_host_tests.cpp)
        target_link_libraries(amk_tests PRIVATE amk_win)
    endif()
    target_compile_definitions(amk_tests PRIVATE AMK_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    add_test(NAME amk_tests COMMAND amk_tests)
endif()
