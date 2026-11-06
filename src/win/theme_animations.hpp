#pragma once

// The `animation` values of a theme on Apple Music's live tree: keyframe animations of the compositor, started on the
// properties each element animates by itself (Opacity, Scale, Rotation, Translation), so they stay smooth while the
// interface thread is busy. Built only with the Windows App SDK headers (AMPIFY_WITH_XAML). Every member must be
// called on the interface thread.

#if defined(AMPIFY_WITH_XAML)

#include <chrono>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <winrt/Microsoft.UI.Composition.h>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
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
    // True while an animation of the element draws its background colour: the engine must not write the element's
    // `background` meanwhile, or the two would take turns. The element's own brush comes back when the animation stops
    // or ends without `forwards`.
    bool OwnsBackground(theme::NodeId id) const;
    // The `background` the theme asked for meanwhile. It reads back as the element's value, so that the engine does not
    // write it again at every pass; nothing when no colour animation draws this element's background.
    void WantBackground(theme::NodeId id, const theme::Value& value);
    std::optional<theme::Value> WantedBackground(theme::NodeId id) const;
    // Forgets the elements that were destroyed.
    void Sweep();
    // Stops and forgets the animations of an element Ampify made (a ::before or ::after decor) that leaves the tree:
    // such an element is held strongly, so Sweep never sees it go.
    void Forget(theme::NodeId id);

private:
    struct Running {
        winrt::weak_ref<winrt::Microsoft::UI::Xaml::UIElement> element;
        // Set instead for an element Ampify made: a weak reference to it never resolves inside Apple Music, and the
        // stepped loops of a decor (the turning artwork) never ran.
        winrt::Microsoft::UI::Xaml::UIElement held{nullptr};
        std::string text;
        std::vector<winrt::Microsoft::UI::Composition::CompositionAnimation> animations;
        // Keeps the centre of scale and rotation on the origin while the element changes size.
        winrt::Microsoft::UI::Composition::ExpressionAnimation center{nullptr};
        bool centered = false;  // CenterPoint was changed: centerBefore goes back once the animations stop
        winrt::Windows::Foundation::Numerics::float3 centerBefore{};
        // An element a stepped animation turns or scales has its centre set from its size at each step: the expression
        // above followed the size of its visual, and a record the theme made smaller turned around the point of its old size.
        winrt::Windows::Foundation::Point origin{0.5f, 0.5f};
        bool stepCentre = false;
        winrt::Windows::Foundation::Numerics::float3 steppedCentre{};  // the CenterPoint set at the last step
        // A finite animation without `forwards` runs on and is stopped when it ends: stopping gives the element its
        // own values back, which a completed composition animation does not. The timers hold no pointer to this
        // object, only the element and the animations to stop.
        std::vector<winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer> ends;
        // The endless animations, stepped by StepLoops from `started`, and the element's values before them.
        std::vector<theme::Animation> loops;
        std::vector<theme::ChannelValue> own;
        std::chrono::steady_clock::time_point started;
        // The time the `playing` loops spent holding still since `started`, counted from the step before.
        double heldMs = 0;
        std::chrono::steady_clock::time_point lastStep;
        bool stillShown = false;  // the held frame of the `playing` loops was applied while nothing plays
        // A background colour is stepped like an endless animation. The brush the element had goes back at the end.
        winrt::Microsoft::UI::Xaml::Media::Brush ownBackground{nullptr};
        bool backgroundAnimated = false;
        std::optional<theme::Value> wantedBackground;
    };

    static winrt::Microsoft::UI::Xaml::UIElement Target(const Running& running);
    static void Stop(Running& running);
    static void Center(Running& running, const winrt::Microsoft::UI::Composition::Compositor& compositor,
                       const winrt::Microsoft::UI::Xaml::UIElement& element, winrt::Windows::Foundation::Point origin);
    static void EndAt(Running& running, double ms,
                      std::vector<winrt::Microsoft::UI::Composition::CompositionAnimation> parts);

    bool MotionAllowed();
    void StartLoops(Running& running, std::vector<theme::Animation> loops);
    void StepLoops();

    bool m_wasPlaying = true;  // at the previous step of the loops
    std::map<theme::NodeId, Running> m_running;
    HWND__* m_window = nullptr;
    winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer m_loopTimer{nullptr};
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
    winrt::Windows::UI::ViewManagement::UISettings m_settings{nullptr};  // made once, read at each start
};

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
