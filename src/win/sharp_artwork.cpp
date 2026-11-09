#include "sharp_artwork.hpp"

#if defined(AMPIFY_WITH_XAML)

#include "win_common.hpp"
#include "larger_cover.hpp"

#include "../core/journal.hpp"
#include "../core/ui_watchdog.hpp"

#ifdef GetCurrentTime
#undef GetCurrentTime  // windows.h macro that breaks Storyboard::GetCurrentTime in the projection
#endif

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <string>
#include <vector>

namespace ampify::win {

namespace {

namespace xaml = winrt::Microsoft::UI::Xaml;
namespace controls = xaml::Controls;
namespace media = xaml::Media;
namespace imaging = xaml::Media::Imaging;
using winrt::Windows::Foundation::IInspectable;

constexpr wchar_t kArtworkName[] = L"ActualArtwork";  // Apple's ArtworkImage in the full-screen player
constexpr int kSearchLimit = 4000;                     // elements walked to find it
constexpr auto kFade = std::chrono::milliseconds(150);
constexpr double kResizeRatio = 0.1;  // a cover shown this much larger or smaller is judged again

std::atomic<bool> g_enabled{true};  // apple.artwork, set from the settings' thread

// Ampify's cover lies over Apple's, in the same place, and Apple's is never written to: its source is bound, and a
// value set on it cut the binding, which left the first song's cover on all the next ones. Interface thread
// only. Never freed: releasing WinRT objects while the process exits can crash it.
struct Sharp {
    Journal* journal = nullptr;
    winrt::weak_ref<controls::Image> image;  // Apple's own image: a weak reference resolves
    int64_t sourceToken = 0;
    int64_t visibilityToken = 0;
    int64_t opacityToken = 0;
    controls::Image overlay{nullptr};  // Ampify's, held strongly, just above Apple's in its panel
    bool ready = false;                // the overlay shows the sharper version of Apple's cover of now
    winrt::hstring appleAddress;       // the address of Apple's cover the overlay stands for
    double shown = 0;                  // the size Apple's cover was judged at, 0 before it was laid out
    imaging::BitmapImage apple{nullptr};  // Apple's cover while it opens: its size is known once it has
    winrt::event_token appleOpened{};
    imaging::BitmapImage loading{nullptr};
    winrt::event_token opened{};
    winrt::event_token failed{};
    unsigned serial = 0;                        // moves on at each change of Apple's cover: a late answer is dropped
    std::string lastNote;                       // why Apple's cover was kept, written once until it changes
};

Sharp& State() {
    static Sharp* state = new Sharp();
    return *state;
}

void Refresh(Sharp& state);

// Why the player keeps Apple's soft cover, for the log.
void Note(Sharp& state, const std::string& why) {
    if (!state.journal || why == state.lastNote) return;
    state.lastNote = why;
    state.journal->Write("Full-screen player: Apple's cover stays, " + why);
}

// The first element under `root`, breadth first, that `match` accepts.
template <typename Match>
xaml::DependencyObject FindUnder(const xaml::DependencyObject& root, Match match) {
    ampify::ActivityScope activity("sharp artwork tree search");
    std::vector<xaml::DependencyObject> queue{root};
    for (size_t i = 0; i < queue.size() && i < static_cast<size_t>(kSearchLimit); ++i) {
        if (i > 0 && match(queue[i])) return queue[i];
        int count = media::VisualTreeHelper::GetChildrenCount(queue[i]);
        for (int child = 0; child < count; ++child) queue.push_back(media::VisualTreeHelper::GetChild(queue[i], child));
    }
    return nullptr;
}

// Apple's ArtworkImage builds its Image only once a song plays: null before.
controls::Image ArtworkImage(const xaml::UIElement& root) {
    auto artwork = FindUnder(root, [](const xaml::DependencyObject& node) {
        auto element = node.try_as<xaml::FrameworkElement>();
        return element && element.Name() == kArtworkName;
    });
    if (!artwork) return nullptr;
    auto image = FindUnder(artwork, [](const xaml::DependencyObject& node) {
        auto found = node.try_as<controls::Image>();
        return found && found != State().overlay;
    });
    return image ? image.as<controls::Image>() : nullptr;
}

// The overlay follows Apple's image: hidden with it, faded with it, and empty until the sharper cover has opened.
void SyncOverlay(Sharp& state) {
    controls::Image image = state.image.get();
    if (!state.overlay || !image) return;
    state.overlay.Visibility(image.Visibility());
    state.overlay.Opacity(state.ready ? image.Opacity() : 0.0);
}

void DropApple(Sharp& state) {
    if (!state.apple) return;
    state.apple.ImageOpened(state.appleOpened);
    state.apple = nullptr;
}

void DropLoading(Sharp& state) {
    if (!state.loading) return;
    state.loading.ImageOpened(state.opened);
    state.loading.ImageFailed(state.failed);
    state.loading = nullptr;
}

void RemoveOverlay(Sharp& state) {
    if (!state.overlay) return;
    if (auto panel = media::VisualTreeHelper::GetParent(state.overlay).try_as<controls::Panel>()) {
        uint32_t index = 0;
        if (panel.Children().IndexOf(state.overlay, index)) panel.Children().RemoveAt(index);
    }
    state.overlay = nullptr;
    state.ready = false;
}

// Ampify's image laid out as Apple's: same cell, size, alignment and stretch, so the two covers fall on each other.
// Copied again each second while the player is open, after a change of mode or size.
void CopyLayout(const controls::Image& image, const controls::Image& overlay) {
    overlay.Stretch(image.Stretch());
    overlay.HorizontalAlignment(image.HorizontalAlignment());
    overlay.VerticalAlignment(image.VerticalAlignment());
    overlay.Margin(image.Margin());
    overlay.Width(image.Width());
    overlay.Height(image.Height());
    overlay.MaxWidth(image.MaxWidth());
    overlay.MaxHeight(image.MaxHeight());
    controls::Grid::SetRow(overlay, controls::Grid::GetRow(image));
    controls::Grid::SetColumn(overlay, controls::Grid::GetColumn(image));
    controls::Grid::SetRowSpan(overlay, controls::Grid::GetRowSpan(image));
    controls::Grid::SetColumnSpan(overlay, controls::Grid::GetColumnSpan(image));
}

controls::Image MakeOverlay(const controls::Image& image) {
    auto panel = media::VisualTreeHelper::GetParent(image).try_as<controls::Panel>();
    uint32_t index = 0;
    if (!panel || !panel.Children().IndexOf(image, index)) return nullptr;
    controls::Image overlay;
    overlay.IsHitTestVisible(false);
    overlay.Opacity(0);
    xaml::ScalarTransition fade;
    fade.Duration(kFade);
    overlay.OpacityTransition(fade);
    CopyLayout(image, overlay);
    panel.Children().InsertAt(index + 1, overlay);  // under whatever Apple draws over its image (outline, video)
    return overlay;
}

// The size Apple shows its cover at, in pixels.
double ShownPixels(const controls::Image& image) {
    double scale = image.XamlRoot() ? image.XamlRoot().RasterizationScale() : 1.0;
    return std::max(image.ActualWidth(), image.ActualHeight()) * scale;
}

// Loads `address` decoded at the size shown into the overlay, which shows it once it has opened.
void Load(Sharp& state, const std::string& address, double shown, int appleWidth) {
    controls::Image image = state.image.get();
    if (!image) return;
    if (!state.overlay) state.overlay = MakeOverlay(image);
    if (!state.overlay) return Note(state, "its image is not in a panel");
    state.lastNote.clear();
    imaging::BitmapImage large;
    large.DecodePixelWidth(static_cast<int>(std::lround(shown)));
    state.loading = large;
    state.opened = large.ImageOpened([appleWidth](IInspectable const& sender, xaml::RoutedEventArgs const&) noexcept {
        try {
            Sharp& state = State();
            auto bitmap = sender.as<imaging::BitmapImage>();
            if (state.loading != bitmap) return;
            DropLoading(state);
            state.ready = true;
            SyncOverlay(state);
            if (state.journal) {
                state.journal->Write("Full-screen player: a cover decoded at " + std::to_string(bitmap.DecodePixelWidth()) +
                                     " px from a " + std::to_string(bitmap.PixelWidth()) + " px image, over Apple's " +
                                     std::to_string(appleWidth) + " px one");
            }
        } catch (...) {
            // A handler runs inside Apple Music's XAML frame: nothing may escape it.
        }
    });
    state.failed = large.ImageFailed([](IInspectable const& sender, xaml::ExceptionRoutedEventArgs const&) noexcept {
        try {
            Sharp& state = State();
            if (state.loading != sender.as<imaging::BitmapImage>()) return;
            DropLoading(state);
            if (state.journal) state.journal->Write("Full-screen player: the sharper cover did not load, Apple's stays");
        } catch (...) {
            // A handler runs inside Apple Music's XAML frame: nothing may escape it.
        }
    });
    large.UriSource(winrt::Windows::Foundation::Uri(winrt::to_hstring(address)));
    state.overlay.Source(large);  // decoded while hidden: WinUI decodes what the tree holds, even at opacity 0
}

// Apple's cover changed. Shown larger than it is, it gets a sharper one over it, found by FindLargerCover.
void Refresh(Sharp& state) {
    controls::Image image = state.image.get();
    if (!image) return;
    auto bitmap = image.Source().try_as<imaging::BitmapImage>();
    auto uri = bitmap ? bitmap.UriSource() : nullptr;
    winrt::hstring address = uri ? uri.AbsoluteUri() : winrt::hstring();
    if (!address.empty() && address == state.appleAddress && (state.ready || state.loading)) return;  // same cover
    DropApple(state);
    DropLoading(state);
    state.ready = false;
    SyncOverlay(state);
    state.appleAddress = address;
    state.shown = ShownPixels(image);
    ++state.serial;
    state.lastNote.clear();  // a new cover: the same reason for it is worth a line again
    if (!bitmap) return Note(state, "its image is not a bitmap");
    int natural = bitmap.PixelWidth();
    if (natural <= 0) {
        state.appleAddress = winrt::hstring();  // still opening, or given its address after: looked at once it opened
        state.apple = bitmap;
        state.appleOpened = bitmap.ImageOpened([](IInspectable const&, xaml::RoutedEventArgs const&) noexcept {
            try {
                Refresh(State());
            } catch (...) {
                // A handler runs inside Apple Music's XAML frame: nothing may escape it.
            }
        });
        return;
    }
    double shown = state.shown;
    if (shown <= 0) return;  // not laid out yet: judged at the next look, within a second
    // Without a decode size of Apple's, WinUI decodes the image at the size it is first laid out, which is smaller when
    // the player opens (its window grows from Apple's mini player or the page): the cover stayed soft at full size on
    // some openings only. Such a cover is never judged sharp by its file: the same image is decoded again at the size
    // shown (FindLargerCover answers with its own address).
    const bool autoDecoded = bitmap.DecodePixelWidth() <= 0;
    int decoded = autoDecoded ? natural : std::min(bitmap.DecodePixelWidth(), natural);
    if (!autoDecoded && decoded >= shown * kSoftCoverRatio) {
        // Said once: on a 1920x1080 screen at 100 %, Apple's cover is large enough and the player never changes it.
        return Note(state, "sharp enough (" + std::to_string(decoded) + " px shown at " +
                               std::to_string(std::lround(shown)) + " px)");
    }
    FindLargerCover(bitmap, shown, [serial = state.serial, shown, decoded](const std::string& larger,
                                                                           const std::string& why) {
        Sharp& state = State();
        if (state.serial != serial) return;  // Apple's cover changed meanwhile
        if (larger.empty()) return Note(state, why);
        Load(state, larger, shown, decoded);
    });
}

void Unfollow(Sharp& state) {
    if (controls::Image old = state.image.get()) {
        old.UnregisterPropertyChangedCallback(controls::Image::SourceProperty(), state.sourceToken);
        old.UnregisterPropertyChangedCallback(xaml::UIElement::VisibilityProperty(), state.visibilityToken);
        old.UnregisterPropertyChangedCallback(xaml::UIElement::OpacityProperty(), state.opacityToken);
    }
    DropApple(state);
    DropLoading(state);
    RemoveOverlay(state);
    state.appleAddress = winrt::hstring();
    state.image = nullptr;
}

}  // namespace

void SharpenPlayerArtwork(const xaml::UIElement& root, Journal& journal) noexcept {
    try {
        Sharp& state = State();
        state.journal = &journal;
        if (!g_enabled) {  // turned off: Apple's cover shows again, and a late answer is dropped
            if (state.image.get() || state.overlay) {
                ++state.serial;
                Unfollow(state);
            }
            return;
        }
        if (!root) return;
        controls::Image followed = state.image.get();
        if (followed && followed.XamlRoot() && followed.XamlRoot() == root.XamlRoot()) {
            if (state.overlay) CopyLayout(followed, state.overlay);
            double now = ShownPixels(followed);
            if (now > 0 && std::abs(now - state.shown) > state.shown * kResizeRatio) {
                state.appleAddress = winrt::hstring();  // laid out since, or shown larger: judged again
                Refresh(state);
            }
            return;
        }
        controls::Image image = ArtworkImage(root);
        if (!image || image == followed) return;
        Unfollow(state);
        state.image = winrt::make_weak(image);
        auto refresh = [](xaml::DependencyObject const&, xaml::DependencyProperty const&) noexcept {
            try {
                Refresh(State());
            } catch (...) {
                // A handler runs inside Apple Music's XAML frame: nothing may escape it.
            }
        };
        auto sync = [](xaml::DependencyObject const&, xaml::DependencyProperty const&) noexcept {
            try {
                SyncOverlay(State());
            } catch (...) {
                // A handler runs inside Apple Music's XAML frame: nothing may escape it.
            }
        };
        state.sourceToken = image.RegisterPropertyChangedCallback(controls::Image::SourceProperty(), refresh);
        state.visibilityToken = image.RegisterPropertyChangedCallback(xaml::UIElement::VisibilityProperty(), sync);
        state.opacityToken = image.RegisterPropertyChangedCallback(xaml::UIElement::OpacityProperty(), sync);
        Refresh(state);
    } catch (...) {
        // Nothing may escape into Apple Music's XAML frame.
    }
}

void SetSharpArtwork(bool enabled) noexcept {
    g_enabled = enabled;
}

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
