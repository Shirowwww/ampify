#include "package_watch.hpp"

#include <algorithm>
#include <initializer_list>

namespace ampify {

PackageWatch::PackageWatch(WatchConfig config) : m_config(config) {}

void PackageWatch::MarkHandled(uint32_t processId) {
    m_handled.insert(processId);
}

WatchActions PackageWatch::Step(const WatchSnapshot& snapshot) {
    WatchActions actions;

    if (m_config.inject) {
        for (uint32_t processId : snapshot.appProcesses) {
            if (m_handled.insert(processId).second) actions.inject.push_back(processId);
        }
        // An agent gone before it settled is forgotten, so a new one given the same pid waits its own time.
        std::erase_if(m_agentSeen, [&snapshot](const auto& entry) {
            return std::find(snapshot.agentProcesses.begin(), snapshot.agentProcesses.end(), entry.first) ==
                   snapshot.agentProcesses.end();
        });
        for (uint32_t processId : snapshot.agentProcesses) {
            if (m_handled.count(processId)) continue;
            double seen = m_agentSeen.emplace(processId, snapshot.seconds).first->second;
            if (snapshot.seconds - seen < m_config.agentSettleAfter) continue;
            m_handled.insert(processId);
            m_agentSeen.erase(processId);
            actions.inject.push_back(processId);
        }
    }

    if (!snapshot.appProcesses.empty()) {
        if (!m_appSeen) {
            m_appSeen = true;
            m_firstSeen = snapshot.seconds;
        }
        m_lastSeen = snapshot.seconds;
    }

    bool healthy = m_appSeen && !snapshot.appProcesses.empty() && snapshot.appResponsive;
    if (m_config.reportHealth && !m_healthReported && healthy &&
        snapshot.seconds - m_firstSeen >= m_config.healthyAfter) {
        actions.markHealthy = true;
        m_healthReported = true;
    }

    if (!m_appSeen) {
        if (snapshot.seconds >= m_config.firstSeenWithin) {
            actions.stop = true;
            actions.stopReason = "Apple Music did not appear";
        }
    } else if (snapshot.appProcesses.empty() && snapshot.seconds - m_lastSeen >= m_config.goneAfter) {
        actions.stop = true;
        actions.stopReason = "Apple Music closed";
    }
    actions.settled = m_appSeen && !NeedsResponsiveness() && snapshot.seconds - m_firstSeen >= m_config.settleAfter;
    return actions;
}

}  // namespace ampify
