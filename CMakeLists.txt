cmake_minimum_required(VERSION 3.20)
project(AppleMusicModKit VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(AMK_BUILD_TESTS "Build the portable unit tests" ON)

set(AMK_WARNINGS $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic>)

# Portable kernel: settings, journal, safe mode, version gate, extension host.
add_library(amk_core STATIC
    core/settings.cpp
    core/journal.cpp
    core/safe_mode.cpp
    core/version_gate.cpp
    core/extension.cpp)
target_compile_options(amk_core PRIVATE ${AMK_WARNINGS})

# Atmos leveler: BS.1770 loudness meter and gain policy. Pure C++, no Windows API.
add_library(atmos_leveler STATIC
    extensions/atmos-leveler/loudness_meter.cpp
    extensions/atmos-leveler/leveler.cpp)
target_compile_options(atmos_leveler PRIVATE ${AMK_WARNINGS})

if(AMK_BUILD_TESTS)
    enable_testing()
    add_executable(amk_tests
        tests/main.cpp
        tests/core_tests.cpp
        tests/atmos_tests.cpp)
    target_link_libraries(amk_tests PRIVATE amk_core atmos_leveler)
    target_compile_options(amk_tests PRIVATE ${AMK_WARNINGS})
    add_test(NAME amk_tests COMMAND amk_tests)
endif()
