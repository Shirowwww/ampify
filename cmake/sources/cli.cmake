# The command line `amk`: its portable library (amk_cli_portable) and the executable (amk). See the convention at
# the top of CMakeLists.txt.
list(APPEND AMK_CLI_PORTABLE_SOURCES
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

list(APPEND AMK_CLI_SOURCES
    cli/amk.cpp
    cli/commands.cpp
    cli/marketplace.cpp
    cli/activation.cpp
    cli/author.cpp
    cli/sdk_types.cpp
    cli/develop.cpp
    cli/diagnose.cpp
    cli/transfer.cpp
    cli/test_runner.cpp
    cli/maintenance.cpp)
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    list(APPEND AMK_CLI_SOURCES cli/platform.cpp cli/platform_win.cpp)
else()
    list(APPEND AMK_CLI_SOURCES cli/platform.cpp cli/platform_posix.cpp)
endif()
