#include "extension.hpp"

#include <algorithm>
#include <exception>
#include <utility>

namespace amk {

ExtensionHost::ExtensionHost(Journal& journal) : m_journal(journal) {}

bool ExtensionHost::Add(ExtensionInfo info) {
    if (info.id.empty()) return false;

    bool duplicate = std::any_of(m_entries.begin(), m_entries.end(),
                                 [&](const Entry& entry) { return entry.info.id == info.id; });
    if (duplicate) return false;

    m_entries.push_back({std::move(info), ExtensionState::Off});
    return true;
}

void ExtensionHost::StartAll(const Settings& settings, std::string_view blockReason) {
    for (Entry& entry : m_entries) {
        bool enabled = settings.GetBool(SettingKey(entry.info.id), entry.info.enabledByDefault);
        if (!enabled) {
            entry.state = ExtensionState::Off;
            continue;
        }

        if (!blockReason.empty()) {
            entry.state = ExtensionState::Blocked;
            m_journal.Write("Extension " + entry.info.id + " non démarrée : " + std::string(blockReason));
            continue;
        }

        bool started = false;
        try {
            started = entry.info.start && entry.info.start();
        } catch (const std::exception& error) {
            m_journal.Write("Extension " + entry.info.id + " a levé une erreur : " + error.what());
        } catch (...) {
            m_journal.Write("Extension " + entry.info.id + " a levé une erreur inconnue");
        }

        entry.state = started ? ExtensionState::Running : ExtensionState::Failed;
        m_journal.Write("Extension " + entry.info.id + (started ? " démarrée" : " en échec"));
    }
}

void ExtensionHost::StopAll() {
    for (auto it = m_entries.rbegin(); it != m_entries.rend(); ++it) {
        if (it->state != ExtensionState::Running) continue;

        try {
            if (it->info.stop) it->info.stop();
            m_journal.Write("Extension " + it->info.id + " arrêtée");
        } catch (const std::exception& error) {
            m_journal.Write("Extension " + it->info.id + " n'a pas pu s'arrêter proprement : " + error.what());
        } catch (...) {
            m_journal.Write("Extension " + it->info.id + " n'a pas pu s'arrêter proprement");
        }
        it->state = ExtensionState::Off;
    }
}

ExtensionState ExtensionHost::StateOf(const std::string& id) const {
    for (const Entry& entry : m_entries) {
        if (entry.info.id == id) return entry.state;
    }
    return ExtensionState::Off;
}

std::string ExtensionHost::SettingKey(const std::string& id) {
    return "extensions." + id + ".enabled";
}

}  // namespace amk
