#include "artwork_large.hpp"

#if defined(AMPIFY_WITH_XAML)

#include <winrt/Microsoft.UI.Xaml.Automation.Peers.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>

#include <chrono>
#include <utility>

#include "larger_cover.hpp"

namespace ampify::win {

namespace xaml = winrt::Microsoft::UI::Xaml;
namespace controls = xaml::Controls;
namespace media = xaml::Media;
namespace imaging = media::Imaging;
using winrt::Windows::Foundation::IInspectable;

namespace {

// Apple's library covers are 600 to 1200 px wide, its catalog ones larger; the window's background is blurred or
// faded, so more would only cost memory (a 1200 px cover holds about 6 MB).
constexpr double kLargePixels = 1200;
// A cover that has not opened by then gives way to Apple's thumbnail; it still replaces it if it opens later.
constexpr auto kPatience = std::chrono::milliseconds(1500);

}  // namespace

struct LargeArtwork::State {
    media::ImageSource apple{nullptr};       // Apple's source the large cover stands for
    std::string address;                     // where that large cover comes from
    media::ImageSource large{nullptr};       // opened, decoded large
    imaging::BitmapImage loading{nullptr};   // still opening
    winrt::event_token opened{};
    winrt::event_token failed{};
    bool refused = false;                    // no large cover for this source: Apple's is painted
    bool waiting = false;                    // asked, not yet opened or refused
    bool answering = false;                  // inside For: an answer found at once needs no callback
    std::chrono::steady_clock::time_point asked{};
    controls::Image loader{nullptr};         // hidden, beside Apple's image, so that WinUI decodes the large cover
    std::function<void()> onOpened;
    int applePixels = 0;
    std::string note;
};

namespace {

using State = LargeArtwork::State;

void DropLoading(State& state) {
    if (!state.loading) return;
    state.loading.ImageOpened(state.opened);
    state.loading.ImageFailed(state.failed);
    state.loading = nullptr;
    if (state.loader) state.loader.ClearValue(controls::Image::SourceProperty());
}

void RemoveLoader(State& state) {
    if (!state.loader) return;
    if (auto panel = media::VisualTreeHelper::GetParent(state.loader).try_as<controls::Panel>()) {
        uint32_t index = 0;
        if (panel.Children().IndexOf(state.loader, index)) panel.Children().RemoveAt(index);
    }
}

// The hidden image sits in the Grid that holds Apple's: 1 pixel, transparent, in the top left corner of its cell, so
// that it changes no layout. Nothing for another kind of panel, where it would take room.
bool PlaceLoader(State& state, const controls::Image& apple) {
    auto grid = media::VisualTreeHelper::GetParent(apple).try_as<controls::Grid>();
    if (!grid) return false;
    if (state.loader && media::VisualTreeHelper::GetParent(state.loader) == grid) return true;
    RemoveLoader(state);
    if (!state.loader) {
        state.loader = controls::Image();
        state.loader.Name(winrt::to_hstring(kLargeArtworkLoaderName));
        state.loader.Width(1);
        state.loader.Height(1);
        state.loader.Opacity(0);
        state.loader.IsHitTestVisible(false);
        state.loader.HorizontalAlignment(xaml::HorizontalAlignment::Left);
        state.loader.VerticalAlignment(xaml::VerticalAlignment::Top);
        xaml::Automation::AutomationProperties::SetAccessibilityView(state.loader,
                                                                     xaml::Automation::Peers::AccessibilityView::Raw);
    }
    controls::Grid::SetRow(state.loader, controls::Grid::GetRow(apple));
    controls::Grid::SetColumn(state.loader, controls::Grid::GetColumn(apple));
    uint32_t index = 0;
    if (!grid.Children().IndexOf(apple, index)) return false;
    grid.Children().InsertAt(index + 1, state.loader);
    return true;
}

void Load(const std::shared_ptr<State>& shared, const std::string& address) {
    State& state = *shared;
    imaging::BitmapImage bitmap;
    bitmap.DecodePixelWidth(static_cast<int>(kLargePixels));  // a 3000 px catalog cover is not decoded whole
    state.loading = bitmap;
    state.asked = std::chrono::steady_clock::now();
    std::weak_ptr<State> weak = shared;
    state.opened = bitmap.ImageOpened([weak](IInspectable const& sender, xaml::RoutedEventArgs const&) noexcept {
        try {
            std::shared_ptr<State> alive = weak.lock();
            if (!alive || alive->loading != sender.as<imaging::BitmapImage>()) return;
            auto opened = sender.as<imaging::BitmapImage>();
            alive->large = opened;
            alive->waiting = false;
            alive->note = "Artwork: the theme's cover decoded at " + std::to_string(opened.DecodePixelWidth()) +
                          " px from a " + std::to_string(opened.PixelWidth()) + " px image, over the play bar's " +
                          std::to_string(alive->applePixels) + " px one";
            // A copy: the call sets a new one. The brushes take the cover while the loader still holds it.
            if (auto callback = alive->onOpened) callback();
            DropLoading(*alive);
        } catch (...) {
            // A handler runs inside Apple Music's XAML frame: nothing may escape it.
        }
    });
    state.failed = bitmap.ImageFailed([weak](IInspectable const& sender,
                                             xaml::ExceptionRoutedEventArgs const&) noexcept {
        try {
            std::shared_ptr<State> alive = weak.lock();
            if (!alive || alive->loading != sender.as<imaging::BitmapImage>()) return;
            DropLoading(*alive);
            alive->refused = true;
            alive->waiting = false;
            alive->note = "Artwork: the theme's large cover did not load, the play bar's stays";
            if (auto callback = alive->onOpened) callback();
        } catch (...) {
            // A handler runs inside Apple Music's XAML frame: nothing may escape it.
        }
    });
    bitmap.UriSource(winrt::Windows::Foundation::Uri(winrt::to_hstring(address)));
    state.loader.Source(bitmap);
}

// Apple's cover found larger (FindLargerCover): loaded beside Apple's image, or Apple's kept, with the reason logged.
void Found(const std::shared_ptr<State>& shared, const controls::Image& apple, const std::string& address,
           const std::string& why) {
    State& state = *shared;
    // Apple sets its image again with the same file (two songs of one album, a redraw of its bar): same large cover.
    if (!address.empty() && address == state.address && state.large) {
        state.waiting = false;
    } else {
        DropLoading(state);
        state.large = nullptr;
        state.address = address;
        if (!address.empty() && apple && PlaceLoader(state, apple)) return Load(shared, address);
        state.waiting = false;
        state.refused = true;
        const std::string reason = why.empty() ? std::string("no panel to load it in") : why;
        state.note = "Artwork: the play bar's cover stays, " + reason;
    }
    if (auto callback = state.onOpened; callback && !state.answering) callback();
}

}  // namespace

LargeArtwork::LargeArtwork() : m_state(std::make_shared<State>()) {}

media::ImageSource LargeArtwork::For(const controls::Image& apple, const media::ImageSource& source,
                                     std::function<void()> opened) {
    State& state = *m_state;
    if (!source) return nullptr;
    state.onOpened = std::move(opened);
    if (source != state.apple) {
        auto bitmap = source.try_as<imaging::BitmapImage>();
        if (!bitmap) return source;
        state.apple = source;
        state.refused = false;
        state.waiting = true;
        state.asked = std::chrono::steady_clock::now();
        state.applePixels = bitmap.PixelWidth();
        std::weak_ptr<State> weak = m_state;
        winrt::weak_ref<controls::Image> image = apple ? winrt::make_weak(apple) : winrt::weak_ref<controls::Image>{};
        state.answering = true;
        FindLargerCover(bitmap, kLargePixels, [weak, source, image](const std::string& address,
                                                                     const std::string& why) {
            std::shared_ptr<State> alive = weak.lock();
            if (alive && alive->apple == source) Found(alive, image.get(), address, why);  // else Apple moved on
        });
        state.answering = false;
    }
    if (state.large && !state.waiting) return state.large;
    if (state.refused) return source;
    bool patient = state.waiting && std::chrono::steady_clock::now() - state.asked < kPatience;
    return patient ? nullptr : source;  // the brushes keep the previous cover until this one opens
}

void LargeArtwork::Drop() {
    State& state = *m_state;
    DropLoading(state);
    RemoveLoader(state);
    state.loader = nullptr;
    state.apple = nullptr;
    state.large = nullptr;
    state.address.clear();
    state.refused = false;
    state.waiting = false;
    state.onOpened = nullptr;
}

std::string LargeArtwork::TakeNote() {
    return std::exchange(m_state->note, std::string());
}

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
