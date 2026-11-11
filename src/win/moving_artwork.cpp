#include "moving_artwork.hpp"

#if defined(AMPIFY_WITH_XAML)

#include <winrt/Microsoft.UI.Xaml.Automation.Peers.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Hosting.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Numerics.h>

#include <chrono>
#include <cmath>

#include "../core/now_playing.hpp"
#include "xaml_chrome.hpp"

namespace ampify::win {

namespace {

namespace xaml = winrt::Microsoft::UI::Xaml;
namespace comp = winrt::Microsoft::UI::Composition;
using winrt::Windows::Foundation::Numerics::float2;
using winrt::Windows::Foundation::Numerics::float3;

// The cover decoded this small, then stretched over the window: its details melt into soft fields of colour.
constexpr float kWashPixels = 24;

// Each copy: its size against the window's diagonal, its opacity, and how long one turn takes (negative: the other
// way). Slow enough that the eye sees colours drift, not a picture turning.
struct LayerSpec {
    float scale;
    float opacity;
    double turnSeconds;
};
constexpr LayerSpec kLayers[] = {{1.6f, 1.0f, 120.0}, {1.15f, 0.55f, -90.0}};

constexpr auto kStep = std::chrono::milliseconds(100);

comp::ICompositionSurface Cover(const std::string& address) {
    if (address.empty()) return nullptr;
    return xaml::Media::LoadedImageSurface::StartLoadFromUri(winrt::Windows::Foundation::Uri(winrt::to_hstring(address)),
                                                             {kWashPixels, kWashPixels});
}

// Every copy centred on the window and large enough to cover it at any angle, turning around its own middle.
void Place(const xaml::Controls::Grid& host, const std::vector<comp::SpriteVisual>& layers) {
    const float width = static_cast<float>(host.ActualWidth());
    const float height = static_cast<float>(host.ActualHeight());
    const float diagonal = std::hypot(width, height);
    for (size_t i = 0; i < layers.size() && i < std::size(kLayers); i++) {
        const float side = diagonal * kLayers[i].scale;
        layers[i].Size({side, side});
        layers[i].CenterPoint({side / 2, side / 2, 0});
        layers[i].Offset({(width - side) / 2, (height - side) / 2, 0});
    }
}

}  // namespace

xaml::FrameworkElement MovingArtworks::Make(const wchar_t* name) {
    Wash wash;
    wash.host = xaml::Controls::Grid();
    wash.host.Name(name);
    wash.host.IsHitTestVisible(false);
    xaml::Automation::AutomationProperties::SetAccessibilityView(wash.host,
                                                                 xaml::Automation::Peers::AccessibilityView::Raw);
    comp::Compositor compositor = xaml::Hosting::ElementCompositionPreview::GetElementVisual(wash.host).Compositor();
    if (!compositor) return nullptr;
    wash.root = compositor.CreateContainerVisual();
    wash.brush = compositor.CreateSurfaceBrush();
    wash.brush.Stretch(comp::CompositionStretch::UniformToFill);
    wash.brush.BitmapInterpolationMode(comp::CompositionBitmapInterpolationMode::Linear);
    for (const LayerSpec& spec : kLayers) {
        comp::SpriteVisual layer = compositor.CreateSpriteVisual();
        layer.Brush(wash.brush);
        layer.Opacity(spec.opacity);
        wash.root.Children().InsertAtTop(layer);
        wash.layers.push_back(layer);
    }
    xaml::Hosting::ElementCompositionPreview::SetElementChildVisual(wash.host, wash.root);
    try {
        Show(wash);  // an address the cover cannot load from leaves the wash empty until the next song
    } catch (...) {
    }
    auto layers = wash.layers;
    wash.host.SizeChanged([layers](winrt::Windows::Foundation::IInspectable const& sender,
                                   xaml::SizeChangedEventArgs const&) noexcept {
        try {
            Place(sender.as<xaml::Controls::Grid>(), layers);
        } catch (...) {
            // A handler runs inside Apple Music's XAML frame: nothing may escape it.
        }
    });
    m_washes.push_back(wash);
    Turn();
    return wash.host;
}

void MovingArtworks::Show(Wash& wash) const {
    if (comp::ICompositionSurface cover = Cover(m_address)) wash.brush.Surface(cover);
}

// The drift: a few steps a second, not a compositor animation. At this speed a step moves the colours a few pixels, which
// the eye takes for a slow glide, while an animation redrew the window, and the glass blurring it, at every frame
// (about half a core, measured). The timer stops while no window is seen; a step does nothing while the music is
// paused or Windows' animation effects are off, so the wash stands still and nothing is drawn again.
void MovingArtworks::Step() {
    if (m_washes.empty() || !MotionAllowed() || !CurrentNowPlaying().playing) return;
    m_turned += std::chrono::duration<double>(kStep).count();
    for (const Wash& wash : m_washes) {
        for (size_t i = 0; i < wash.layers.size() && i < std::size(kLayers); i++) {
            const double turns = m_turned / kLayers[i].turnSeconds;
            wash.layers[i].RotationAngleInDegrees(static_cast<float>(std::fmod(turns, 1.0) * 360.0));
        }
    }
}

void MovingArtworks::Turn() {
    if (!m_timer.Wanted()) m_timer.Run(kStep, [this] { Step(); });
}

void MovingArtworks::Follow(const std::string& address) {
    if (address.empty() || address == m_address) return;  // between two songs the address may be missing: keep the last
    m_address = address;
    for (Wash& wash : m_washes) {
        try {
            Show(wash);
        } catch (...) {
        }
    }
}

void MovingArtworks::Release(const Wash& wash) noexcept {
    try {
        xaml::Hosting::ElementCompositionPreview::SetElementChildVisual(wash.host, nullptr);
    } catch (...) {
    }
}

void MovingArtworks::StopWhenEmpty() noexcept {
    if (m_washes.empty()) m_timer.Stop();
}

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
