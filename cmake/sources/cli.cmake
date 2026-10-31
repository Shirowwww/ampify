# The command line `ampify`: its portable library (ampify_cli_portable) and the executable (ampify). See the convention at
# the top of CMakeLists.txt.
list(APPEND AMPIFY_CLI_PORTABLE_SOURCES
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

list(APPEND AMPIFY_CLI_SOURCES
    cli/ampify.cpp
    cli/commands.cpp
    cli/marketplace.cpp
    cli/activation.cpp
    cli/author.cpp
    cli/sdk_types.cpp
    cli/develop.cpp
    cli/diagnose.cpp
    cli/transfer.cpp
    cli/test_runner.cpp
    cli/theme_import.cpp
    cli/maintenance.cpp)
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    list(APPEND AMPIFY_CLI_SOURCES cli/platform.cpp cli/platform_win.cpp)
else()
    list(APPEND AMPIFY_CLI_SOURCES cli/platform.cpp cli/platform_posix.cpp)
endif()
