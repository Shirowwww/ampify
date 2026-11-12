#include "ui_watchdog.hpp"

#include <chrono>
#include <memory>
#include <string>
#include <thread>

#include "../core/presence.hpp"
#include "../core/ui_watchdog.hpp"
#include "ui_thread.hpp"

namespace ampify::win {

namespace {

constexpr auto kMonitorStep = std::chrono::milliseconds(100);
// While nobody sees the window, a freeze matters less than waking the interface thread five times a second: the
// steps space out, a beat each second.
constexpr auto kUnseenMonitorStep = std::chrono::milliseconds(500);
constexpr int kBeatEveryMonitorSteps = 2;  // 200 ms

int64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

void Watch(Journal& journal) {
    while (!UiThreadAttached()) std::this_thread::sleep_for(kMonitorStep);

    auto monitor = std::make_shared<UiStallMonitor>(NowMs());
    auto gate = std::make_shared<BeatGate>();
    journal.Write("UI watchdog on: a freeze of 300 ms or more is logged with what Ampify was doing, a summary each minute");
    for (int step = 0;; ++step) {
        // A beat is queued only when the last one ran: a frozen interface thread must not pile up closures.
        if (step % kBeatEveryMonitorSteps == 0 && gate->TryTake()) {
            // Stamped when queued: the wait until it runs is the block. The copies keep the monitor alive for late beats.
            monitor->Posted(NowMs());
            bool queued = PostToUiThread([monitor, gate] {
                monitor->Ran(NowMs());
                gate->Release();
            });
            if (!queued) {
                gate->Release();
                journal.Write("UI watchdog off: the interface thread no longer takes work");
                return;
            }
        }
        for (const std::string& line : monitor->Check(NowMs(), CurrentActivity())) journal.Write(line);
        std::this_thread::sleep_for(InterfacePresence().Seen() ? kMonitorStep : kUnseenMonitorStep);
    }
}

}  // namespace

void StartUiWatchdog(Journal& journal) {
    // Detached on purpose: the watch ends with the process, like the kernel's other threads.
    std::thread([&journal] { Watch(journal); }).detach();
}

}  // namespace ampify::win
