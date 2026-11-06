#pragma once
// `text-align` on text Apple Music draws itself. The word-by-word lyrics (Border#WordByWordHost) hold no TextBlock:
// Apple lays the words out with Win2D and hangs them as composition visuals under the Border, every row at the left.
// Ampify moves each row of those visuals sideways (their TransformMatrix, which Apple leaves alone) and again after
// each size change of the host, when Apple lays the words out anew.

#if defined(AMPIFY_WITH_XAML)

#include <string_view>

#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.h>

#include "../core/theme/glyph_rows.hpp"

namespace ampify::win {

// The host of drawn text that `object` is (a Border with no XAML child), or null.
winrt::Microsoft::UI::Xaml::Controls::Border DrawnTextHost(const winrt::Microsoft::UI::Xaml::DependencyObject& object);

// The alignment given to the host, as a TextAlignment member name: "Left" (Apple's own placement) until one is set.
std::string_view HostedTextAlign(const winrt::Microsoft::UI::Xaml::Controls::Border& host) noexcept;

// Aligns the host's rows, now and after each of its size changes. Start gives Apple's placement back and stops
// following. Interface thread only; the host is Apple's, so it is held weakly.
void SetHostedTextAlign(const winrt::Microsoft::UI::Xaml::Controls::Border& host, theme::RowAlign align) noexcept;

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
