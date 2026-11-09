#include "ui_motion.hpp"

#if defined(AMPIFY_WITH_XAML)

#include "win_common.hpp"
#include "xaml_chrome.hpp"

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Numerics.h>
#include <winrt/Microsoft.UI.Composition.h>
#include <winrt/Microsoft.UI.Xaml.Hosting.h>

namespace ampify::win::motion {

namespace xaml = winrt::Microsoft::UI::Xaml;
namespace comp = winrt::Microsoft::UI::Composition;
using winrt::Windows::Foundation::Numerics::float2;
using winrt::Windows::Foundation::Numerics::float3;

namespace {

// Quick out, soft landing: the curve of every move on Ampify's surfaces.
comp::CubicBezierEasingFunction Easing(const comp::Compositor& compositor) {
    return compositor.CreateCubicBezierEasingFunction(float2{0.2f, 0.8f}, float2{0.2f, 1.0f});
}

}  // namespace

void Enter(const xaml::UIElement& element, float rise, std::chrono::milliseconds duration) noexcept {
    try {
        if (!element) return;
        comp::Visual visual = xaml::Hosting::ElementCompositionPreview::GetElementVisual(element);
        xaml::Hosting::ElementCompositionPreview::SetIsTranslationEnabled(element, true);
        if (!MotionAllowed()) {
            visual.Opacity(1);
            visual.Properties().InsertVector3(L"Translation", float3{0, 0, 0});
            return;
        }
        comp::Compositor compositor = visual.Compositor();
        comp::CubicBezierEasingFunction ease = Easing(compositor);
        comp::ScalarKeyFrameAnimation fade = compositor.CreateScalarKeyFrameAnimation();
        fade.InsertKeyFrame(0, 0);
        fade.InsertKeyFrame(1, 1, ease);
        fade.Duration(duration);
        comp::Vector3KeyFrameAnimation lift = compositor.CreateVector3KeyFrameAnimation();
        lift.InsertKeyFrame(0, float3{0, rise, 0});
        lift.InsertKeyFrame(1, float3{0, 0, 0}, ease);
        lift.Duration(duration);
        visual.StartAnimation(L"Opacity", fade);
        visual.StartAnimation(L"Translation", lift);
    } catch (...) {
        // Called from handlers inside Apple Music's XAML frame: nothing may escape.
    }
}

void Glide(const xaml::UIElement& element, float x, float scaleX, bool animate,
           std::chrono::milliseconds duration) noexcept {
    try {
        if (!element) return;
        if (!animate || !MotionAllowed()) {
            element.TranslationTransition(nullptr);
            element.ScaleTransition(nullptr);
        } else if (!element.TranslationTransition()) {
            xaml::Vector3Transition move;
            move.Duration(duration);
            element.TranslationTransition(move);
            xaml::Vector3Transition stretch;
            stretch.Duration(duration);
            element.ScaleTransition(stretch);
        }
        element.Translation(float3{x, 0, 0});
        element.Scale(float3{scaleX, 1, 1});
    } catch (...) {
        // Called from handlers inside Apple Music's XAML frame: nothing may escape.
    }
}

void FadeTo(const xaml::UIElement& element, double opacity, std::chrono::milliseconds duration) noexcept {
    try {
        if (!element) return;
        if (!MotionAllowed()) {
            element.OpacityTransition(nullptr);
        } else if (!element.OpacityTransition()) {
            xaml::ScalarTransition fade;
            fade.Duration(duration);
            element.OpacityTransition(fade);
        }
        element.Opacity(opacity);
    } catch (...) {
        // Called from handlers inside Apple Music's XAML frame: nothing may escape.
    }
}

}  // namespace ampify::win::motion

#endif  // AMPIFY_WITH_XAML
