# The command line `ampify`: its portable library (ampify_cli_portable) and the executable (ampify). See the convention at
# the top of CMakeLists.txt.
list(APPEND AMPIFY_CLI_PORTABLE_SOURCES
    src/cli/sha256.cpp
    src/cli/element.cpp
    src/cli/catalog.cpp
    src/cli/kit.cpp
    src/cli/backup.cpp
    src/cli/archive.cpp
    src/cli/watch.cpp
    src/cli/release.cpp
    src/cli/zip_writer.cpp
    src/cli/anonymize.cpp
    src/cli/compile.cpp
    src/cli/theme_commands.cpp)

list(APPEND AMPIFY_CLI_SOURCES
    src/cli/ampify.cpp
    src/cli/commands.cpp
    src/cli/setting_keys.cpp
    src/cli/marketplace.cpp
    src/cli/activation.cpp
    src/cli/author.cpp
    src/cli/publish.cpp
    src/cli/sdk_types.cpp
    src/cli/develop.cpp
    src/cli/diagnose.cpp
    src/cli/report.cpp
    src/cli/transfer.cpp
    src/cli/test_runner.cpp
    src/cli/theme_import.cpp
    src/cli/maintenance.cpp)
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    list(APPEND AMPIFY_CLI_SOURCES src/cli/platform.cpp src/cli/platform_win.cpp)
else()
    list(APPEND AMPIFY_CLI_SOURCES src/cli/platform.cpp src/cli/platform_posix.cpp)
endif()
