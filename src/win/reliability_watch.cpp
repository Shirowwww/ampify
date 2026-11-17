#include "reliability_watch.hpp"

#include <windows.h>
#include <knownfolders.h>
#include <psapi.h>

#include <chrono>
#include <thread>
#include <utility>
#include <vector>

#include "../core/reliability.hpp"
#include "win_common.hpp"

namespace ampify::win {

namespace {

constexpr auto kMemoryEvery = std::chrono::minutes(1);

// The private bytes, and the part of them in RAM: the working set would count shared DLL pages too. A Windows older
// than 10 2004 does not know the larger form, which leaves the RAM part out.
PROCESS_MEMORY_COUNTERS_EX2 Counters() {
    PROCESS_MEMORY_COUNTERS_EX2 counters{};
    counters.cb = sizeof(counters);
    auto* asked = reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters);
    if (GetProcessMemoryInfo(GetCurrentProcess(), asked, sizeof(counters))) return counters;
    counters = {};
    if (!GetProcessMemoryInfo(GetCurrentProcess(), asked, sizeof(PROCESS_MEMORY_COUNTERS_EX))) return {};
    return counters;  // PrivateWorkingSetSize stays 0: the older form has no such field
}

}  // namespace

void LogKnownConflicts(Journal& journal) {
    std::vector<std::string> processes = RunningImageNames();
    std::filesystem::path programFiles = KnownFolder(FOLDERID_ProgramFiles);
    bool threeDxFixed = !programFiles.empty() && ThreeDxIgnoresAppleMusic(programFiles);
    for (const KnownConflict& conflict : FindKnownConflicts(processes, threeDxFixed)) {
        journal.Write("Known conflict: " + conflict.name + " runs, known to crash Apple Music: " + conflict.advice);
    }
}

void StartMemoryWatch(Journal& journal, std::string process) {
    // Detached on purpose, like the freeze watch: it lives as long as the process.
    std::thread([&journal, process = std::move(process)] {
        MemoryWatch watch(process);
        while (true) {
            const PROCESS_MEMORY_COUNTERS_EX2 counters = Counters();
            std::string line = watch.Check(counters.PrivateUsage, counters.PrivateWorkingSetSize);
            if (!line.empty()) journal.Write(line);
            std::this_thread::sleep_for(kMemoryEvery);
        }
    }).detach();
}

}  // namespace ampify::win
