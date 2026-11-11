#include "presence.hpp"

#include <utility>
#include <vector>

namespace ampify {

bool Presence::Set(bool seen) {
    if (m_seen.exchange(seen) == seen) return false;
    // Called outside the lock, so a listener may listen, forget or read again. Each is looked up just before its
    // call: one forgotten meanwhile (its owner destroyed by an earlier listener) is not called.
    std::vector<int> ids;
    {
        std::lock_guard<std::mutex> guard(m_lock);
        for (const auto& entry : m_listeners) ids.push_back(entry.first);
    }
    for (int id : ids) {
        Listener listener;
        {
            std::lock_guard<std::mutex> guard(m_lock);
            auto found = m_listeners.find(id);
            if (found == m_listeners.end()) continue;
            listener = found->second;
        }
        if (listener) listener(seen);
    }
    return true;
}

int Presence::Listen(Listener listener) {
    std::lock_guard<std::mutex> guard(m_lock);
    int id = ++m_next;
    m_listeners.emplace(id, std::move(listener));
    return id;
}

void Presence::Forget(int id) {
    std::lock_guard<std::mutex> guard(m_lock);
    m_listeners.erase(id);
}

Presence& InterfacePresence() {
    static Presence* presence = new Presence();  // never destroyed: listeners may forget during the process's end
    return *presence;
}

}  // namespace ampify
