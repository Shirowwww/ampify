#include "artwork_fade.hpp"

#if defined(AMPIFY_WITH_XAML)

#include <winrt/Microsoft.UI.Xaml.Automation.Peers.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>

#include <chrono>

#include "../core/theme/glass.hpp"
#include "xaml_chrome.hpp"

namespace ampify::win {

namespace xaml = winrt::Microsoft::UI::Xaml;
namespace media = xaml::Media;
namespace animation = media::Animation;

namespace {

animation::Storyboard FadeOut(const xaml::Controls::Border& layer, int ms) {
    animation::DoubleAnimation fade;
    fade.From(1.0);
    fade.To(0.0);
    fade.Duration(xaml::DurationHelper::FromTimeSpan(
        std::chrono::duration_cast<winrt::Windows::Foundation::TimeSpan>(std::chrono::milliseconds(ms))));
    animation::Storyboard::SetTarget(fade, layer);
    animation::Storyboard::SetTargetProperty(fade, L"Opacity");
    animation::Storyboard story;
    story.Children().Append(fade);
    return story;
}

}  // namespace

ArtworkFade MakeArtworkFade(media::Stretch stretch) {
    ArtworkFade fade;
    fade.brush = media::ImageBrush();
    fade.brush.Stretch(stretch);
    fade.layer = xaml::Controls::Border();
    fade.layer.Background(fade.brush);
    fade.layer.Opacity(0);
    fade.layer.IsHitTestVisible(false);
    xaml::Automation::AutomationProperties::SetAccessibilityView(fade.layer, xaml::Automation::Peers::AccessibilityView::Raw);
    return fade;
}

void FadeFromPrevious(ArtworkFade& fade, const media::ImageSource& previous, xaml::CornerRadius corner) {
    if (!fade.layer) return;
    if (fade.story) fade.story.Stop();
    const int ms = theme::ArtworkFadeMs(theme::CurrentGlassLevel(), MotionAllowed());
    if (!previous || ms == 0) {
        fade.brush.ClearValue(media::ImageBrush::ImageSourceProperty());
        return;
    }
    fade.brush.ImageSource(previous);
    if (fade.layer.CornerRadius() != corner) fade.layer.CornerRadius(corner);
    // The local value stays 0, so the layer is hidden again once the storyboard stops, whatever stops it.
    fade.story = FadeOut(fade.layer, ms);
    fade.story.Begin();
}

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
