#pragma once

#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace ampify {

// What the launcher sees at one poll of the package.
struct WatchSnapshot {
    double seconds = 0;                    // since the watch started
    std::vector<uint32_t> appProcesses;    // AppleMusic.exe of the package, running now
    std::vector<uint32_t> agentProcesses;  // AMPLibraryAgent.exe of the package, running now
    bool appResponsive = false;            // at least one app process has no hung window
};

// What the launcher must do after a poll.
struct WatchActions {
    std::vector<uint32_t> inject;  // load the kernel into these processes now
    bool markHealthy = false;      // clear the safe-mode count
    bool stop = false;             // nothing is left to follow
    std::string stopReason;        // for the journal, when `stop` is set
    bool settled = false;          // the app has run past its start: the launcher may look less often
};

struct WatchConfig {
    bool inject = true;           // false: nothing is injected (safe mode, unknown build, kernel missing)
    bool reportHealth = false;    // true when no kernel reports health for this launch
    double healthyAfter = 20;     // seconds of a responsive app before health is reported
    double goneAfter = 5;         // seconds without any app process before the watch stops
    double firstSeenWithin = 15;  // seconds allowed for the app to appear at all
    double settleAfter = 30;      // seconds after the app appears, past its hand-overs and health, before it settles
};

// Follows one launch of Apple Music. Pure logic: the launcher feeds it one snapshot per poll.
//
// Apple Music can restart under a new process id: the process the launcher starts may hand over to
// another one and exit. So the watch follows the package, not a single pid. Every process of the
// package is injected once, and the watch stops only when no app process has been seen for a while.
class PackageWatch {
public:
    explicit PackageWatch(WatchConfig config);

    // Records a process the launcher has already dealt with, so it is not injected a second time.
    void MarkHandled(uint32_t processId);

    WatchActions Step(const WatchSnapshot& snapshot);

    // Whether the next snapshot needs `appResponsive`, which walks every window: only while health is awaited.
    bool NeedsResponsiveness() const { return m_config.reportHealth && !m_healthReported; }

private:
    WatchConfig m_config;
    std::set<uint32_t> m_handled;
    bool m_appSeen = false;
    double m_firstSeen = 0;  // first poll that saw an app process
    double m_lastSeen = 0;   // last poll that saw an app process
    bool m_healthReported = false;
};

}  // namespace ampify
