#include "package_watch.hpp"

#include <initializer_list>

namespace amk {

PackageWatch::PackageWatch(WatchConfig config) : m_config(config) {}

void PackageWatch::MarkHandled(uint32_t processId) {
    m_handled.insert(processId);
}

WatchActions PackageWatch::Step(const WatchSnapshot& snapshot) {
    WatchActions actions;

    if (m_config.inject) {
        for (const std::vector<uint32_t>* group : {&snapshot.appProcesses, &snapshot.agentProcesses}) {
            for (uint32_t processId : *group) {
                if (m_handled.insert(processId).second) actions.inject.push_back(processId);
            }
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
            actions.stopReason = "Apple Music n'est pas apparu";
        }
    } else if (snapshot.appProcesses.empty() && snapshot.seconds - m_lastSeen >= m_config.goneAfter) {
        actions.stop = true;
        actions.stopReason = "Apple Music fermé";
    }
    return actions;
}

}  // namespace amk
