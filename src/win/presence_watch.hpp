#pragma once
// Keeps the kernel's presence (src/core/presence.hpp) in step with Apple Music's main window, and the timers of the
// interface that only draw: they stop while nobody sees the window and start again, at once, when it shows.

#if defined(AMPIFY_WITH_XAML)

#include <functional>
#include <memory>

#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Windows.Foundation.h>

#include "../core/presence.hpp"

namespace ampify {
class Journal;
}

namespace ampify::win {

// Follows Apple Music's windows (the main one, the full-screen player, the mini player): shown, minimized, hidden in
// the notification area, on another virtual desktop. Seen while one of them is on screen. Once, from the main
// window's interface thread, whose message loop brings the events. Each change is written to the journal.
void WatchPresence(Journal& journal) noexcept;

// Reads the main window again, should an event have been missed. Interface thread only.
void RefreshPresence() noexcept;

// Hears the presence on the interface thread that made it: Apple's mini player has its own thread, whose XAML answers
// only there, so a change heard on another thread is posted to this one's queue. Destroyed on its own thread.
class SeenListener {
public:
    explicit SeenListener(std::function<void(bool seen)> listener);
    SeenListener(const SeenListener&) = delete;
    SeenListener& operator=(const SeenListener&) = delete;

private:
    std::shared_ptr<std::function<void(bool)>> m_listener;  // a post that comes after the owner is gone does nothing
    PresenceListener m_presence;
};

// A timer of the interface thread that ticks only while it is wanted (Run) and the window is seen. Hidden, it stops
// instead of ticking for nobody; seen again, it ticks once at once, so what it draws catches up, then at its pace.
// Interface thread only, from creation to destruction.
class SeenTimer {
public:
    SeenTimer();
    ~SeenTimer();
    SeenTimer(const SeenTimer&) = delete;
    SeenTimer& operator=(const SeenTimer&) = delete;

    // Wants the timer: `tick` at `interval`, from now on while the window is seen. A tick that throws is dropped.
    void Run(winrt::Windows::Foundation::TimeSpan interval, std::function<void()> tick);
    // No longer wanted.
    void Stop() noexcept;
    // A new pace, kept until the next one.
    void Interval(winrt::Windows::Foundation::TimeSpan interval);
    bool Wanted() const noexcept { return m_wanted; }

private:
    void Follow(bool seen) noexcept;
    void Tick() noexcept;

    winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer m_timer{nullptr};
    std::function<void()> m_tick;
    winrt::Windows::Foundation::TimeSpan m_interval{0};
    bool m_wanted = false;
    SeenListener m_presence;
};

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
