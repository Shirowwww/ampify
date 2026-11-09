#pragma once

// The motion of what Ampify draws itself (its settings page, its cards): one set of durations and one easing, so
// everything that moves on those surfaces moves alike. Each helper gives the final state at once when Windows'
// animation effects are off (MotionAllowed). Themes animate Apple's elements through their own `transition`.

#if defined(AMPIFY_WITH_XAML)

#include <chrono>

#include <winrt/Microsoft.UI.Xaml.h>

namespace ampify::win::motion {

inline constexpr std::chrono::milliseconds kQuick{120};   // a state that follows the pointer or a click
inline constexpr std::chrono::milliseconds kSwitch{180};  // content that replaces other content
inline constexpr std::chrono::milliseconds kGlide{220};   // something that travels across the page
inline constexpr float kRise = 8;                         // how far content comes from when it appears

// Fades `element` in while it rises `rise` pixels into place, on the compositor: no layout pass, nothing under it
// moves. For content that just became visible (a tab's page, the results of a search).
void Enter(const winrt::Microsoft::UI::Xaml::UIElement& element, float rise = kRise,
           std::chrono::milliseconds duration = kSwitch) noexcept;

// Moves `element` to `x` and stretches it to `scaleX` of its width, easing from where it is now. `animate` false
// places it at once (its first place, before anything was seen).
void Glide(const winrt::Microsoft::UI::Xaml::UIElement& element, float x, float scaleX, bool animate,
           std::chrono::milliseconds duration = kGlide) noexcept;

// Eases the opacity of `element` to `opacity` (selected and unselected states, a ring that shows the choice).
void FadeTo(const winrt::Microsoft::UI::Xaml::UIElement& element, double opacity,
            std::chrono::milliseconds duration = kQuick) noexcept;

}  // namespace ampify::win::motion

#endif  // AMPIFY_WITH_XAML
