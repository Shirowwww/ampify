#pragma once

// The ETW trace of a test run, started and stopped with logman. Windows only.
// logman needs administrator rights, so both calls go through the administrator prompt when it is needed.

#include <windows.h>

#include <filesystem>

namespace amk::testkit {

// The trace session that the test kit creates and stops. The session lives only until it is stopped or Windows restarts.
inline constexpr const wchar_t* kTraceSession = L"AMK_M5";

// Starts the trace into `etlFile`. With `firstProviderOnly`, only the first provider is recorded: used when the
// full list is refused by this Windows build. Returns logman's exit code, or -1 when it did not run or was refused.
// Each call may show the administrator prompt once.
int StartTrace(const std::filesystem::path& etlFile, bool firstProviderOnly);

// Stops the trace session. Returns logman's exit code, or -1 when it did not run or was refused.
int StopTrace();

}  // namespace amk::testkit
