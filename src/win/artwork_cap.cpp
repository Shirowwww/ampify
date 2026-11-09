#include "artwork_cap.hpp"

#if defined(AMPIFY_WITH_XAML)

#include <winrt/Windows.Foundation.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>

#include <algorithm>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <inspectable.h>

#include "../core/amp_abi.hpp"
#include "../core/player_model.hpp"
#include "../core/theme/glyph_rows.hpp"
#include "winrt_abi.hpp"
#include "xaml_chrome.hpp"

namespace ampify::win {

namespace {

namespace th = ampify::theme;
namespace xaml = winrt::Microsoft::UI::Xaml;
using winrt::Windows::Foundation::Size;


struct Original {
    Size cap{};
    double maxWidth = 0;   // the element's own MaxWidth and MaxHeight: Apple also sets MaxHeight to 270
    double maxHeight = 0;
    Size target{};     // the lifted cap Ampify gave it
    int restored = 0;  // how many times Apple put its own cap back since (KeepArtworkCaps)
    winrt::hstring square;  // the address of the square picture Ampify gave its image (SquareCover)
    int squared = 0;        // how many times it did, at most kMaxCapRestores
};

// Apple putting its cap back more often than this is a fight with its own code: Ampify then leaves it.
constexpr int kMaxCapRestores = 3;

rt::Ref ArtworkOf(const xaml::FrameworkElement& element) {
    rt::Ref unknown(winrt::detach_abi(element.as<winrt::Windows::Foundation::IUnknown>()));
    rt::Ref artwork;
    if (!rt::Query(unknown, rt::GuidOf(amp::kIidArtworkImage), artwork)) return {};
    return artwork;
}

// Apple's caps of the artworks whose cap is lifted. Interface thread only; never destroyed (xaml-lessons.md).
ElementMap<xaml::FrameworkElement, Original>& Originals() {
    static auto* originals = new ElementMap<xaml::FrameworkElement, Original>();
    return *originals;
}

// The element's own bounds follow the cap: lifted with it, Apple's again once it is back.
void FollowBounds(const xaml::FrameworkElement& element, const Original& original, Size target, bool lifted) {
    double width = lifted ? th::LiftedBound(original.maxWidth, target.Width) : original.maxWidth;
    double height = lifted ? th::LiftedBound(original.maxHeight, target.Height) : original.maxHeight;
    if (element.MaxWidth() != width) element.MaxWidth(width);
    if (element.MaxHeight() != height) element.MaxHeight(height);
}

// Apple picks the size of the picture it loads from the cap when its artwork is set: a cap lifted later left the
// 270-pixel picture stretched, softer than Apple's own (Large album artwork, 4bis.7). The same artwork set again is
// loaded at the new cap.
void LoadAgainAtCap(const rt::Ref& artwork) {
    void* value = nullptr;
    if (FAILED(rt::CallSlot(artwork, amp::kArtworkImageGetArtwork, &value)) || !value) return;
    winrt::com_ptr<::IUnknown> held;
    held.attach(static_cast<::IUnknown*>(value));
    if (SUCCEEDED(rt::CallSlot(artwork, amp::kArtworkImagePutArtwork, static_cast<void*>(nullptr)))) {
        rt::CallSlot(artwork, amp::kArtworkImagePutArtwork, static_cast<void*>(held.get()));
    }
}

}  // namespace

void FollowArtworkCap(const xaml::FrameworkElement& element) noexcept {
    try {
        rt::Ref artwork = ArtworkOf(element);
        if (!artwork.Get()) return;
        Size current{};
        if (FAILED(rt::CallSlot(artwork.Get(), amp::kArtworkImageGetMaxSize, &current))) return;

        auto& originals = Originals();
        originals.Sweep();
        Original* found = originals.Find(element);
        Original apple = found ? *found : Original{current, element.MaxWidth(), element.MaxHeight()};
        th::BoxSize wanted = th::LiftedMaxSize({apple.cap.Width, apple.cap.Height}, element.Width(), element.Height());
        Size target{static_cast<float>(wanted.width), static_cast<float>(wanted.height)};
        bool lifted = target.Width != apple.cap.Width || target.Height != apple.cap.Height;
        bool known = found != nullptr;
        if (lifted) originals.Add(element, apple).target = target;
        if (!lifted && known) originals.Erase(element);
        if (lifted || known) FollowBounds(element, apple, target, lifted);
        if (target.Width == current.Width && target.Height == current.Height) return;
        if (FAILED(rt::CallSlot(artwork.Get(), amp::kArtworkImagePutMaxSize, target))) return;
        element.InvalidateMeasure();
        if (target.Width > current.Width || target.Height > current.Height) LoadAgainAtCap(artwork);
    } catch (...) {
        // Nothing may escape into Apple Music's XAML frame.
    }
}

namespace {

// A picture this much smaller than shown looks soft (larger_cover.hpp's kSoftCoverRatio).
constexpr double kSoftPicture = 0.9;

// The picture of an artwork: its Image#StaticArtwork, a few levels down.
xaml::Controls::Image PictureOf(const xaml::FrameworkElement& artwork) {
    namespace tree = xaml::Media;
    std::vector<xaml::DependencyObject> queue{artwork};
    constexpr size_t kLevels = 64;  // ArtworkImage > AspectRatioGrid > Image: a handful of elements
    for (size_t i = 0; i < queue.size() && queue.size() < kLevels; i++) {
        auto image = queue[i].try_as<xaml::Controls::Image>();
        if (image && image.Name() == L"StaticArtwork") return image;
        int count = tree::VisualTreeHelper::GetChildrenCount(queue[i]);
        for (int child = 0; child < count; child++) queue.push_back(tree::VisualTreeHelper::GetChild(queue[i], child));
    }
    return nullptr;
}

// Apple asks its catalog picture at its cap's width and the artwork's height: an editorial playlist's cover lifted to
// 360 came 270 by 360, a portrait padded with white and drawn small between bands. A lifted artwork whose picture is
// not square, or smaller than shown, gets the square one at the size shown, three times at most for one element.
void SquareCover(const xaml::FrameworkElement& element, Original& original) {
    xaml::Controls::Image image = PictureOf(element);
    auto bitmap = image ? image.Source().try_as<xaml::Media::Imaging::BitmapImage>() : nullptr;
    auto uri = bitmap ? bitmap.UriSource() : nullptr;
    if (!uri || uri.AbsoluteUri() == original.square) return;
    const std::string address = winrt::to_string(uri.AbsoluteUri());
    std::optional<CoverSize> asked = CatalogCoverSize(address);
    if (!asked) return;
    const double scale = element.XamlRoot() ? element.XamlRoot().RasterizationScale() : 1.0;
    const double shown = std::max(element.ActualWidth(), element.ActualHeight()) * scale;
    if (asked->width == asked->height && asked->width >= shown * kSoftPicture) return;
    const std::string square = CatalogCoverAddress(address, shown);
    if (square.empty() || original.squared >= kMaxCapRestores) return;
    original.squared++;
    original.square = winrt::to_hstring(square);
    xaml::Media::Imaging::BitmapImage larger;
    larger.UriSource(winrt::Windows::Foundation::Uri(original.square));
    image.Source(larger);
}

}  // namespace

void KeepArtworkCaps() noexcept {
    try {
        auto& originals = Originals();
        originals.Sweep();
        originals.ForEach([](const xaml::FrameworkElement& element, Original& original) {
            SquareCover(element, original);
            rt::Ref artwork = ArtworkOf(element);
            Size current{};
            if (!artwork.Get() || FAILED(rt::CallSlot(artwork.Get(), amp::kArtworkImageGetMaxSize, &current))) return;
            const Size target = original.target;
            if (current.Width >= target.Width && current.Height >= target.Height) return;
            if (original.restored >= kMaxCapRestores) return;
            original.restored++;
            if (FAILED(rt::CallSlot(artwork.Get(), amp::kArtworkImagePutMaxSize, target))) return;
            FollowBounds(element, original, target, true);
            element.InvalidateMeasure();
            LoadAgainAtCap(artwork);
        });
    } catch (...) {
        // Nothing may escape into Apple Music's XAML frame.
    }
}

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
