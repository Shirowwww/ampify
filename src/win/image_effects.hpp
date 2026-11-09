#pragma once
// The Direct2D effects Ampify's image effects are built from, described as Composition's effect factories read them
// (what Win2D's effect classes describe; Win2D is not loaded in Apple Music, and the Windows App SDK ships none of
// these). Shared by `filter` (element_blur.*) and the liquid glass backdrop (glass_brush.*): one description of each
// effect, one easing curve, so both animate alike.

#if defined(AMPIFY_WITH_XAML)

#include <vector>

#include <winrt/Microsoft.UI.Composition.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Effects.h>

#include "../core/theme/color_matrix.hpp"

namespace ampify::win::effects {

using Source = winrt::Windows::Graphics::Effects::IGraphicsEffectSource;
using Effect = winrt::Windows::Graphics::Effects::IGraphicsEffect;

// The animatable amount of GaussianBlur, as a brush's property: "Blur.BlurAmount".
inline constexpr wchar_t kBlurAmount[] = L"Blur.BlurAmount";

// Direct2D's Gaussian blur of `source`, named "Blur", its edges hard (the edges blur with what is inside).
Effect GaussianBlur(Source source);

// Direct2D's colour matrix on `source`: `matrix` is fixed when the factory compiles it (Composition animates no 5x4
// matrix), so a change of colours is a new brush, crossfaded.
Effect ColorMatrixOf(Source source, const theme::ColorMatrix& matrix);

// Direct2D's composite of `sources`, each drawn over the ones before it.
Effect SourceOver(std::vector<Source> sources);

// The curve of every easing of an image effect, the same as Ampify's other transitions start fast and settle.
winrt::Microsoft::UI::Composition::CompositionEasingFunction EaseOut(const winrt::Microsoft::UI::Composition::Compositor& compositor);

winrt::Windows::Foundation::TimeSpan Milliseconds(double ms);

}  // namespace ampify::win::effects

#endif  // AMPIFY_WITH_XAML
