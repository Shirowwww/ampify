#pragma once

// The `animation` values of a theme on Apple Music's live tree: keyframe animations of the compositor, started on the
// properties each element animates by itself (Opacity, Scale, Rotation, Translation), so they stay smooth while the
// interface thread is busy. Built only with the Windows App SDK headers (AMPIFY_WITH_XAML). Every member must be
// called on the interface thread.

#if defined(AMPIFY_WITH_XAML)

#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <winrt/Microsoft.UI.Composition.h>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Windows.UI.ViewManagement.h>

#include "../core/theme/animation.hpp"
#include "../core/theme/tree.hpp"

struct HWND__;  // windows.h's HWND, without its macros, which break the WinRT projection included after this

namespace ampify::win {

// An endless animation does not go to the compositor: it would redraw the whole window at every frame of the screen
// (about 570 ms of processor a second at 240 Hz, for a turning record). The backend steps it itself, kLoopFps times a
// second, and not at all while the window is hidden or minimized.
class ThemeAnimations {
public:
    ThemeAnimations() = default;
    ~ThemeAnimations();
    ThemeAnimations(const ThemeAnimations&) = delete;
    ThemeAnimations& operator=(const ThemeAnimations&) = delete;

    // The main window, whose state pauses the endless animations.
    void Window(HWND__* window) { m_window = window; }
    // Runs the animations of `text` (EncodeAnimations) on the element in place of the ones it ran, scaling and turning
    // it around `origin`, relative to its size. An empty text stops them. False when the element refuses: Apple Music
    // drives it through its composition visual or a RenderTransform.
    bool Write(theme::NodeId id, const winrt::Microsoft::UI::Xaml::UIElement& element, const std::string& text,
               winrt::Windows::Foundation::Point origin);
    // The text of the animations running on the element, empty for none. A finite animation that ended still counts:
    // it does not start again on the next pass.
    std::string Read(theme::NodeId id) const;
    // Forgets the elements that were destroyed.
    void Sweep();

private:
    struct Running {
        winrt::weak_ref<winrt::Microsoft::UI::Xaml::UIElement> element;
        std::string text;
        std::vector<winrt::Microsoft::UI::Composition::CompositionAnimation> animations;
        // Keeps the centre of scale and rotation on the origin while the element changes size.
        winrt::Microsoft::UI::Composition::ExpressionAnimation center{nullptr};
        bool centered = false;  // CenterPoint was changed: centerBefore goes back once the animations stop
        winrt::Windows::Foundation::Numerics::float3 centerBefore{};
        // A finite animation without `forwards` runs on and is stopped when it ends: stopping gives the element its
        // own values back, which a completed composition animation does not. The timers hold no pointer to this
        // object, only the element and the animations to stop.
        std::vector<winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer> ends;
        // The endless animations, stepped by StepLoops from `started`, and the element's values before them.
        std::vector<theme::Animation> loops;
        std::vector<theme::ChannelValue> own;
        std::chrono::steady_clock::time_point started;
    };

    static void Stop(Running& running);
    static void Center(Running& running, const winrt::Microsoft::UI::Composition::Compositor& compositor,
                       const winrt::Microsoft::UI::Xaml::UIElement& element, winrt::Windows::Foundation::Point origin);
    static void EndAt(Running& running, double ms,
                      std::vector<winrt::Microsoft::UI::Composition::CompositionAnimation> parts);

    bool MotionAllowed();
    void StartLoops(Running& running, std::vector<theme::Animation> loops);
    void StepLoops();

    std::map<theme::NodeId, Running> m_running;
    HWND__* m_window = nullptr;
    winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer m_loopTimer{nullptr};
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
    winrt::Windows::UI::ViewManagement::UISettings m_settings{nullptr};  // made once, read at each start
};

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
