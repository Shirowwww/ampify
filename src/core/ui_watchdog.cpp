#include "ui_watchdog.hpp"

namespace ampify {

namespace {

std::atomic<const char*> g_activity{nullptr};

}  // namespace

const char* CurrentActivity() {
    return g_activity.load(std::memory_order_acquire);
}

ActivityScope::ActivityScope(const char* name) : m_previous(g_activity.exchange(name, std::memory_order_acq_rel)) {}

ActivityScope::~ActivityScope() {
    g_activity.store(m_previous, std::memory_order_release);
}

bool BeatGate::TryTake() {
    return !m_pending.exchange(true, std::memory_order_acq_rel);
}

void BeatGate::Release() {
    m_pending.store(false, std::memory_order_release);
}

UiStallMonitor::UiStallMonitor(int64_t startMs) : m_last(startMs), m_seen(startMs), m_windowStart(startMs) {}

void UiStallMonitor::Beat(int64_t nowMs) {
    int64_t previous = m_last.exchange(nowMs, std::memory_order_acq_rel);
    int64_t gap = nowMs - previous;
    int64_t worst = m_worstGap.load(std::memory_order_relaxed);
    // A compare-and-swap keeps the largest gap of the minute, even when two beats race.
    while (gap > worst && !m_worstGap.compare_exchange_weak(worst, gap, std::memory_order_relaxed)) {
    }
    m_beats.fetch_add(1, std::memory_order_relaxed);
}

std::vector<std::string> UiStallMonitor::Check(int64_t nowMs, const char* activity) {
    std::vector<std::string> lines;
    int64_t last = m_last.load(std::memory_order_acquire);
    if (last != m_seen) {
        if (m_inStall) {
            lines.push_back(StallLine(last - m_stallSince, m_stallActivity));
            m_inStall = false;
            m_stallsInWindow++;
        }
        m_seen = last;
    }
    if (!m_inStall && nowMs - last >= kStallMs) {
        m_inStall = true;
        m_stallSince = last;
        m_stallActivity = activity ? activity : "";
    }
    if (nowMs - m_windowStart >= kSummaryMs) {
        lines.push_back("UI watchdog, last minute: " + std::to_string(m_beats.exchange(0)) + " beats, worst gap " +
                        std::to_string(m_worstGap.exchange(0)) + " ms, " + std::to_string(m_stallsInWindow) + " stall(s)");
        m_stallsInWindow = 0;
        m_windowStart = nowMs;
    }
    return lines;
}

std::string StallLine(int64_t ms, const std::string& activity) {
    return "UI thread stalled " + std::to_string(ms) + " ms, while: " + (activity.empty() ? "no Ampify work" : activity);
}

}  // namespace ampify
