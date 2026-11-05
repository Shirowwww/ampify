#include "orphan_agent.hpp"

namespace ampify {

OrphanAction DecideOrphanAgent(bool enabled, bool appRunning, bool agentRunning, uint64_t waitedMs) {
    if (!enabled || appRunning || !agentRunning) return OrphanAction::None;
    return waitedMs < kOrphanGraceMs ? OrphanAction::WaitForExit : OrphanAction::End;
}

}  // namespace ampify
