#include "ui_watchdog.hpp"

#include <chrono>
#include <memory>
#include <string>
#include <thread>

#include "../core/ui_watchdog.hpp"
#include "ui_thread.hpp"

namespace ampify::win {

namespace {

constexpr auto kMonitorStep = std::chrono::milliseconds(100);
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
            // The beat only reads the clock on the interface thread; the copies keep the monitor alive for late beats.
            bool queued = PostToUiThread([monitor, gate] {
                monitor->Beat(NowMs());
                gate->Release();
            });
            if (!queued) {
                gate->Release();
                journal.Write("UI watchdog off: the interface thread no longer takes work");
                return;
            }
        }
        for (const std::string& line : monitor->Check(NowMs(), CurrentActivity())) journal.Write(line);
        std::this_thread::sleep_for(kMonitorStep);
    }
}

}  // namespace

void StartUiWatchdog(Journal& journal) {
    // Detached on purpose: the watch ends with the process, like the kernel's other threads.
    std::thread([&journal] { Watch(journal); }).detach();
}

}  // namespace ampify::win
