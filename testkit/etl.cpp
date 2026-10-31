#include "etl.hpp"

#include <knownfolders.h>
#include <shlobj.h>

#include <iterator>

#include "../win/win_common.hpp"

namespace ampify::testkit {

namespace {

// Providers recorded during a run, as logman takes them: name, keyword mask, level.
// Kernel-Process: processes, threads and image loads, which show the kernel being mapped into each process.
// Audio: the audio engine, which shows the output path. The names and masks are checked on the Windows CI bench;
// when this Windows build refuses the list, StartTrace is called again with the first provider only.
struct Provider {
    const wchar_t* name;
    const wchar_t* keywords;
    const wchar_t* level;
};

constexpr Provider kProviders[] = {
    {L"Microsoft-Windows-Kernel-Process", L"0x70", L"4"},
    {L"Microsoft-Windows-Audio", L"0xFFFF", L"4"},
};

std::filesystem::path LogmanPath() {
    return ampify::win::KnownFolder(FOLDERID_System) / L"logman.exe";
}

}  // namespace

int StartTrace(const std::filesystem::path& etlFile, bool firstProviderOnly) {
    std::wstring parameters = std::wstring(L"create trace ") + kTraceSession + L" -ets -f etl -o \"" +
                              etlFile.wstring() + L"\"";
    size_t count = firstProviderOnly ? 1 : std::size(kProviders);
    for (size_t i = 0; i < count; i++) {
        parameters += std::wstring(L" -p \"") + kProviders[i].name + L"\" " + kProviders[i].keywords + L" " +
                      kProviders[i].level;
    }
    return ampify::win::RunElevatedAndWait(LogmanPath(), parameters);
}

int StopTrace() {
    return ampify::win::RunElevatedAndWait(LogmanPath(), std::wstring(L"stop ") + kTraceSession + L" -ets");
}

}  // namespace ampify::testkit
