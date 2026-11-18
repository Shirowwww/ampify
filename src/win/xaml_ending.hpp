#pragma once

// Apple Music ends its interface thread by shutting XAML down, then draining the thread's DispatcherQueue: timers and
// queued work still run there with the windows closed and XAML gone, and any XAML call that fails then brings Apple
// Music down (WinUI builds the error's message from a XAML core that no longer exists). Window.Closed does not come
// first when Apple Music exits. Every timer tick and queued task of Ampify's interface code goes through OnUiTick or
// TryEnqueueUi, which do nothing once the queue started shutting down; AppWindowOf gives no window then either.

#if defined(AMPIFY_WITH_XAML)

#include <utility>

#include <winrt/Microsoft.UI.Dispatching.h>

namespace ampify::win {

// True once the interface thread's DispatcherQueue started shutting down: no XAML call is safe any more. Any thread.
bool XamlEnding() noexcept;

// Follows the shutdown of `queue`, the main window's. Once is enough; any thread.
void WatchXamlEnding(const winrt::Microsoft::UI::Dispatching::DispatcherQueue& queue) noexcept;

// timer.Tick(handler), whose ticks do nothing once XamlEnding(). Returns the event's token, as Tick does.
template <typename Handler>
winrt::event_token OnUiTick(const winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer& timer, Handler handler) {
    return timer.Tick([handler = std::move(handler)](auto const& sender, auto const& args) mutable noexcept {
        if (XamlEnding()) return;
        try {
            handler(sender, args);
        } catch (...) {
            // A handler runs inside Apple Music's XAML frame: nothing may escape it.
        }
    });
}

// queue.TryEnqueue(work) and queue.TryEnqueue(priority, work), whose work does nothing once XamlEnding().
template <typename Work>
bool TryEnqueueUi(const winrt::Microsoft::UI::Dispatching::DispatcherQueue& queue, Work work) {
    return queue.TryEnqueue([work = std::move(work)]() mutable noexcept {
        if (XamlEnding()) return;
        try {
            work();
        } catch (...) {
            // Nothing may escape into Apple Music's XAML frame.
        }
    });
}

template <typename Work>
bool TryEnqueueUi(const winrt::Microsoft::UI::Dispatching::DispatcherQueue& queue,
                  winrt::Microsoft::UI::Dispatching::DispatcherQueuePriority priority, Work work) {
    return queue.TryEnqueue(priority, [work = std::move(work)]() mutable noexcept {
        if (XamlEnding()) return;
        try {
            work();
        } catch (...) {
            // Nothing may escape into Apple Music's XAML frame.
        }
    });
}

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
