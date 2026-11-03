cmake_minimum_required(VERSION 3.20)
project(Ampify VERSION 1.3.1 LANGUAGES C CXX)

# The Ampify version is written in three places: here (ampify --version), src/core/kit_layout.hpp (installer, uninstall entry) and
# src/core/script_extension.hpp (Ampify.version). A release with two different numbers would confuse `ampify update`, so the
# configure step refuses to go on when they disagree.
foreach(ampify_version_file src/core/kit_layout.hpp src/core/script_extension.hpp)
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/${ampify_version_file}" ampify_version_text)
    if(NOT ampify_version_text MATCHES "(kKitVersion|kKernelVersion) = \"${PROJECT_VERSION}\"")
        message(FATAL_ERROR "${ampify_version_file} does not carry the project version ${PROJECT_VERSION}: make them agree.")
    endif()
endforeach()

# Properties > Details of the files people download: product, publisher, description and version. MSVC builds only,
# the ones the release is made from.
if(MSVC)
    enable_language(RC)
endif()
function(ampify_version_resource target description)
    if(NOT MSVC)
        return()
    endif()
    get_target_property(ampify_rc_name ${target} OUTPUT_NAME)
    if(NOT ampify_rc_name)
        set(ampify_rc_name ${target})
    endif()
    get_target_property(ampify_rc_type ${target} TYPE)
    if(ampify_rc_type STREQUAL "EXECUTABLE")
        set(AMPIFY_RC_FILETYPE VFT_APP)
        set(AMPIFY_RC_FILE "${ampify_rc_name}.exe")
    else()
        set(AMPIFY_RC_FILETYPE VFT_DLL)
        set(AMPIFY_RC_FILE "${ampify_rc_name}.dll")
    endif()
    set(AMPIFY_RC_ICON "${CMAKE_CURRENT_SOURCE_DIR}/src/resources/ampify.ico")
    set(AMPIFY_RC_NAME "${ampify_rc_name}")
    set(AMPIFY_RC_DESCRIPTION "${description}")
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.rc.in" "${CMAKE_CURRENT_BINARY_DIR}/${target}.rc" @ONLY)
    target_sources(${target} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/${target}.rc")
endfunction()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(AMPIFY_BUILD_TESTS "Build the portable unit tests" ON)

# Bench build, used only by the Windows test bench in CI (.github/workflows/ci.yml). It accepts any
# installed Apple Music build, so the bench can check the injection on the version the runner has. Never ship it.
option(AMPIFY_BENCH "Bench build: accepts any Apple Music build. Never distribute." OFF)
if(AMPIFY_BENCH)
    message(WARNING "AMPIFY_BENCH=ON: bench build, any Apple Music version is accepted. Do not distribute.")
    add_compile_definitions(AMPIFY_BENCH_ANY_VERSION)
    set(AMPIFY_BUILD_TESTS OFF)  # the unit tests check the shipping version policy, which the bench build relaxes
endif()

set(AMPIFY_WARNINGS $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic>)

# The sources are UTF-8 without BOM. MSVC reads them in the system code page unless told otherwise, which turns the
# non-ASCII wide-string literals of the XAML interface (L"…") into mojibake. GCC already assumes UTF-8.
add_compile_options($<$<C_COMPILER_ID:MSVC>:/utf-8> $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
# A .pdb beside each binary, so the offset of a crash in the Windows event log leads to a function. The package takes
# the binaries by name and never ships it; only its file name is written into the binary. /Z7 keeps ccache working.
if(MSVC)
    add_compile_options(/Z7)
    add_link_options(/DEBUG /OPT:REF /OPT:ICF /PDBALTPATH:%_PDB%)
endif()
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
#   - a new target is declared below, and its list gets a name of the form AMPIFY_<TARGET>_SOURCES.
# Lists: AMPIFY_QUICKJS_SOURCES, AMPIFY_CORE_SOURCES, AMPIFY_THEME_SOURCES,AMPIFY_ATMOS_SOURCES,
# AMPIFY_WIN_SOURCES, AMPIFY_KERNEL_SOURCES, AMPIFY_UI_THREAD_TEST_SOURCES, AMPIFY_XAML_SOURCES, AMPIFY_LAUNCHER_SOURCES,
# AMPIFY_SETUP_SOURCES, AMPIFY_CLI_PORTABLE_SOURCES, AMPIFY_CLI_SOURCES, AMPIFY_TESTS_SOURCES, AMPIFY_TESTS_WIN_SOURCES.
file(GLOB AMPIFY_SOURCE_LISTS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/cmake/sources/*.cmake")
list(SORT AMPIFY_SOURCE_LISTS)
foreach(source_list IN LISTS AMPIFY_SOURCE_LISTS)
    include("${source_list}")
endforeach()

# quickjs-ng 0.18.0, vendored unchanged (MIT, see vendor/quickjs/LICENSE and vendor/quickjs/UPSTREAM.txt). Compiled as
# C11 with its own warnings silenced: those files are upstream code, and we only change them by replacing the release.
# The sources, defines and MSVC flags mirror the qjs library target of upstream's CMakeLists.txt.
add_library(ampify_quickjs STATIC ${AMPIFY_QUICKJS_SOURCES})
set_target_properties(ampify_quickjs PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS ON)
target_include_directories(ampify_quickjs SYSTEM PUBLIC vendor/quickjs)
if(WIN32)
    # Upstream's Windows defines: quickjs-ng uses Win32 threads, critical sections and condition variables there.
    target_compile_definitions(ampify_quickjs PRIVATE WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0601)
else()
    target_compile_definitions(ampify_quickjs PRIVATE _GNU_SOURCE)
    find_package(Threads REQUIRED)
    target_link_libraries(ampify_quickjs PUBLIC Threads::Threads m ${CMAKE_DL_LIBS})
endif()
if(MSVC)
    # MSVC accepts <stdatomic.h>, which quickjs.c uses for Atomics, only in C11 mode with this flag.
    target_compile_options(ampify_quickjs PRIVATE /W0 /experimental:c11atomics)
else()
    target_compile_options(ampify_quickjs PRIVATE -w -funsigned-char)
endif()

# Portable kernel: settings, journal, safe mode, version gate, extension host,
# Apple Music identity, injection policy, DRM guard, package watch, kit file layout, JSON, manifests,
# the JavaScript runtime and the extension loader.
add_library(ampify_core STATIC ${AMPIFY_CORE_SOURCES})
target_compile_options(ampify_core PRIVATE ${AMPIFY_WARNINGS})
target_link_libraries(ampify_core PUBLIC ampify_quickjs)

# Theme engine: colour schemes, resources, CSS-like rules, cascade, apply and restore, live reload, inspector.
# Pure C++ over the TreeBackend interface, so it builds and tests on any host. The Windows backend implements that interface.
add_library(ampify_theme STATIC ${AMPIFY_THEME_SOURCES})
target_link_libraries(ampify_theme PUBLIC ampify_core)
target_compile_options(ampify_theme PRIVATE ${AMPIFY_WARNINGS})

# Atmos leveler: BS.1770 loudness meter, gain ramp, spatial object pass and gain policy. Pure C++, no Windows API.
add_library(atmos_leveler STATIC ${AMPIFY_ATMOS_SOURCES})
target_compile_options(atmos_leveler PRIVATE ${AMPIFY_WARNINGS})

# XAML theme backend (src/win/xaml_backend.cpp, src/win/theme_service.cpp). Off by default: it needs the C++/WinRT projection
# of WinUI, which scripts/build-msvc.ps1 and CI (ci.yml) generate. See docs/development/xaml-lessons.md.
option(AMPIFY_XAML "Build the XAML theme backend and native interface in the kernel (needs the Windows App SDK C++/WinRT headers)" OFF)
set(AMPIFY_WINAPPSDK_INCLUDE "" CACHE PATH "Folder that holds winrt/Microsoft.UI.Xaml.h, from the Windows App SDK NuGet package")

# Windows only: the kernel DLL, the launcher, the installer, and the helpers they share.
# Built when targeting Windows. Every executable is static, so it runs without any DLL beside it.
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    add_library(ampify_win STATIC ${AMPIFY_WIN_SOURCES})
    target_compile_options(ampify_win PRIVATE ${AMPIFY_WARNINGS})
    target_compile_definitions(ampify_win PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(ampify_win PUBLIC ole32 shell32 user32 comctl32 advapi32 psapi winhttp ws2_32 winmm windowscodecs
                          ampify_core)

    # Loaded into AppleMusic.exe and AMPLibraryAgent.exe by the launcher.
    add_library(ampify_kernel MODULE ${AMPIFY_KERNEL_SOURCES})
    set_target_properties(ampify_kernel PROPERTIES PREFIX "" OUTPUT_NAME ampify_kernel)
    target_link_libraries(ampify_kernel PRIVATE ampify_core ampify_win atmos_leveler)
    target_compile_options(ampify_kernel PRIVATE ${AMPIFY_WARNINGS})
    ampify_version_resource(ampify_kernel "Ampify kernel, loaded into Apple Music")

    # Checks the interface-thread hook with a real window. CI runs it beside ampify_tests.
    add_executable(ampify_ui_thread_test ${AMPIFY_UI_THREAD_TEST_SOURCES})
    target_link_libraries(ampify_ui_thread_test PRIVATE ampify_win)
    target_compile_options(ampify_ui_thread_test PRIVATE ${AMPIFY_WARNINGS})

    if(AMPIFY_XAML)
        target_sources(ampify_kernel PRIVATE ${AMPIFY_XAML_SOURCES})
        target_compile_definitions(ampify_kernel PRIVATE AMPIFY_WITH_XAML=1)
        if(AMPIFY_WINAPPSDK_INCLUDE)
            target_include_directories(ampify_kernel PRIVATE "${AMPIFY_WINAPPSDK_INCLUDE}")
        endif()
        # C++/WinRT calls RoGetActivationFactory and RoOriginateLanguageException, exported by runtimeobject.lib.
        # dwmapi: the visible frame of a window, to lay Apple's player over the main window (src/win/apple_player.cpp).
        target_link_libraries(ampify_kernel PRIVATE ampify_theme runtimeobject dwmapi)
        # The WinUI projection gives objects with more sections than the default COFF limit.
        target_compile_options(ampify_kernel PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/bigobj>)
        if(MSVC AND NOT CMAKE_CXX_COMPILER_LAUNCHER)
            # The projection headers are parsed once, for the XAML files only; the other kernel files skip the PCH.
            # Not with a compiler cache (CI: ccache), which cannot cache a file built on a precompiled header.
            target_precompile_headers(ampify_kernel PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/src/win/xaml_pch.hpp>")
            set_source_files_properties(${AMPIFY_KERNEL_SOURCES} PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
        endif()
    endif()

    # Starts Apple Music, holds its first process while the kernel loads, then follows the package.
    add_executable(ampify_launcher WIN32 ${AMPIFY_LAUNCHER_SOURCES})
    target_link_libraries(ampify_launcher PRIVATE ampify_core ampify_win)
    target_compile_options(ampify_launcher PRIVATE ${AMPIFY_WARNINGS})
    ampify_version_resource(ampify_launcher "Ampify launcher")

    # Installs the kit and uninstalls it. The same file is copied into the install folder as the uninstaller.
    add_executable(ampify_setup WIN32 ${AMPIFY_SETUP_SOURCES})
    set_target_properties(ampify_setup PROPERTIES OUTPUT_NAME ampify_setup)
    target_link_libraries(ampify_setup PRIVATE ampify_core ampify_win)
    target_compile_options(ampify_setup PRIVATE ${AMPIFY_WARNINGS})
    ampify_version_resource(ampify_setup "Ampify setup and uninstaller")
endif()

# Portable part of the command line `ampify`: SHA-256, manifests, catalog, kit layout, backups, theme commands.
add_library(ampify_cli_portable STATIC ${AMPIFY_CLI_PORTABLE_SOURCES})
target_link_libraries(ampify_cli_portable PUBLIC ampify_theme ampify_core)
target_compile_options(ampify_cli_portable PRIVATE ${AMPIFY_WARNINGS})

# The command line `ampify`: commands, platform glue, and the marketplace.
add_executable(ampify ${AMPIFY_CLI_SOURCES})
target_link_libraries(ampify PRIVATE ampify_cli_portable ampify_theme ampify_core)
target_compile_options(ampify PRIVATE ${AMPIFY_WARNINGS})
target_compile_definitions(ampify PRIVATE AMPIFY_VERSION="${PROJECT_VERSION}")
ampify_version_resource(ampify "Ampify command line")
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_link_libraries(ampify PRIVATE shell32 ampify_win)
endif()

if(AMPIFY_BUILD_TESTS)
    enable_testing()
    add_executable(ampify_tests ${AMPIFY_TESTS_SOURCES})
    target_link_libraries(ampify_tests PRIVATE ampify_core atmos_leveler ampify_theme ampify_cli_portable)
    target_compile_options(ampify_tests PRIVATE ${AMPIFY_WARNINGS})
    if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        target_sources(ampify_tests PRIVATE ${AMPIFY_TESTS_WIN_SOURCES})
        target_link_libraries(ampify_tests PRIVATE ampify_win)
    endif()
    target_compile_definitions(ampify_tests PRIVATE AMPIFY_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    add_test(NAME ampify_tests COMMAND ampify_tests)

    # Python tests of the validator, the packer and the command line (tools/test_cli.py runs the ampify binary).
    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        add_test(NAME ampify_tools COMMAND ${Python3_EXECUTABLE} -m unittest discover -s ${CMAKE_SOURCE_DIR}/tools -v)
        set_tests_properties(ampify_tools PROPERTIES ENVIRONMENT "AMPIFY_BIN=$<TARGET_FILE:ampify>")
    endif()
endif()
