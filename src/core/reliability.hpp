#pragma once

// What the kernel and `ampify doctor` say about Apple Music's own troubles, without Windows: network waits long
// enough to be felt (proxy detection, name lookups), the memory of a process followed in steps, and the programs
// known to make Apple Music crash at start. The Windows side times the calls and reads the processes
// (src/win/network_fix.cpp, src/win/ui_watchdog.cpp, src/cli/diagnose.cpp). Tests: tests/reliability_tests.cpp.

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace ampify {

inline constexpr int64_t kSlowCallMs = 300;  // the freeze log's own threshold
inline constexpr int kSlowCallLines = 20;    // per process, so a broken network cannot fill the log

// Network calls that waited `kSlowCallMs` or more. Any thread.
class SlowCallLog {
public:
    // The line to write for `what` (such as "proxy detection") that took `ms`, or empty: quick enough, or the
    // process's lines are spent. The last line says that the next ones are not written.
    std::string Record(std::string_view what, int64_t ms, bool onInterfaceThread);

private:
    std::mutex m_lock;
    int m_lines = 0;
};

inline constexpr uint64_t kMemoryStepBytes = 512ull * 1024 * 1024;

// The memory a process holds (private bytes), written once, then each time it reaches a new step of 512 MB, so
// a leak shows as a few lines and a steady process as one.
class MemoryWatch {
public:
    explicit MemoryWatch(std::string process);

    // The line to write for this reading, or empty. 0 is a reading that failed.
    std::string Check(uint64_t privateBytes);

private:
    std::string m_process;
    uint64_t m_nextStep = 0;  // 0 until the first reading
};

// A program that makes Apple Music crash or vanish while it runs, and what fixes it. Apple Music cannot be repaired
// from inside: the advice is for the person (reports of 2023 and 2024 on Apple's forums): 3Dconnexion's driver and
// RivaTuner's overlay.
struct KnownConflict {
    std::string id;
    std::string name;
    std::string advice;
};

// The known programs among `processes` (image names, any case), in a fixed order. `threeDxIgnoresAppleMusic`: the
// 3Dconnexion driver's IgnoredApps.xml already lists AppleMusic.exe, the fix for that one.
std::vector<KnownConflict> FindKnownConflicts(const std::vector<std::string>& processes, bool threeDxIgnoresAppleMusic);

// True when the text of 3Dconnexion's IgnoredApps.xml has an <ExecutableName> of AppleMusic.exe, any case.
bool IgnoredAppsListsAppleMusic(std::string_view xml);

// The same, read from its place under `programFiles`. False when the file is missing.
bool ThreeDxIgnoresAppleMusic(const std::filesystem::path& programFiles);

// The crash dumps Windows wrote in `folder` for Apple Music's two processes (AppleMusic.exe.<id>.dmp,
// AMPLibraryAgent.exe.<id>.dmp) during the last `days` days, file names only, newest first.
std::vector<std::string> RecentAppleCrashDumps(const std::filesystem::path& folder, int days);

// True when `folder` exists. Without it Windows records no crash dumps, so "none found" would prove nothing.
bool CrashDumpsAreRecorded(const std::filesystem::path& folder);

// What the freeze log (apple.watchdog) wrote in a log: its lines in order (stalls of the interface thread, the summary
// of each minute, slow network waits, memory steps), how many stalls and the longest. `ampify doctor` and `ampify
// report` read it from every log.
struct FreezeDigest {
    std::vector<std::string> lines;
    int stalls = 0;
    int64_t worstStallMs = 0;
};

bool IsFreezeLogLine(std::string_view line);
FreezeDigest DigestFreezeLog(std::string_view logText);

}  // namespace ampify
