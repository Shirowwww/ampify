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
    for (Entry& entry : m_entries) StartEntry(entry, settings, blockReason);
}

void ExtensionHost::StartEntry(Entry& entry, const Settings& settings, std::string_view blockReason) {
    bool enabled = entry.info.isEnabled ? entry.info.isEnabled(settings)
                                        : settings.GetBool(SettingKey(entry.info.id), entry.info.enabledByDefault);
    if (!enabled) {
        entry.state = ExtensionState::Off;
        return;
    }

    if (!blockReason.empty()) {
        entry.state = ExtensionState::Blocked;
        m_journal.Write("Extension " + entry.info.id + " non démarrée : " + std::string(blockReason));
        return;
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

void ExtensionHost::StopAll() {
    for (auto it = m_entries.rbegin(); it != m_entries.rend(); ++it) StopEntry(*it);
}

void ExtensionHost::StopEntry(Entry& entry) {
    if (entry.state != ExtensionState::Running) return;

    try {
        if (entry.info.stop) entry.info.stop();
        m_journal.Write("Extension " + entry.info.id + " arrêtée");
    } catch (const std::exception& error) {
        m_journal.Write("Extension " + entry.info.id + " n'a pas pu s'arrêter proprement : " + error.what());
    } catch (...) {
        m_journal.Write("Extension " + entry.info.id + " n'a pas pu s'arrêter proprement");
    }
    entry.state = ExtensionState::Off;
}

bool ExtensionHost::Restart(const std::string& id, const Settings& settings) {
    for (Entry& entry : m_entries) {
        if (entry.info.id != id) continue;
        StopEntry(entry);
        StartEntry(entry, settings, {});
        return true;
    }
    return false;
}

bool ExtensionHost::Start(const std::string& id, const Settings& settings) {
    for (Entry& entry : m_entries) {
        if (entry.info.id != id) continue;
        if (entry.state != ExtensionState::Running) StartEntry(entry, settings, {});
        return true;
    }
    return false;
}

bool ExtensionHost::Stop(const std::string& id) {
    for (Entry& entry : m_entries) {
        if (entry.info.id != id) continue;
        StopEntry(entry);
        entry.state = ExtensionState::Off;  // a failed or blocked one is off too once asked to stop
        return true;
    }
    return false;
}

bool ExtensionHost::Remove(const std::string& id) {
    for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
        if (it->info.id != id) continue;
        StopEntry(*it);
        m_entries.erase(it);
        return true;
    }
    return false;
}

bool ExtensionHost::Has(const std::string& id) const {
    return std::any_of(m_entries.begin(), m_entries.end(), [&](const Entry& entry) { return entry.info.id == id; });
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
