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

UiStallMonitor::UiStallMonitor(int64_t startMs) : m_windowStart(startMs) {}

void UiStallMonitor::Posted(int64_t nowMs) {
    m_postedAt.store(nowMs, std::memory_order_release);
}

void UiStallMonitor::Ran(int64_t nowMs) {
    int64_t posted = m_postedAt.exchange(kNone, std::memory_order_acq_rel);
    if (posted == kNone) return;
    int64_t delay = nowMs - posted;
    int64_t worst = m_worstDelay.load(std::memory_order_relaxed);
    while (delay > worst && !m_worstDelay.compare_exchange_weak(worst, delay, std::memory_order_relaxed)) {
    }
    m_beats.fetch_add(1, std::memory_order_relaxed);
    m_lastDelay.store(delay, std::memory_order_release);
}

std::vector<std::string> UiStallMonitor::Check(int64_t nowMs, const char* activity) {
    std::vector<std::string> lines;
    const std::string now = activity ? activity : "";
    int64_t delay = m_lastDelay.exchange(kNone, std::memory_order_acq_rel);
    if (delay != kNone) {
        if (delay >= kStallMs) {
            lines.push_back(StallLine(delay, m_inStall ? m_stallActivity : now));
            m_stallsInWindow++;
        }
        m_inStall = false;
    }
    int64_t posted = m_postedAt.load(std::memory_order_acquire);
    if (!m_inStall && posted != kNone && nowMs - posted >= kStallMs) {
        m_inStall = true;
        m_stallActivity = now;
    }
    if (nowMs - m_windowStart >= kSummaryMs) {
        lines.push_back("UI watchdog, last minute: " + std::to_string(m_beats.exchange(0)) + " beats, worst delay " +
                        std::to_string(m_worstDelay.exchange(0)) + " ms, " + std::to_string(m_stallsInWindow) +
                        " stall(s)");
        m_stallsInWindow = 0;
        m_windowStart = nowMs;
    }
    return lines;
}

std::string StallLine(int64_t ms, const std::string& activity) {
    return "UI thread stalled " + std::to_string(ms) + " ms, while: " + (activity.empty() ? "no Ampify work" : activity);
}

}  // namespace ampify
