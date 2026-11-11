// The song's artwork as a theme paints it: `background: artwork`, `content: artwork`, and its colours.

#if defined(AMPIFY_WITH_XAML)

#include "xaml_backend.hpp"

#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <optional>
#include <string_view>
#include <utility>
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <set>

#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Automation.Peers.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Microsoft.UI.Xaml.Media.Animation.h>
#include <winrt/Microsoft.UI.Xaml.Navigation.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.UI.h>
#include <winrt/Windows.UI.Text.h>
#include <winrt/Windows.Foundation.Numerics.h>

#include "../core/color_extract.hpp"
#include "../core/theme/colour_keys.hpp"
#include "../core/theme/properties.hpp"
#include "../core/theme/selector.hpp"
#include "../core/text_util.hpp"
#include "../core/ui_render/render_tree.hpp"
#include "sidebar_hover.hpp"
#include "xaml_host.hpp"
#include "xaml_backend_detail.hpp"

namespace ampify::win {

using namespace xaml_detail;

media::Brush XamlTreeBackend::ArtworkBrush(const std::string& text) {
    media::ImageBrush brush;
    brush.Stretch(StretchOf(theme::DecodeImage(text).fit, media::Stretch::UniformToFill));
    if (m_artwork) brush.ImageSource(m_artwork);
    m_artworkLayers.push_back(ArtworkLayer{brush, nullptr, {}});
    return brush;
}

// `content: artwork`: a Border painted by an artwork brush, with the layer that fades the previous cover out on top.
controls::Border XamlTreeBackend::ArtworkDecor(const std::string& text, const wchar_t* name) {
    controls::Border artwork;
    artwork.Name(name);
    artwork.IsHitTestVisible(false);
    xaml::Automation::AutomationProperties::SetAccessibilityView(artwork, xaml::Automation::Peers::AccessibilityView::Raw);
    auto brush = ArtworkBrush(text).as<media::ImageBrush>();
    ArtworkLayer& layer = m_artworkLayers.back();
    layer.owner = artwork;
    layer.fade = MakeArtworkFade(brush.Stretch());
    artwork.Background(brush);
    artwork.Child(layer.fade.layer);
    return artwork;
}

bool XamlTreeBackend::DecorHolds(const xaml::FrameworkElement& owner) const {
    for (const auto& [key, decor] : m_decors) {
        if (decor.image && ComIdentity(decor.image) == ComIdentity(owner)) return true;
    }
    return false;
}

// Gives the artwork brushes the cover of the song playing, decoded large (LargeArtwork: Apple's `image` is a
// thumbnail), and lets go of those nothing holds any more (the theme was removed, the decor replaced). Strong
// references: weak ones to what Ampify made never resolve in Apple Music.
void XamlTreeBackend::FollowArtwork(const controls::Image& image, const media::ImageSource& artwork) {
    // The moving washes (content: immersion) take the play bar's cover too.
    m_immersion.Sweep([this](const xaml::FrameworkElement& lockup) { return DecorHolds(lockup); });
    if (auto bitmap = artwork ? artwork.try_as<media::Imaging::BitmapImage>() : nullptr) {
        if (auto uri = bitmap.UriSource()) m_immersion.Follow(Utf8(uri.AbsoluteUri()));
    }
    std::erase_if(m_artworkLayers, [&](const ArtworkLayer& layer) {
        if (layer.owner) return !DecorHolds(layer.owner);
        for (const auto& [key, written] : m_written) {
            if (ComIdentity(written.object) == ComIdentity(layer.brush)) return false;
        }
        return true;
    });
    if (m_artworkLayers.empty()) {
        m_largeArtwork.Drop();
        if (artwork) m_artwork = artwork;  // a brush made later starts from it
        return;
    }
    std::weak_ptr<bool> alive = m_alive;
    media::ImageSource paint = m_largeArtwork.For(image, artwork, [this, alive] {
        if (!alive.expired()) OnArtworkChanged();
    });
    // Null while the large cover opens, or between two songs: the brushes keep the last cover, or show Apple's.
    if (!paint && !m_artwork) paint = artwork;
    if (paint) m_artwork = paint;
    for (ArtworkLayer& layer : m_artworkLayers) {
        media::ImageSource shown = layer.brush.ImageSource();
        if (!m_artwork || shown == m_artwork) continue;
        if (layer.owner) FadeFromPrevious(layer.fade, shown, layer.owner.CornerRadius());
        layer.brush.ImageSource(m_artwork);
    }
}

// Reads the colours of an artwork once per image: rendered small, then median cut (color_extract.hpp). Each prefix
// ("" for the play bar, kPageArtworkPrefix for the page on screen) holds its own variables and key. A blank or failed
// reading leaves the key unset, so the next pass tries again.
void XamlTreeBackend::SampleArtwork(const controls::Image& image, const std::string& prefix) {
    constexpr int kSampleSize = 48;
    media::ImageSource source = image.Source();
    if (!source || image.ActualWidth() < 1) return;
    std::string key;
    if (auto bitmap = source.try_as<media::Imaging::BitmapImage>()) {
        if (bitmap.PixelWidth() == 0) return;  // not decoded yet
        if (auto uri = bitmap.UriSource()) key = Utf8(uri.AbsoluteUri());
    }
    if (key.empty()) key = std::to_string(reinterpret_cast<std::uintptr_t>(winrt::get_abi(source)));
    std::shared_ptr<ArtworkColours> shared = m_artworkColours;
    bool later = false;
    {
        std::lock_guard<std::mutex> guard(shared->lock);
        if (shared->keys[prefix] == key) return;
        later = !shared->busy.insert(prefix).second;
    }
    if (later && !prefix.empty()) return;  // the page's artwork waits for the next pass
    if (later) {
        // The reading of the previous image is still under way: this one is read right after it.
        constexpr auto kRetry = std::chrono::milliseconds(120);
        try {
            if (!m_artworkRetry) {
                m_artworkRetry = image.DispatcherQueue().CreateTimer();
                m_artworkRetry.IsRepeating(false);
                std::weak_ptr<bool> alive = m_alive;
                m_artworkRetry.Tick([this, alive](auto const&, auto const&) noexcept {
                    if (!alive.expired()) OnArtworkChanged();
                });
            }
            m_artworkRetry.Interval(kRetry);
            m_artworkRetry.Start();
        } catch (...) {
        }
        return;
    }
    auto done = [shared, prefix](std::optional<std::map<std::string, std::string>> variables, const std::string& from) {
        {
            std::lock_guard<std::mutex> guard(shared->lock);
            shared->busy.erase(prefix);
            if (!variables) return;
            const std::string own = prefix + "art-";
            std::erase_if(shared->variables, [&](const auto& entry) { return entry.first.starts_with(own); });
            for (auto& [name, value] : *variables) shared->variables[prefix + name] = std::move(value);
            shared->keys[prefix] = from;
        }
        shared->changed.notify_all();
    };
    try {
        media::Imaging::RenderTargetBitmap target;
        using winrt::Windows::Foundation::AsyncStatus;
        target.RenderAsync(image, kSampleSize, kSampleSize)
            .Completed([target, key, done](auto const&, AsyncStatus status) {
                if (status != AsyncStatus::Completed) return done(std::nullopt, key);
                target.GetPixelsAsync().Completed([target, key, done](auto const& operation, AsyncStatus read) {
                    try {
                        if (read != AsyncStatus::Completed) return done(std::nullopt, key);
                        auto buffer = operation.GetResults();
                        size_t width = static_cast<size_t>(target.PixelWidth());
                        size_t height = static_cast<size_t>(target.PixelHeight());
                        if (buffer.Length() != width * height * 4) return done(std::nullopt, key);
                        // BGRA with premultiplied alpha, to the straight RGBA the extractor reads.
                        std::vector<uint8_t> rgba(buffer.Length());
                        const uint8_t* bgra = buffer.data();
                        for (size_t i = 0; i < rgba.size(); i += 4) {
                            uint8_t alpha = bgra[i + 3];
                            auto straight = [alpha](uint8_t channel) {
                                return alpha ? static_cast<uint8_t>(std::min(255, channel * 255 / alpha)) : uint8_t{0};
                            };
                            rgba[i] = straight(bgra[i + 2]);
                            rgba[i + 1] = straight(bgra[i + 1]);
                            rgba[i + 2] = straight(bgra[i]);
                            rgba[i + 3] = alpha;
                        }
                        std::optional<ExtractedColors> colors = ExtractColors(rgba.data(), rgba.size(), width, height);
                        if (!colors) return done(std::nullopt, key);
                        done(ArtworkVariables(*colors), key);
                    } catch (...) {
                        done(std::nullopt, key);
                    }
                });
            });
    } catch (...) {
        done(std::nullopt, key);
    }
}

// Follows the play bar's artwork between passes: ImageOpened for an image that loads, and Source for one Apple Music
// had already loaded (a cached artwork opens nothing). Apple's own image, so a weak reference resolves. The handlers
// read the artwork at once, not through SchedulePass: a song's colours follow it within a quarter second, and
// OnArtworkChanged lets nothing escape into Apple Music's XAML.
void XamlTreeBackend::FollowArtworkImage(const controls::Image& image) {
    controls::Image followed = m_artworkImage.get();
    if (followed == image) return;
    try {
        if (followed) {
            followed.ImageOpened(m_artworkOpened);
            followed.UnregisterPropertyChangedCallback(controls::Image::SourceProperty(), m_artworkSourceToken);
        }
        std::weak_ptr<bool> alive = m_alive;
        m_artworkOpened = image.ImageOpened([this, alive](IInspectable const&, xaml::RoutedEventArgs const&) noexcept {
            if (!alive.expired()) OnArtworkChanged();
        });
        m_artworkSourceToken = image.RegisterPropertyChangedCallback(
            controls::Image::SourceProperty(),
            [this, alive](xaml::DependencyObject const&, xaml::DependencyProperty const&) noexcept {
                if (!alive.expired()) OnArtworkChanged();
            });
        m_artworkImage = winrt::make_weak(image);
    } catch (...) {
        m_artworkImage = nullptr;  // the next pass tries again
    }
}

void XamlTreeBackend::OnArtworkChanged() noexcept {
    try {
        if (*m_closed) return;
        controls::Image image = m_artworkImage.get();
        if (!image) return;
        FollowArtwork(image, image.Source());
        SampleArtwork(image);
    } catch (...) {
    }
}

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
