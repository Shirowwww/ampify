#include "orphan_agent.hpp"

#include <utility>

namespace ampify {

OrphanAction DecideOrphanAgent(bool enabled, bool appRunning, bool agentRunning, uint64_t waitedMs) {
    if (!enabled || appRunning || !agentRunning) return OrphanAction::None;
    return waitedMs < kOrphanGraceMs ? OrphanAction::WaitForExit : OrphanAction::End;
}

std::vector<uint32_t> OrphanRefusals::Untried(const std::vector<uint32_t>& running) {
    std::set<uint32_t> stillRefused;
    std::vector<uint32_t> untried;
    for (uint32_t processId : running) {
        if (m_refused.count(processId)) stillRefused.insert(processId);
        else untried.push_back(processId);
    }
    m_refused = std::move(stillRefused);
    return untried;
}

void OrphanRefusals::Refused(uint32_t processId) {
    m_refused.insert(processId);
}

}  // namespace ampify
