#pragma once
// `backdrop: liquid-glass(tint, opacity[, blur[, saturation]])`: a XAML brush (XamlCompositionBrushBase) painting what
// lies behind the element, blurred lightly, its colours made more vivid, under the tint and a sheen from the top
// edge; the same effect descriptions as `filter` (image_effects.*). A brush, not a visual, so that it paints any
// panel, border or control whatever its children. When it starts painting, its blur and tint ease in.

#if defined(AMPIFY_WITH_XAML)

#include <optional>
#include <string>

#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Windows.UI.h>

namespace ampify::win {

struct GlassLook {
    winrt::Windows::UI::Color tint{};
    double opacity = 0;     // of the tint, 0 to 1
    double blur = 0;        // in pixels
    double saturation = 1;  // 1 keeps the colours behind as they are
};

winrt::Microsoft::UI::Xaml::Media::Brush LiquidGlassBrush(const GlassLook& look, const std::string& text);

// The value a liquid glass brush was made from (its ValueKind::Acrylic text); nothing for another brush.
std::optional<std::string> LiquidGlassText(const winrt::Microsoft::UI::Xaml::Media::Brush& brush);

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
