#pragma once
// Whether anyone can see Ampify's interface: Apple Music's main window on screen, not minimized and not hidden in the
// notification area. Work whose only effect is on screen (an animated background, the karaoke, the visualiser bars,
// the loops of a theme) runs while the window is seen and pauses while it is not; the moment it shows again, that
// work catches up with the song. One state for the whole kernel: the interface sets it, any thread reads it.

#include <atomic>
#include <functional>
#include <map>
#include <mutex>

namespace ampify {

class Presence {
public:
    using Listener = std::function<void(bool seen)>;

    bool Seen() const noexcept { return m_seen.load(); }

    // Records the state. True when it changed, once every listener heard it, on this caller's thread.
    bool Set(bool seen);

    // Hears every change until Forget(id). A listener may call Forget, even its own, while it is called.
    int Listen(Listener listener);
    void Forget(int id);

private:
    std::atomic<bool> m_seen{true};  // seen until told otherwise: nothing pauses before the window is known
    std::mutex m_lock;
    std::map<int, Listener> m_listeners;
    int m_next = 0;
};

// A window's box on the screen, in pixels: right and bottom are past its last pixel.
struct ScreenBox {
    long left = 0;
    long top = 0;
    long right = 0;
    long bottom = 0;
};

// Whether `over`, in front, hides all of `under`: a game or a full-screen video over Apple Music, a maximized window
// in front of it. An empty `under` is never hidden this way.
inline bool Covers(const ScreenBox& over, const ScreenBox& under) noexcept {
    if (under.right <= under.left || under.bottom <= under.top) return false;
    return over.left <= under.left && over.top <= under.top && over.right >= under.right && over.bottom >= under.bottom;
}

// The kernel's one presence: Apple Music's main window, set on its interface thread (src/win/presence_watch.cpp).
Presence& InterfacePresence();

// Listens for as long as it lives (one owner, never copied).
class PresenceListener {
public:
    PresenceListener(Presence& presence, Presence::Listener listener)
        : m_presence(presence), m_id(presence.Listen(std::move(listener))) {}
    ~PresenceListener() { m_presence.Forget(m_id); }
    PresenceListener(const PresenceListener&) = delete;
    PresenceListener& operator=(const PresenceListener&) = delete;

private:
    Presence& m_presence;
    int m_id;
};

}  // namespace ampify
