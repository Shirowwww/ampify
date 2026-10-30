cmake_minimum_required(VERSION 3.20)
project(AppleMusicModKit VERSION 0.5.0 LANGUAGES C CXX)

# The kit version is written in three places: here (amk --version), core/kit_layout.hpp (installer, test report) and
# core/script_extension.hpp (AMK.version). A release with two different numbers would confuse `amk update`, so the
# configure step refuses to go on when they disagree.
foreach(amk_version_file core/kit_layout.hpp core/script_extension.hpp)
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/${amk_version_file}" amk_version_text)
    if(NOT amk_version_text MATCHES "(kKitVersion|kKernelVersion) = \"${PROJECT_VERSION}\"")
        message(FATAL_ERROR "${amk_version_file} ne porte pas la version ${PROJECT_VERSION} du projet : mettez-les d'accord.")
    endif()
endforeach()

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

# The sources are UTF-8 without BOM. MSVC reads them in the system code page unless told otherwise, which turns the
# French wide-string literals of the XAML interface (L"Réglages") into mojibake. GCC already assumes UTF-8.
add_compile_options($<$<C_COMPILER_ID:MSVC>:/utf-8> $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
# Visual Studio builds the files of one project one after the other unless told otherwise: /MP compiles them on every
# core (the five XAML files of the kernel took most of the 14 minutes of the MSVC build in CI).
if(MSVC AND CMAKE_GENERATOR MATCHES "Visual Studio")
    add_compile_options(/MP)
endif()

# Source lists. No source file is named in this file: each package lists its files in cmake/sources/<package>.cmake,
# and every *.cmake file of that folder is included here, in alphabetical order, before any target is defined.
# Convention for those files:
#   - only append, with list(APPEND <LIST> file...), never set(): several files may add to the same list;
#   - a new package (an API module, a native extension, a group of tests) adds its own new file rather than editing
#     another package's, so that parallel work does not touch the same lines;
#   - a new target is declared below, and its list gets a name of the form AMK_<TARGET>_SOURCES.
# Lists: AMK_QUICKJS_SOURCES, AMK_CORE_SOURCES, AMK_THEME_SOURCES, AMK_THEME_CHECK_SOURCES, AMK_ATMOS_SOURCES,
# AMK_WIN_SOURCES, AMK_KERNEL_SOURCES, AMK_FAKE_AV_SOURCES, AMK_HOOK_HARNESS_SOURCES, AMK_UI_THREAD_TEST_SOURCES,
# AMK_XAML_SOURCES, AMK_LAUNCHER_SOURCES, AMK_SETUP_SOURCES, AMK_TESTKIT_SOURCES, AMK_CLI_PORTABLE_SOURCES,
# AMK_CLI_SOURCES, AMK_TESTS_SOURCES, AMK_TESTS_WIN_SOURCES.
file(GLOB AMK_SOURCE_LISTS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/cmake/sources/*.cmake")
list(SORT AMK_SOURCE_LISTS)
foreach(source_list IN LISTS AMK_SOURCE_LISTS)
    include("${source_list}")
endforeach()

# quickjs-ng 0.17.0, vendored unchanged (MIT, see vendor/quickjs/LICENSE and vendor/quickjs/UPSTREAM.txt). Compiled as
# C11 with its own warnings silenced: those files are upstream code, and we only change them by replacing the release.
# The sources, defines and MSVC flags mirror the qjs library target of upstream's CMakeLists.txt.
add_library(amk_quickjs STATIC ${AMK_QUICKJS_SOURCES})
set_target_properties(amk_quickjs PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS ON)
target_include_directories(amk_quickjs SYSTEM PUBLIC vendor/quickjs)
if(WIN32)
    # Upstream's Windows defines. quickjs-ng uses Win32 threads, critical sections and condition variables there,
    # so MinGW no longer needs winpthreads.
    target_compile_definitions(amk_quickjs PRIVATE WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0601)
else()
    target_compile_definitions(amk_quickjs PRIVATE _GNU_SOURCE)
    find_package(Threads REQUIRED)
    target_link_libraries(amk_quickjs PUBLIC Threads::Threads m ${CMAKE_DL_LIBS})
endif()
if(MSVC)
    # MSVC accepts <stdatomic.h>, which quickjs.c uses for Atomics, only in C11 mode with this flag.
    target_compile_options(amk_quickjs PRIVATE /W0 /experimental:c11atomics)
else()
    target_compile_options(amk_quickjs PRIVATE -w -funsigned-char)
endif()

# Portable kernel: settings, journal, safe mode, version gate, extension host,
# Apple Music identity, injection policy, DRM guard, package watch, kit file layout, test report, JSON, manifests,
# the JavaScript runtime and the extension loader.
add_library(amk_core STATIC ${AMK_CORE_SOURCES})
target_compile_options(amk_core PRIVATE ${AMK_WARNINGS})
target_link_libraries(amk_core PUBLIC amk_quickjs)

# Theme engine: colour schemes, resources, CSS-like rules, cascade, apply and restore, live reload, inspector.
# Pure C++ over the TreeBackend interface, so it builds and tests on any host. The Windows backend implements that interface.
add_library(amk_theme STATIC ${AMK_THEME_SOURCES})
target_link_libraries(amk_theme PUBLIC amk_core)
target_compile_options(amk_theme PRIVATE ${AMK_WARNINGS})

# Command-line checker for a theme folder: prints the problems and the rules that would apply. Runs on any host.
add_executable(amk_theme_check ${AMK_THEME_CHECK_SOURCES})
target_link_libraries(amk_theme_check PRIVATE amk_theme)
target_compile_options(amk_theme_check PRIVATE ${AMK_WARNINGS})

# Atmos leveler: BS.1770 loudness meter, gain ramp, spatial object pass and gain policy. Pure C++, no Windows API.
add_library(atmos_leveler STATIC ${AMK_ATMOS_SOURCES})
target_compile_options(atmos_leveler PRIVATE ${AMK_WARNINGS})

# XAML theme backend (win/xaml_backend.cpp, win/theme_service.cpp). Off by default: it needs the Windows App SDK C++/WinRT
# headers, which MinGW does not ship, and it has not been compiled or run yet. See docs/tests/M8-themes.md.
option(AMK_XAML "Build the XAML theme backend and native interface in the kernel (needs the Windows App SDK C++/WinRT headers)" OFF)
set(AMK_WINAPPSDK_INCLUDE "" CACHE PATH "Folder that holds winrt/Microsoft.UI.Xaml.h, from the Windows App SDK NuGet package")

# Windows only: the kernel DLL, the launcher, the installer, the test kit, and the helpers they share.
# Built when targeting Windows. Every executable is static, so it runs without any DLL beside it.
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    add_library(amk_win STATIC ${AMK_WIN_SOURCES})
    target_compile_options(amk_win PRIVATE ${AMK_WARNINGS})
    target_compile_definitions(amk_win PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(amk_win PUBLIC ole32 shell32 user32 advapi32 psapi winhttp ws2_32 winmm amk_core)

    # Loaded into AppleMusic.exe and AMPLibraryAgent.exe by the launcher.
    add_library(amk_kernel MODULE ${AMK_KERNEL_SOURCES})
    set_target_properties(amk_kernel PROPERTIES PREFIX "" OUTPUT_NAME amk_kernel)
    target_link_libraries(amk_kernel PRIVATE amk_core amk_win atmos_leveler)
    target_compile_options(amk_kernel PRIVATE ${AMK_WARNINGS})

    # Wine harness (opt-in): hooks on fake COM objects and a fake AVFoundationCF.dll, run under Wine.
    option(AMK_BUILD_HARNESS "Build the Wine hook harness" OFF)
    if(AMK_BUILD_HARNESS)
        # Named like Apple's DLL, so the import table says "AVFoundationCF.dll" as Apple Music's would.
        add_library(amk_fake_av SHARED ${AMK_FAKE_AV_SOURCES})
        set_target_properties(amk_fake_av PROPERTIES PREFIX "" OUTPUT_NAME AVFoundationCF)
        target_compile_options(amk_fake_av PRIVATE ${AMK_WARNINGS})

        add_executable(amk_hook_harness ${AMK_HOOK_HARNESS_SOURCES})
        target_link_libraries(amk_hook_harness PRIVATE amk_core amk_win atmos_leveler amk_fake_av)
        target_compile_options(amk_hook_harness PRIVATE ${AMK_WARNINGS})
    endif()

    # Checks the interface-thread hook with a real window. Run it under Wine with a display, or on the Windows bench.
    add_executable(amk_ui_thread_test ${AMK_UI_THREAD_TEST_SOURCES})
    target_link_libraries(amk_ui_thread_test PRIVATE amk_win)
    target_compile_options(amk_ui_thread_test PRIVATE ${AMK_WARNINGS})

    if(AMK_XAML)
        target_sources(amk_kernel PRIVATE ${AMK_XAML_SOURCES})
        target_compile_definitions(amk_kernel PRIVATE AMK_WITH_XAML=1)
        if(AMK_WINAPPSDK_INCLUDE)
            target_include_directories(amk_kernel PRIVATE "${AMK_WINAPPSDK_INCLUDE}")
        endif()
        # C++/WinRT calls RoGetActivationFactory and RoOriginateLanguageException, exported by runtimeobject.lib.
        target_link_libraries(amk_kernel PRIVATE amk_theme runtimeobject)
        # The WinUI projection gives objects with more sections than the default COFF limit.
        target_compile_options(amk_kernel PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/bigobj>)
        if(MSVC AND NOT CMAKE_CXX_COMPILER_LAUNCHER)
            # The projection headers are parsed once, for the XAML files only; the other kernel files skip the PCH.
            # Not with a compiler cache (CI: ccache), which cannot cache a file built on a precompiled header.
            target_precompile_headers(amk_kernel PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/win/xaml_pch.hpp>")
            set_source_files_properties(${AMK_KERNEL_SOURCES} PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
        endif()
    endif()

    # Starts Apple Music, holds its first process while the kernel loads, then follows the package.
    add_executable(amk_launcher WIN32 ${AMK_LAUNCHER_SOURCES})
    target_link_libraries(amk_launcher PRIVATE amk_core amk_win)
    target_compile_options(amk_launcher PRIVATE ${AMK_WARNINGS})

    # Installs the kit and uninstalls it. The same file is copied into the install folder as the uninstaller.
    add_executable(amk_setup WIN32 ${AMK_SETUP_SOURCES})
    set_target_properties(amk_setup PROPERTIES OUTPUT_NAME amk_setup)
    target_link_libraries(amk_setup PRIVATE amk_core amk_win)
    target_compile_options(amk_setup PRIVATE ${AMK_WARNINGS})

    # The M5 test kit, started by a double-click: installs, launches Apple Music, traces, writes the report.
    add_executable(amk_testkit WIN32 ${AMK_TESTKIT_SOURCES})
    set_target_properties(amk_testkit PROPERTIES OUTPUT_NAME AMK-Test)
    target_link_libraries(amk_testkit PRIVATE amk_core amk_win)
    target_compile_options(amk_testkit PRIVATE ${AMK_WARNINGS})
endif()

# Portable part of the command line `amk`: JSON, SHA-256, manifests, catalog, kit layout, backups.
add_library(amk_cli_portable STATIC ${AMK_CLI_PORTABLE_SOURCES})
target_link_libraries(amk_cli_portable PUBLIC amk_core)
target_compile_options(amk_cli_portable PRIVATE ${AMK_WARNINGS})

# The command line `amk`: commands, platform glue, and the marketplace.
add_executable(amk ${AMK_CLI_SOURCES})
target_link_libraries(amk PRIVATE amk_cli_portable amk_theme amk_core)
target_compile_options(amk PRIVATE ${AMK_WARNINGS})
target_compile_definitions(amk PRIVATE AMK_VERSION="${PROJECT_VERSION}")
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_link_libraries(amk PRIVATE shell32 amk_win)
endif()

if(AMK_BUILD_TESTS)
    enable_testing()
    add_executable(amk_tests ${AMK_TESTS_SOURCES})
    target_link_libraries(amk_tests PRIVATE amk_core atmos_leveler amk_theme amk_cli_portable)
    target_compile_options(amk_tests PRIVATE ${AMK_WARNINGS})
    if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        target_sources(amk_tests PRIVATE ${AMK_TESTS_WIN_SOURCES})
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
