#pragma once

// Freeze watch of the interface thread: the logic, without Windows. The heartbeat and the monitor thread are in
// src/win/ui_watchdog.*. The interface thread beats every 200 ms; a silence of 300 ms or more is a stall, logged once
// the beats resume, with what the interface was doing (ActivityScope).

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace ampify {

inline constexpr int64_t kStallMs = 300;
inline constexpr int64_t kSummaryMs = 60000;

// What the interface thread is doing now, or nullptr. Read from the monitor thread; only the interface thread sets it.
const char* CurrentActivity();

// Names the work of the interface thread for the stall line. Nests: the inner name shows until it ends.
class ActivityScope {
public:
    explicit ActivityScope(const char* name);
    ~ActivityScope();
    ActivityScope(const ActivityScope&) = delete;
    ActivityScope& operator=(const ActivityScope&) = delete;

private:
    const char* m_previous;
};

// Keeps the monitor from queuing a beat while an earlier one still waits: a blocked interface thread must not pile up
// closures. Any thread.
class BeatGate {
public:
    // True when the caller may queue a beat now; false while one is waiting.
    bool TryTake();

    // The waiting beat ran, or its post was refused.
    void Release();

private:
    std::atomic<bool> m_pending{false};
};

class UiStallMonitor {
public:
    explicit UiStallMonitor(int64_t startMs);

    // The interface thread is alive at `nowMs`. Any thread.
    void Beat(int64_t nowMs);

    // Called by the monitor thread every 100 ms or so. Returns the lines to log now.
    std::vector<std::string> Check(int64_t nowMs, const char* activity);

private:
    std::atomic<int64_t> m_last;
    std::atomic<int64_t> m_beats{0};
    std::atomic<int64_t> m_worstGap{0};

    // Monitor thread only.
    int64_t m_seen;
    int64_t m_windowStart;
    bool m_inStall = false;
    int64_t m_stallSince = 0;
    std::string m_stallActivity;
    int m_stallsInWindow = 0;
};

std::string StallLine(int64_t ms, const std::string& activity);

}  // namespace ampify
