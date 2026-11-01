#pragma once

// The `animation` values of a theme on Apple Music's live tree: keyframe animations of the compositor, started on the
// properties each element animates by itself (Opacity, Scale, Rotation, Translation), so they stay smooth while the
// interface thread is busy. Built only with the Windows App SDK headers (AMPIFY_WITH_XAML). Every member must be
// called on the interface thread.

#if defined(AMPIFY_WITH_XAML)

#include <map>
#include <string>
#include <vector>

#include <winrt/Microsoft.UI.Composition.h>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Windows.UI.ViewManagement.h>

#include "../core/theme/tree.hpp"

namespace ampify::win {

class ThemeAnimations {
public:
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
    };

    static void Stop(Running& running);
    static void Center(Running& running, const winrt::Microsoft::UI::Composition::Compositor& compositor,
                       const winrt::Microsoft::UI::Xaml::UIElement& element, winrt::Windows::Foundation::Point origin);
    static void EndAt(Running& running, double ms,
                      std::vector<winrt::Microsoft::UI::Composition::CompositionAnimation> parts);

    bool MotionAllowed();

    std::map<theme::NodeId, Running> m_running;
    winrt::Windows::UI::ViewManagement::UISettings m_settings{nullptr};  // made once, read at each start
};

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
