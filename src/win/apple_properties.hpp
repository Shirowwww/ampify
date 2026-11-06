#pragma once
// Properties of Apple Music's own controls that no WinRT projection of Ampify's knows: their dependency properties come
// from the statics interface of the control's class (AMP.UI.Controls.winmd), so the theme engine reads, writes and
// clears them as it does WinUI's. The progress line's track (ScrubberControl.TrackHeight, TrackRectRadius) and thumb
// (ThumbVisibility) are the first: its template sizes them, not the control's own Height.

#if defined(AMPIFY_WITH_XAML)

#include <winrt/Microsoft.UI.Xaml.h>

#include <optional>
#include <string_view>

#include "../core/theme/tree.hpp"

namespace ampify::win {

// True for a property name of this table, whatever the element.
bool IsAppleProperty(std::string_view property) noexcept;

// The dependency property, or null when the element is not of the class that has it. Interface thread only.
winrt::Microsoft::UI::Xaml::DependencyProperty AppleProperty(const winrt::Microsoft::UI::Xaml::DependencyObject& object,
                                                             std::string_view property) noexcept;

std::optional<theme::Value> ReadAppleProperty(const winrt::Microsoft::UI::Xaml::DependencyObject& object,
                                              std::string_view property) noexcept;

// False when the element lacks the property or the value does not fit it (a negative or automatic length).
bool WriteAppleProperty(const winrt::Microsoft::UI::Xaml::DependencyObject& object, std::string_view property,
                        const theme::Value& value) noexcept;

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
