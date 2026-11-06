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

uint64_t PrivateBytes() {
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    if (!GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                              sizeof(counters))) {
        return 0;
    }
    return counters.PrivateUsage;
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
            std::string line = watch.Check(PrivateBytes());
            if (!line.empty()) journal.Write(line);
            std::this_thread::sleep_for(kMemoryEvery);
        }
    }).detach();
}

}  // namespace ampify::win
