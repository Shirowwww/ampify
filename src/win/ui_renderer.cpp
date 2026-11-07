// Written against the C++/WinRT projection of Windows App SDK 1.8.
// The style copying follows AMExclusive (MIT, https://github.com/AntiOblivionis/AMExclusive, mod/src/UiAdapter.cpp,
// AttachOrRefreshToggle, at 91f589d): native Style, margins and padding first, WinUI card resources otherwise.

#if defined(AMPIFY_WITH_XAML)

#include "ui_renderer.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cwchar>
#include <limits>
#include <utility>

#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Xaml.Documents.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.Numerics.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.h>
#include <winrt/Windows.UI.Text.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Data.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>

#include "../core/i18n.hpp"
#include "../core/json.hpp"
#include "../core/now_playing.hpp"
#include "../core/player_queue.hpp"
#include "../core/ui_render/probe.hpp"
#include "../core/ui_render/style.hpp"
#include "../core/ui_render/svg_icon.hpp"
#include "artwork_fade.hpp"
#include "player_controls.hpp"
#include "xaml_host.hpp"

namespace ampify::win {

namespace {

namespace xaml = winrt::Microsoft::UI::Xaml;
namespace controls = xaml::Controls;
namespace primitives = xaml::Controls::Primitives;
namespace media = xaml::Media;
namespace automation = xaml::Automation;
using winrt::Windows::Foundation::IInspectable;
using uirender::Patch;
using uirender::PatchOp;
using uirender::RenderNode;

constexpr wchar_t kXmlns[] = L"xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation'";

winrt::hstring Wide(const std::string& text) {
    return winrt::to_hstring(text);
}

// Loads a small piece of XAML written by Ampify (never text of an extension, except path data checked by
// ParseSvgIcon). Null when the reader refuses it, for example when a theme resource is missing.
template <typename T>
T LoadXaml(const std::wstring& text) noexcept {
    try {
        return xaml::Markup::XamlReader::Load(winrt::hstring(text)).try_as<T>();
    } catch (...) {
        return nullptr;
    }
}

xaml::Style StyleResource(const wchar_t* key) noexcept {
    return ThemeResource(key).try_as<xaml::Style>();
}

media::Brush BrushResource(const wchar_t* key) noexcept {
    return ThemeResource(key).try_as<media::Brush>();
}

void SetText(const controls::TextBlock& block, const winrt::hstring& text) {
    if (block.Text() != text) block.Text(text);
}

void SetVisible(const xaml::UIElement& element, bool visible) {
    xaml::Visibility wanted = visible ? xaml::Visibility::Visible : xaml::Visibility::Collapsed;
    if (element.Visibility() != wanted) element.Visibility(wanted);
}

// A slider's header: its label (kept in Tag) and the value it is on, which WinUI shows only while dragging.
void ShowSliderValue(const controls::Slider& slider) {
    std::wstring label(winrt::unbox_value_or<winrt::hstring>(slider.Tag(), L""));
    double value = std::round(slider.Value() * 100) / 100;
    wchar_t number[32];
    std::swprintf(number, 32, L"%g", value);
    std::wstring text = label.empty() ? number : label + L" · " + number;
    slider.Header(winrt::box_value(winrt::hstring(text)));
}

const wchar_t* TextStyleKey(const std::string& variant) {
    if (variant == "title") return L"TitleTextBlockStyle";
    if (variant == "subtitle") return L"SubtitleTextBlockStyle";
    if (variant == "caption") return L"CaptionTextBlockStyle";
    if (variant == "bodyStrong") return L"BodyStrongTextBlockStyle";
    return L"BodyTextBlockStyle";
}

// An icon drawn as XAML shapes: the SVG paths in a Viewbox, filled with a theme brush, so the icon follows the light
// and dark modes. A glyph gives a FontIcon. Null when the icon cannot be drawn.
xaml::FrameworkElement MakeIcon(const std::string& icon, double size, const wchar_t* brushKey) {
    if (icon.empty()) return nullptr;
    if (uirender::IsGlyphIcon(icon)) {
        controls::FontIcon glyph;
        glyph.Glyph(Wide(icon));
        glyph.FontSize(size);
        return glyph;
    }
    uirender::SvgIcon svg = uirender::ParseSvgIcon(icon);
    if (!svg.Usable()) return nullptr;
    std::wstring text = L"<Viewbox " + std::wstring(kXmlns) + L" Width='" + std::to_wstring(size) + L"' Height='" +
                        std::to_wstring(size) + L"'><Canvas Width='" + std::to_wstring(svg.width) + L"' Height='" +
                        std::to_wstring(svg.height) + L"'>";
    for (const std::string& data : svg.paths) {
        text += L"<Path Data='" + std::wstring(Wide(data)) + L"' Fill='{ThemeResource " + brushKey + L"}'/>";
    }
    text += L"</Canvas></Viewbox>";
    return LoadXaml<xaml::FrameworkElement>(text);
}

// An image address: https, http, ms-appx and ms-appdata as they are; anything else is a file path.
winrt::Windows::Foundation::Uri ImageUri(const std::string& source) {
    for (const char* scheme : {"https://", "http://", "ms-appx:", "ms-appdata:", "file:"}) {
        if (source.rfind(scheme, 0) == 0) return winrt::Windows::Foundation::Uri(Wide(source));
    }
    std::string path = source;
    for (char& c : path) {
        if (c == '\\') c = '/';
    }
    return winrt::Windows::Foundation::Uri(Wide("file:///" + path));
}

primitives::PlacementMode TooltipPlacement(const std::string& placement) {
    if (placement == "bottom") return primitives::PlacementMode::Bottom;
    if (placement == "left") return primitives::PlacementMode::Left;
    if (placement == "right") return primitives::PlacementMode::Right;
    return primitives::PlacementMode::Top;
}

// The name read by screen readers: the label, the title, the text, or the tooltip.
std::string AccessibleName(const RenderNode& node) {
    for (const char* key : {"label", "title", "text", "tooltip"}) {
        std::string value = node.Text(key);
        if (!value.empty()) return value;
    }
    return {};
}

struct UpdateGuard {
    explicit UpdateGuard(int& counter) : m_counter(counter) { ++m_counter; }
    ~UpdateGuard() { --m_counter; }
    UpdateGuard(const UpdateGuard&) = delete;
    UpdateGuard& operator=(const UpdateGuard&) = delete;

private:
    int& m_counter;
};

using uirender::Layout;

constexpr double kImageMaxHeight = 240;  // an image without fit or height, as before the style attributes
constexpr double kInfinity = std::numeric_limits<double>::infinity();
constexpr double kAuto = std::numeric_limits<double>::quiet_NaN();  // XAML's Width and Height when unset
constexpr winrt::Windows::UI::Color kThemedAcrylicTint{0xFF, 0x20, 0x20, 0x20};

winrt::Windows::UI::Color ToColor(const uirender::Rgba& color) {
    return winrt::Windows::UI::Color{color.a, color.r, color.g, color.b};
}

bool SameLength(double a, double b) {
    return (std::isnan(a) && std::isnan(b)) || a == b;
}

// A background attribute as a brush: solid, or an in-app AcrylicBrush that blurs what is under it in the window.
media::Brush MakeBackground(const std::string& text) {
    std::optional<uirender::Background> parsed = uirender::ParseBackground(text);
    if (!parsed) return nullptr;
    if (!parsed->acrylic) return media::SolidColorBrush(ToColor(parsed->color));
    if (parsed->themed) {
        if (auto themed = BrushResource(L"AcrylicInAppFillColorDefaultBrush")) return themed;
    }
    winrt::Windows::UI::Color tint = parsed->themed ? kThemedAcrylicTint : ToColor(parsed->color);
    media::AcrylicBrush acrylic;
    acrylic.TintColor(tint);
    acrylic.TintOpacity(parsed->tintOpacity);
    acrylic.FallbackColor(tint);
    return acrylic;
}

xaml::HorizontalAlignment HorizontalOf(const std::string& value, xaml::HorizontalAlignment fallback) {
    if (value == "start") return xaml::HorizontalAlignment::Left;
    if (value == "center") return xaml::HorizontalAlignment::Center;
    if (value == "end") return xaml::HorizontalAlignment::Right;
    if (value == "stretch") return xaml::HorizontalAlignment::Stretch;
    return fallback;
}

xaml::VerticalAlignment VerticalOf(const std::string& value, xaml::VerticalAlignment fallback) {
    if (value == "start") return xaml::VerticalAlignment::Top;
    if (value == "center") return xaml::VerticalAlignment::Center;
    if (value == "end") return xaml::VerticalAlignment::Bottom;
    if (value == "stretch") return xaml::VerticalAlignment::Stretch;
    return fallback;
}

struct Placement {
    xaml::HorizontalAlignment horizontal = xaml::HorizontalAlignment::Stretch;
    xaml::VerticalAlignment vertical = xaml::VerticalAlignment::Stretch;
};

// Where a node sits in its parent when it says nothing: images, icons, buttons and dropdowns on the left (the last two
// as their WinUI styles have it), rows centred vertically.
Placement TypePlacement(const RenderNode& node) {
    Placement placement;
    if (node.type == "image" || node.type == "icon" || node.type == "button" || node.type == "dropdown") {
        placement.horizontal = xaml::HorizontalAlignment::Left;
    }
    if (node.type == "row") placement.vertical = xaml::VerticalAlignment::Center;
    return placement;
}

// The node's own align and valign over the parent's defaults. On a text, align is the text alignment instead. The
// values are always set locally: a control outside the tree has no style yet, so comparing with what it reads would
// skip the write, and its style would decide until the next update wrote the value (buttons stretched after a click).
void Place(const xaml::FrameworkElement& element, const RenderNode& node, Placement placement) {
    if (node.type != "text") placement.horizontal = HorizontalOf(node.Text("align"), placement.horizontal);
    placement.vertical = VerticalOf(node.Text("valign"), placement.vertical);
    auto unset = xaml::DependencyProperty::UnsetValue();
    if (element.ReadLocalValue(xaml::FrameworkElement::HorizontalAlignmentProperty()) == unset ||
        element.HorizontalAlignment() != placement.horizontal) {
        element.HorizontalAlignment(placement.horizontal);
    }
    if (element.ReadLocalValue(xaml::FrameworkElement::VerticalAlignmentProperty()) == unset ||
        element.VerticalAlignment() != placement.vertical) {
        element.VerticalAlignment(placement.vertical);
    }
}

void SetPanelBackground(const xaml::FrameworkElement& element, const media::Brush& brush) {
    if (auto panel = element.try_as<controls::Panel>()) panel.Background(brush);
    else if (auto border = element.try_as<controls::Border>()) border.Background(brush);
}

void SetPanelPadding(const xaml::FrameworkElement& element, const xaml::Thickness& padding) {
    if (auto stack = element.try_as<controls::StackPanel>()) stack.Padding(padding);
    else if (auto grid = element.try_as<controls::Grid>()) grid.Padding(padding);
    else if (auto border = element.try_as<controls::Border>()) border.Padding(padding);
}

void SetPanelCorner(const xaml::FrameworkElement& element, const xaml::CornerRadius& corner) {
    if (auto stack = element.try_as<controls::StackPanel>()) stack.CornerRadius(corner);
    else if (auto grid = element.try_as<controls::Grid>()) grid.CornerRadius(corner);
    else if (auto border = element.try_as<controls::Border>()) border.CornerRadius(corner);
}

void ClearLocal(const xaml::DependencyObject& object, const xaml::DependencyProperty& property) {
    if (object.ReadLocalValue(property) != xaml::DependencyProperty::UnsetValue()) object.ClearValue(property);
}

winrt::Windows::Foundation::TimeSpan Milliseconds(double ms) {
    return std::chrono::milliseconds(static_cast<std::int64_t>(ms));
}

media::Stretch StretchOf(const std::string& fit) {
    if (fit == "cover") return media::Stretch::UniformToFill;
    if (fit == "fill") return media::Stretch::Fill;
    return media::Stretch::Uniform;
}

}  // namespace

// What an event handler needs, shared with the handlers so they never hold the renderer itself.
struct XamlRenderer::Callbacks {
    std::string elementId;
    int onClick = 0;
    int onChange = 0;
    UiEventSink sink;
    std::weak_ptr<int> updating;
    // The value the renderer last set itself, as JSON. WinUI may report such a change after the renderer has returned
    // (Toggled and TextChanged can arrive late): that echo is dropped once, so the extension only hears the user.
    mutable std::string echo;

    void Fire(bool click, const std::string& valueJson) const noexcept {
        try {
            std::shared_ptr<int> busy = updating.lock();
            if (!busy) return;  // the renderer is gone
            // The renderer's own value, reported now (Toggled, ValueChanged) or late (TextChanged): dropped, and the
            // echo is spent either way, so a later change by the user to that same value still counts.
            if (!click && !echo.empty() && valueJson == echo) {
                echo.clear();
                return;
            }
            if (*busy > 0) return;  // the renderer itself is setting values
            if (!click) echo.clear();
            int id = click ? onClick : onChange;
            if (id <= 0 || !sink) return;
            sink(UiEvent{elementId, id, click, valueJson});
        } catch (...) {
            // A handler runs inside Apple Music's XAML frame: nothing may escape it.
        }
    }
};

struct LyricsView;

// An image that shows the song playing (src: "artwork"): Apple's play-bar image tells when the song changes.
struct ArtworkFollow {
    media::ImageBrush brush{nullptr};
    controls::Image sizer{nullptr};
    controls::Image apple{nullptr};  // set while followed
    int64_t token = 0;
    bool off = false;  // the image shows something else now
    // The large cover being loaded and its handlers, removed by token: auto_revoke goes through a weak reference,
    // which never resolves for a XAML object Ampify made, so the handlers would stay and an old cover land late.
    xaml::Media::Imaging::BitmapImage loading{nullptr};
    winrt::event_token openedToken{};
    winrt::event_token failedToken{};
    bool pinned = false;  // the sizer held at its height while it loads the large cover
    // The frame's Loaded and Unloaded handlers, removed when the image shows something else. The frame itself is not
    // kept: it holds those handlers, which hold the follower.
    winrt::event_token loadedToken{};
    winrt::event_token unloadedToken{};
    // The song playing only: the previous cover fades out over the new one, rounded like the frame.
    ArtworkFade fade;
    xaml::CornerRadius corner{};
    // Without Apple's play-bar image in the window, Apple's loader gives the cover of each new song (PollArtwork).
    winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer poll{nullptr};
    std::string polledKey;  // the song whose cover the loader last gave

    ~ArtworkFollow() {
        try {
            if (poll) poll.Stop();
        } catch (...) {  // off the interface thread, or once its queue has stopped: never terminate over a timer
        }
    }
};

struct XamlRenderer::Mounted {
    std::string type;
    Layout layout = Layout::None;  // how the native container lays out the children (uirender::LayoutOf)
    std::vector<bool> cells;       // Layout::Grid: the growing children the cell definitions were made for
    int columns = 0;               // Layout::Columns (grid): the columns and rows the cell definitions were made for
    int rows = 0;
    // What the style attributes last set, so an update only touches what changed and a removed one is undone.
    std::string background;
    std::string transition;
    std::string source;            // image address
    bool colored = false;          // a text whose color the renderer set
    bool centered = false;         // the scale centre follows the size
    double baseOpacity = 1;        // a dimmed secondary text: the opacity attribute scales it
    media::Brush baseBackground{nullptr};  // a card's own look, put back when the attribute goes
    xaml::Thickness basePadding{};
    xaml::CornerRadius baseCorner{};
    // All strong: in Apple Music a weak reference to a XAML object Ampify made never resolves, even while the object
    // is on screen (seen on 1.1792: the slot of a panel was lost right after it was made). The renderer is released
    // on the interface thread with its surface, and no handler holds these objects, so there is no cycle.
    xaml::FrameworkElement outer{nullptr};    // what the parent holds
    xaml::FrameworkElement control{nullptr};  // what takes the attributes, the name and the tooltip
    controls::Panel host{nullptr};            // where the children go
    controls::TextBlock label{nullptr};       // the label of a toggle, the title of a card
    controls::TextBlock subtitle{nullptr};    // the subtitle of a card
    media::ImageBrush brush{nullptr};         // an image: drawn by its frame's brush, so the corners can be rounded
    controls::Image sizer{nullptr};           // an image: invisible, it gives the frame the picture's own size
    std::shared_ptr<ArtworkFollow> artwork;   // an image showing the song playing
    std::shared_ptr<LyricsView> lyrics;       // a lyrics node
    std::shared_ptr<Callbacks> callbacks;
    std::vector<Mounted> children;
};

namespace {

constexpr double kArtworkPixels = 1200;  // Apple's library covers are 600 to 1200 px wide, its catalog ones larger
constexpr int kArtworkSearchLimit = 20000;  // elements looked at in the window, a few thousand on the busiest pages

// Apple Music's image of the song playing: the first Image#StaticArtwork under NowPlayingControl#LCD, the one its play
// bar shows (the same that `background: artwork` uses, src/win/xaml_backend.cpp). Null when the bar has none.
controls::Image FindPlayingArtwork(const xaml::XamlRoot& root) {
    if (!root || !root.Content()) return nullptr;
    std::vector<std::pair<xaml::DependencyObject, bool>> pending{{root.Content(), false}};
    for (int looked = 0; !pending.empty() && looked < kArtworkSearchLimit; looked++) {
        auto [object, inDisplay] = pending.back();
        pending.pop_back();
        auto element = object.try_as<xaml::FrameworkElement>();
        std::wstring_view name = element ? std::wstring_view(element.Name()) : std::wstring_view();
        if (element && name == L"LCD" && object.try_as<controls::Control>()) inDisplay = true;
        if (inDisplay && name == L"StaticArtwork") {
            if (auto image = object.try_as<controls::Image>()) return image;
        }
        int count = media::VisualTreeHelper::GetChildrenCount(object);
        for (int i = count - 1; i >= 0; i--) pending.emplace_back(media::VisualTreeHelper::GetChild(object, i), inDisplay);
    }
    return nullptr;
}

// Apple empties its image between two songs: the previous cover stays until the next one, so the image keeps its
// size and a scrolled column does not jump.
void ShowArtwork(ArtworkFollow& follow, const media::ImageSource& source) {
    if (!source) return;
    media::ImageSource previous = follow.brush.ImageSource();
    if (previous != source) FadeFromPrevious(follow.fade, previous, follow.corner);
    follow.brush.ImageSource(source);
    follow.sizer.Source(source);
}

winrt::Windows::Foundation::Uri AddressOf(const media::ImageSource& source) {
    auto bitmap = source ? source.try_as<media::Imaging::BitmapImage>() : nullptr;
    return bitmap ? bitmap.UriSource() : nullptr;
}

// The image already shows this file decoded large. Apple sets its play-bar image again with the same file (two songs of
// one album, a redraw of its bar); a new bitmap of an address WinUI holds decoded may never raise ImageOpened, which
// left the thumbnail upscaled in place of the cover.
bool ShowsLarge(const ArtworkFollow& follow, const winrt::Windows::Foundation::Uri& address) {
    if (!address) return false;
    auto shown = follow.brush.ImageSource().try_as<media::Imaging::BitmapImage>();
    if (!shown || shown.DecodePixelWidth() != static_cast<int>(kArtworkPixels)) return false;
    winrt::Windows::Foundation::Uri current = shown.UriSource();
    return current && current.Equals(address);
}

// The large cover's handlers, removed once fired or replaced: the bitmap would otherwise hold the follower, which
// holds the bitmap.
void DropLoading(ArtworkFollow& follow) {
    if (!follow.loading) return;
    follow.loading.ImageOpened(follow.openedToken);
    follow.loading.ImageFailed(follow.failedToken);
    follow.loading = nullptr;
    if (follow.pinned) follow.sizer.ClearValue(xaml::FrameworkElement::HeightProperty());
    follow.pinned = false;
}

// Apple's image at once, then the same cover decoded at its full size as soon as it has loaded: Apple's own bitmaps
// are decoded at the size of where Apple shows them (a thumbnail in the play bar), though the file behind is large
// (600 to 1200 px for the library). No empty frame between two songs.
void ShowCover(const std::shared_ptr<ArtworkFollow>& follow, const media::ImageSource& shown,
               const media::ImageSource& broker) {
    DropLoading(*follow);
    winrt::Windows::Foundation::Uri address = AddressOf(broker);
    if (!address) address = AddressOf(shown);
    winrt::Windows::Foundation::Uri shownAddress = AddressOf(shown);
    // Apple's image of another file means a new song even while the loader still gives the previous cover.
    if (ShowsLarge(*follow, address) && (!shownAddress || shownAddress.Equals(address))) return;
    ShowArtwork(*follow, shown);
    if (!address) return;
    media::Imaging::BitmapImage large;
    large.DecodePixelWidth(static_cast<int>(kArtworkPixels));  // a 3000 px catalog cover is not decoded whole
    follow->loading = large;
    // Only the latest bitmap is shown: a slow cover of the previous song that opens late is ignored.
    follow->openedToken = large.ImageOpened([follow](IInspectable const& sender, xaml::RoutedEventArgs const&) noexcept {
        try {
            auto bitmap = sender.as<media::Imaging::BitmapImage>();
            if (follow->loading != bitmap) return;
            if (!follow->off) follow->brush.ImageSource(bitmap);
            DropLoading(*follow);
        } catch (...) {
        }
    });
    follow->failedToken = large.ImageFailed([follow](IInspectable const& sender, xaml::ExceptionRoutedEventArgs const&) noexcept {
        try {
            if (follow->loading != sender.as<media::Imaging::BitmapImage>()) return;
            follow->sizer.Source(follow->brush.ImageSource());
            DropLoading(*follow);
        } catch (...) {
        }
    });
    large.UriSource(address);
    // WinUI decodes a bitmap only once something in the window shows it: the invisible sizer loads it, held at its
    // height meanwhile so the column does not jump, and the visible brush takes it once it has opened.
    if (double held = follow->sizer.ActualHeight(); held > 0) {
        follow->sizer.Height(held);
        follow->pinned = true;
    }
    follow->sizer.Source(large);
}

// Apple's image of the song playing at once, then its cover large (ShowCover).
void RefreshArtwork(const std::shared_ptr<ArtworkFollow>& follow) {
    if (!follow->apple) return;
    ShowCover(follow, follow->apple.Source(), PlayingArtworkSource(kArtworkPixels));
}

// A queue entry's cover (AppleArtworkSource): Apple's loader draws it, then it is decoded large. It does not follow
// anything, as the entry keeps its cover; the image gets a new src when the view shows another entry.
std::shared_ptr<ArtworkFollow> ShowQueueArtwork(const media::ImageBrush& brush, const controls::Image& sizer,
                                                std::uint64_t itemId) {
    auto follow = std::make_shared<ArtworkFollow>();
    follow->brush = brush;
    follow->sizer = sizer;
    media::ImageSource broker = QueueArtworkImage(itemId, kArtworkPixels);
    ShowCover(follow, broker, broker);
    return follow;
}

constexpr auto kArtworkPoll = std::chrono::milliseconds(500);

// The cover of the song playing from Apple's loader, once per song: the key of the now-playing feed tells a new one.
void PollArtwork(const std::shared_ptr<ArtworkFollow>& follow) {
    std::string key = CurrentNowPlaying().trackKey;
    if (follow->off || follow->apple || key == follow->polledKey) return;
    media::ImageSource broker = PlayingArtworkSource(kArtworkPixels);
    if (!broker) return;  // Apple has no cover yet: the next tick asks again
    follow->polledKey = key;
    ShowCover(follow, broker, broker);
}

// Apple's play-bar image was not found in the window (a view in a popup over a page too large to walk, or a layout
// without it): the image would have stayed blank. It follows the song through Apple's loader instead.
void PollArtworkWithoutApple(const std::shared_ptr<ArtworkFollow>& follow) {
    PollArtwork(follow);
    if (!follow->poll) {
        auto queue = winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();
        if (!queue) return;
        follow->poll = queue.CreateTimer();
        follow->poll.Interval(kArtworkPoll);
        follow->poll.IsRepeating(true);
        std::weak_ptr<ArtworkFollow> weak = follow;
        follow->poll.Tick([weak](auto const&, auto const&) noexcept {
            try {
                if (std::shared_ptr<ArtworkFollow> alive = weak.lock()) PollArtwork(alive);
            } catch (...) {
            }
        });
    }
    if (!follow->poll.IsRunning()) follow->poll.Start();
}

bool StartArtwork(const std::shared_ptr<ArtworkFollow>& follow, const xaml::XamlRoot& root) {
    if (follow->off || follow->apple) return true;
    controls::Image apple = FindPlayingArtwork(root);
    if (!apple) {
        PollArtworkWithoutApple(follow);
        return false;
    }
    if (follow->poll) follow->poll.Stop();
    follow->apple = apple;
    follow->token = apple.RegisterPropertyChangedCallback(
        controls::Image::SourceProperty(), [follow](xaml::DependencyObject const&, xaml::DependencyProperty const&) noexcept {
            try {
                RefreshArtwork(follow);
            } catch (...) {
            }
        });
    RefreshArtwork(follow);
    return true;
}

void StopArtwork(ArtworkFollow& follow) {
    DropLoading(follow);
    if (follow.poll) follow.poll.Stop();
    follow.polledKey.clear();  // shown again when the image comes back to the window
    if (!follow.apple) return;
    follow.apple.UnregisterPropertyChangedCallback(controls::Image::SourceProperty(), follow.token);
    follow.apple = nullptr;
}

// The image shows something else now: what it followed stops, and its fade layer leaves the frame.
void DropFollow(const controls::Border& frame, ArtworkFollow& follow) {
    follow.off = true;
    StopArtwork(follow);
    if (follow.loadedToken.value) frame.Loaded(follow.loadedToken);
    if (follow.unloadedToken.value) frame.Unloaded(follow.unloadedToken);
    if (follow.fade.story) follow.fade.story.Stop();
    auto layers = frame.Child().try_as<controls::Grid>();
    std::uint32_t index = 0;
    if (follow.fade.layer && layers && layers.Children().IndexOf(follow.fade.layer, index)) layers.Children().RemoveAt(index);
}

// Ties an image of a view to Apple's own image of the song playing, which changes with every song, and shows that
// cover loaded large. Followed while the view is in the window, where Apple's image can be found.
std::shared_ptr<ArtworkFollow> FollowPlayingArtwork(const controls::Border& frame, const media::ImageBrush& brush,
                                                    const controls::Image& sizer) {
    auto follow = std::make_shared<ArtworkFollow>();
    follow->brush = brush;
    follow->sizer = sizer;
    follow->fade = MakeArtworkFade(brush.Stretch());
    follow->corner = frame.CornerRadius();
    if (auto layers = frame.Child().try_as<controls::Grid>()) layers.Children().Append(follow->fade.layer);
    if (frame.XamlRoot()) StartArtwork(follow, frame.XamlRoot());
    follow->loadedToken = frame.Loaded([follow](IInspectable const& sender, xaml::RoutedEventArgs const&) noexcept {
        try {
            StartArtwork(follow, sender.as<controls::Border>().XamlRoot());
        } catch (...) {
        }
    });
    follow->unloadedToken = frame.Unloaded([follow](IInspectable const&, xaml::RoutedEventArgs const&) noexcept {
        try {
            StopArtwork(*follow);
        } catch (...) {
        }
    });
    return follow;
}

constexpr double kLyricsSize = 26;
constexpr double kLyricsIdleOpacity = 0.4;  // the lines not being sung
constexpr double kLyricsHoverOpacity = 0.6; // such a line under the pointer
constexpr double kLyricsUnsung = 0.3;       // a word of the current line not reached yet
constexpr double kLyricsIdleScale = 0.92;   // the current line is drawn larger than the others
constexpr int64_t kLyricsLeadMs = 120;      // a word lights up a little ahead, as the eye expects it
constexpr int64_t kLyricsManualMs = 4000;   // after the user scrolls, the view stays put this long
constexpr auto kLyricsFrame = std::chrono::milliseconds(33);
constexpr auto kLyricsMotion = std::chrono::milliseconds(350);

}  // namespace

// The lyrics of the song playing (the `lyrics` node), lit word by word as in Apple's own view: one TextBlock per line,
// one Run per word whose brush brightens as the word is sung, the current line larger and kept in view. Driven by a
// timer of the interface thread from src/core/now_playing.hpp, so the extension does not render it again and again.
struct LyricsView {
    controls::ScrollViewer scroller{nullptr};
    controls::StackPanel lines{nullptr};
    controls::TextBlock placeholder{nullptr};
    winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer timer{nullptr};
    std::shared_ptr<const Lyrics> shown;
    std::vector<std::vector<media::SolidColorBrush>> words;  // per line, per word (one per line without word times)
    int current = -2;                                        // the line drawn as current; -2 before the first frame
    double size = kLyricsSize;
    winrt::Windows::UI::Color color{255, 255, 255, 255};
    int64_t manualUntil = 0;
    bool stale = true;  // the look changed: build the lines again
    std::weak_ptr<LyricsView> self;
    std::function<void(int64_t)> pick;  // a line clicked: its start, for the element's onChange

    ~LyricsView() {
        if (timer) timer.Stop();
    }
};

namespace {

winrt::Windows::UI::Color WithAlpha(winrt::Windows::UI::Color color, double alpha) {
    color.A = static_cast<uint8_t>(std::lround(std::clamp(alpha, 0.0, 1.0) * color.A));
    return color;
}

void SetBrushAlpha(media::SolidColorBrush& brush, winrt::Windows::UI::Color base, double alpha) {
    winrt::Windows::UI::Color wanted = WithAlpha(base, alpha);
    if (brush.Color() != wanted) brush.Color(wanted);
}

controls::TextBlock LyricLineBlock(LyricsView& view, const LyricLine& line, bool timed) {
    controls::TextBlock block;
    block.TextWrapping(xaml::TextWrapping::Wrap);
    block.FontSize(view.size);
    block.FontWeight(winrt::Windows::UI::Text::FontWeights::Bold());
    xaml::ScalarTransition fade;
    fade.Duration(kLyricsMotion);
    block.OpacityTransition(fade);
    xaml::Vector3Transition grow;
    grow.Duration(kLyricsMotion);
    block.ScaleTransition(grow);
    std::vector<media::SolidColorBrush> brushes;
    for (const KaraokeWord& word : KaraokeWords(line)) {
        winrt::Microsoft::UI::Xaml::Documents::Run run;
        run.Text(Wide(word.piece));
        media::SolidColorBrush brush(view.color);
        run.Foreground(brush);
        block.Inlines().Append(run);
        brushes.push_back(brush);
    }
    view.words.push_back(std::move(brushes));
    if (timed) {
        int64_t start = line.startMs;
        block.Tapped([weak = view.self, start](IInspectable const&, xaml::Input::TappedRoutedEventArgs const&) noexcept {
            try {
                std::shared_ptr<LyricsView> alive = weak.lock();
                if (!alive || !alive->pick) return;
                alive->manualUntil = 0;  // the view follows the song again from the line picked
                alive->pick(start);
            } catch (...) {
            }
        });
        // A line under the pointer brightens a little, to say it can be clicked; the current one stays as it is.
        block.PointerEntered([](IInspectable const& sender, xaml::Input::PointerRoutedEventArgs const&) noexcept {
            try {
                auto line = sender.as<controls::TextBlock>();
                if (line.Opacity() < 1) line.Opacity(kLyricsHoverOpacity);
            } catch (...) {
            }
        });
        block.PointerExited([](IInspectable const& sender, xaml::Input::PointerRoutedEventArgs const&) noexcept {
            try {
                auto line = sender.as<controls::TextBlock>();
                if (line.Opacity() < 1) line.Opacity(kLyricsIdleOpacity);
            } catch (...) {
            }
        });
        block.Opacity(kLyricsIdleOpacity);
        block.Scale({static_cast<float>(kLyricsIdleScale), static_cast<float>(kLyricsIdleScale), 1});
    }
    return block;
}

void BuildLyricLines(LyricsView& view, const std::shared_ptr<const Lyrics>& lyrics) {
    view.shown = lyrics;
    view.stale = false;
    view.current = -2;
    view.words.clear();
    view.lines.Children().Clear();
    view.placeholder.Visibility(lyrics ? xaml::Visibility::Collapsed : xaml::Visibility::Visible);
    view.placeholder.Foreground(media::SolidColorBrush(WithAlpha(view.color, kLyricsIdleOpacity)));
    if (lyrics) {
        for (const LyricLine& line : lyrics->lines) view.lines.Children().Append(LyricLineBlock(view, line, lyrics->synced));
    }
    view.scroller.ChangeView(nullptr, winrt::Windows::Foundation::IReference<double>(0.0), nullptr, true);
}

controls::TextBlock LyricLineAt(const LyricsView& view, int index) {
    if (index < 0 || static_cast<uint32_t>(index) >= view.lines.Children().Size()) return nullptr;
    return view.lines.Children().GetAt(index).try_as<controls::TextBlock>();
}

// Grows from the left edge of its middle, as Apple's lines do.
void SetLineLook(const controls::TextBlock& block, bool current) {
    float scale = current ? 1.0f : static_cast<float>(kLyricsIdleScale);
    block.CenterPoint({0, static_cast<float>(block.ActualHeight() / 2), 0});
    block.Scale({scale, scale, 1});
    block.Opacity(current ? 1.0 : kLyricsIdleOpacity);
}

// The current line about a third down the view, unless the user scrolled a moment ago.
void KeepLineInView(LyricsView& view, const controls::TextBlock& block, int64_t tick) {
    if (tick < view.manualUntil) return;
    if (block.ActualHeight() == 0) view.lines.UpdateLayout();  // lines just built: not measured yet, all at 0
    auto top =block.TransformToVisual(view.lines).TransformPoint({0, 0});
    double target = std::max(0.0, top.Y - view.scroller.ViewportHeight() * 0.3);
    view.scroller.ChangeView(nullptr, winrt::Windows::Foundation::IReference<double>(target), nullptr, false);
}

void PaintLyrics(LyricsView& view) {
    // The window hidden (in the notification area, minimized): 30 frames a second for nobody. The next frame shown
    // catches up with the song at once.
    if (auto root = view.scroller ? view.scroller.XamlRoot() : nullptr; root && !root.IsHostVisible()) return;
    NowPlaying now = CurrentNowPlaying();
    if (view.stale || now.lyrics != view.shown) BuildLyricLines(view, now.lyrics);
    if (!view.shown || !view.shown->synced) return;
    const int64_t tick = static_cast<int64_t>(GetTickCount64());
    KaraokeFrame frame = KaraokeAt(*view.shown, now.PositionAt(tick) + kLyricsLeadMs);
    if (frame.line != view.current) {
        if (controls::TextBlock old = LyricLineAt(view, view.current)) {
            SetLineLook(old, false);
            for (media::SolidColorBrush& brush : view.words[view.current]) SetBrushAlpha(brush, view.color, 1);
        }
        view.current = frame.line;
        if (controls::TextBlock line = LyricLineAt(view, frame.line)) {
            SetLineLook(line, true);
            KeepLineInView(view, line, tick);
        }
    }
    if (frame.line < 0 || static_cast<size_t>(frame.line) >= view.words.size()) return;
    std::vector<media::SolidColorBrush>& brushes = view.words[frame.line];
    for (size_t i = 0; i < brushes.size(); i++) {
        double done = frame.between ? 1.0 : frame.words[i];
        SetBrushAlpha(brushes[i], view.color, kLyricsUnsung + (1 - kLyricsUnsung) * done);
    }
}

void RunLyrics(const std::shared_ptr<LyricsView>& view, bool run) {
    if (!run) {
        if (view->timer) view->timer.Stop();
        return;
    }
    if (!view->timer) {
        view->timer = winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread().CreateTimer();
        view->timer.Interval(kLyricsFrame);
        std::weak_ptr<LyricsView> weak = view;  // the timer must not keep the view alive
        view->timer.Tick([weak](auto&&, auto&&) noexcept {
            try {
                if (std::shared_ptr<LyricsView> alive = weak.lock()) PaintLyrics(*alive);
            } catch (...) {
            }
        });
    }
    view->timer.Start();
}

xaml::FrameworkElement MakeLyricsView(const std::shared_ptr<LyricsView>& view) {
    controls::Grid root;
    view->self = view;
    view->lines = controls::StackPanel();
    view->lines.Spacing(view->size * 0.6);
    view->lines.Padding(xaml::Thickness{0, 24, 24, 240});  // the last line can still come up to its place
    view->scroller = controls::ScrollViewer();
    view->scroller.VerticalScrollBarVisibility(controls::ScrollBarVisibility::Hidden);
    view->scroller.Content(view->lines);
    view->placeholder = controls::TextBlock();
    view->placeholder.TextWrapping(xaml::TextWrapping::Wrap);
    view->placeholder.FontSize(16);
    root.Children().Append(view->scroller);
    root.Children().Append(view->placeholder);
    std::weak_ptr<LyricsView> weak = view;
    view->scroller.PointerWheelChanged([weak](auto&&, auto&&) noexcept {
        if (std::shared_ptr<LyricsView> alive = weak.lock()) {
            alive->manualUntil = static_cast<int64_t>(GetTickCount64()) + kLyricsManualMs;
        }
    });
    root.Loaded([weak](auto&&, auto&&) noexcept {
        try {
            if (std::shared_ptr<LyricsView> alive = weak.lock()) RunLyrics(alive, true);
        } catch (...) {
        }
    });
    root.Unloaded([weak](auto&&, auto&&) noexcept {
        try {
            if (std::shared_ptr<LyricsView> alive = weak.lock()) RunLyrics(alive, false);
        } catch (...) {
        }
    });
    return root;
}

}  // namespace

int StyleProbe::Found() const {
    int count = 0;
    for (const xaml::Style& style : {button, toggle, comboBox, slider, textBox, titleText, captionText}) {
        if (style) count++;
    }
    if (cardBackground) count++;
    if (cardStroke) count++;
    if (cardPadding) count++;
    if (cardCorner) count++;
    return count;
}

IInspectable ThemeResource(const wchar_t* key) noexcept {
    try {
        xaml::Application application = xaml::Application::Current();
        if (!application) return nullptr;
        return application.Resources().TryLookup(winrt::box_value(winrt::hstring(key)));
    } catch (...) {
        return nullptr;
    }
}

void CaptureStyleProbe(const xaml::DependencyObject& root, StyleProbe& probe, const std::string& origin) {
    if (!root) return;
    int found = 0;
    std::vector<xaml::DependencyObject> stack{root};
    std::size_t visited = 0;
    controls::ToggleSwitch nativeToggle{nullptr};
    while (!stack.empty() && visited < 4000) {
        xaml::DependencyObject node = stack.back();
        stack.pop_back();
        visited++;
        try {
            auto element = node.try_as<xaml::FrameworkElement>();
            if (element) {
                std::string name = Utf8(element.Name());
                if (uirender::IsOwnName(name) || uirender::IsForeignModName(name)) continue;
            }
            if (node.try_as<controls::ItemsControl>() || node.try_as<controls::ItemsRepeater>()) continue;

            if (auto toggle = node.try_as<controls::ToggleSwitch>()) {
                if (toggle.Style()) probe.toggle = toggle.Style();
                nativeToggle = toggle;  // the last one: its row gives the card values below
                found++;
                continue;
            }
            if (auto combo = node.try_as<controls::ComboBox>()) {
                if (combo.Style() && !probe.comboBox) probe.comboBox = combo.Style();
                found++;
                continue;
            }
            if (auto slider = node.try_as<controls::Slider>()) {
                if (slider.Style() && !probe.slider) probe.slider = slider.Style();
                found++;
                continue;
            }
            if (auto box = node.try_as<controls::TextBox>()) {
                if (box.Style() && !probe.textBox) probe.textBox = box.Style();
                found++;
                continue;
            }
            if (auto button = node.try_as<controls::Button>()) {
                // Only a button that shows a text: an icon button of the title bar would give a wrong look.
                bool textual = !winrt::unbox_value_or<winrt::hstring>(button.Content(), L"").empty();
                if (textual && button.Style() && !probe.button) probe.button = button.Style();
                if (textual) found++;
                continue;
            }
            int count = media::VisualTreeHelper::GetChildrenCount(node);
            for (int index = count - 1; index >= 0; index--) stack.push_back(media::VisualTreeHelper::GetChild(node, index));
        } catch (...) {
        }
    }

    // The card of a native settings row: the widest Border with a background between the toggle and the root, and the
    // two first TextBlocks of that row for the title and the description.
    if (nativeToggle) {
        try {
            controls::Border best{nullptr};
            double bestArea = 0;
            for (xaml::DependencyObject current = nativeToggle; current && current != root;
                 current = media::VisualTreeHelper::GetParent(current)) {
                auto border = current.try_as<controls::Border>();
                if (!border || !border.Background()) continue;
                double area = border.ActualWidth() * border.ActualHeight();
                if (area > bestArea && border.ActualWidth() > nativeToggle.ActualWidth() * 3.0) {
                    best = border;
                    bestArea = area;
                }
            }
            if (best) {
                probe.cardBackground = best.Background();
                probe.cardStroke = best.BorderBrush() ? best.BorderBrush() : BrushResource(L"CardStrokeColorDefaultBrush");
                probe.cardPadding = best.Padding();
                probe.cardCorner = best.CornerRadius();
                probe.cardMargin = best.Margin();
                std::vector<controls::TextBlock> texts;
                std::vector<xaml::DependencyObject> pending{best};
                while (!pending.empty() && texts.size() < 2) {
                    xaml::DependencyObject node = pending.back();
                    pending.pop_back();
                    if (node.try_as<controls::ToggleSwitch>()) continue;  // its On/Off text is not a row title
                    if (auto text = node.try_as<controls::TextBlock>(); text && !text.Text().empty()) {
                        texts.push_back(text);
                        continue;
                    }
                    int count = media::VisualTreeHelper::GetChildrenCount(node);
                    for (int index = count - 1; index >= 0; index--) pending.push_back(media::VisualTreeHelper::GetChild(node, index));
                }
                if (!texts.empty() && texts[0].Style()) probe.titleText = texts[0].Style();
                if (texts.size() > 1 && texts[1].Style()) {
                    probe.captionText = texts[1].Style();
                    probe.captionOpacity = texts[1].Opacity();
                }
            }
        } catch (...) {
        }
    }
    if (found > 0 || probe.Found() > 0) probe.origin = origin;
}

controls::Border MakeCardBorder(const StyleProbe& probe) {
    // {ThemeResource} keeps the brushes in step with the light and dark modes; the literal values are WinUI's card.
    controls::Border card = LoadXaml<controls::Border>(
        L"<Border " + std::wstring(kXmlns) +
        L" Background='{ThemeResource CardBackgroundFillColorDefaultBrush}'"
        L" BorderBrush='{ThemeResource CardStrokeColorDefaultBrush}' BorderThickness='1' CornerRadius='8'"
        L" Padding='16,12,16,12'/>");
    if (!card) {
        card = controls::Border();
        card.Background(BrushResource(L"CardBackgroundFillColorDefaultBrush"));
        card.BorderBrush(BrushResource(L"CardStrokeColorDefaultBrush"));
        card.BorderThickness(xaml::Thickness{1, 1, 1, 1});
        card.CornerRadius(xaml::CornerRadius{8, 8, 8, 8});
        card.Padding(xaml::Thickness{16, 12, 16, 12});
    }
    if (probe.cardBackground) card.Background(probe.cardBackground);
    if (probe.cardStroke) card.BorderBrush(probe.cardStroke);
    if (probe.cardCorner) card.CornerRadius(*probe.cardCorner);
    if (probe.cardPadding) card.Padding(*probe.cardPadding);
    return card;
}

// A primary button: the accent brushes in every visual state. Under Apple Music's button style the pointer brought back
// the plain button's brushes, and a brush set directly stayed the one read when the button was drawn (a dark accent in
// a light window after a theme change). {ThemeResource} follows the light and dark modes and the theme's colours.
controls::ControlTemplate PrimaryButtonTemplate() {
    return LoadXaml<controls::ControlTemplate>(
        L"<ControlTemplate " + std::wstring(kXmlns) +
        L" xmlns:x='http://schemas.microsoft.com/winfx/2006/xaml' TargetType='Button'>"
        L"<Grid x:Name='Root' Background='{ThemeResource AccentFillColorDefaultBrush}'"
        L" BorderBrush='{ThemeResource AccentControlElevationBorderBrush}' BorderThickness='{TemplateBinding BorderThickness}'"
        L" CornerRadius='{TemplateBinding CornerRadius}' Padding='{TemplateBinding Padding}'>"
        L"<VisualStateManager.VisualStateGroups><VisualStateGroup x:Name='CommonStates'>"
        L"<VisualState x:Name='Normal'/>"
        L"<VisualState x:Name='PointerOver'><VisualState.Setters>"
        L"<Setter Target='Root.Background' Value='{ThemeResource AccentFillColorSecondaryBrush}'/>"
        L"</VisualState.Setters></VisualState>"
        L"<VisualState x:Name='Pressed'><VisualState.Setters>"
        L"<Setter Target='Root.Background' Value='{ThemeResource AccentFillColorTertiaryBrush}'/>"
        L"<Setter Target='Label.Foreground' Value='{ThemeResource TextOnAccentFillColorSecondaryBrush}'/>"
        L"</VisualState.Setters></VisualState>"
        L"<VisualState x:Name='Disabled'><VisualState.Setters>"
        L"<Setter Target='Root.Background' Value='{ThemeResource AccentFillColorDisabledBrush}'/>"
        L"<Setter Target='Root.BorderBrush' Value='{ThemeResource ControlFillColorTransparentBrush}'/>"
        L"<Setter Target='Label.Foreground' Value='{ThemeResource TextOnAccentFillColorDisabledBrush}'/>"
        L"</VisualState.Setters></VisualState>"
        L"</VisualStateGroup></VisualStateManager.VisualStateGroups>"
        L"<ContentPresenter x:Name='Label' Content='{TemplateBinding Content}'"
        L" Foreground='{ThemeResource TextOnAccentFillColorPrimaryBrush}' FontFamily='{TemplateBinding FontFamily}'"
        L" FontSize='{TemplateBinding FontSize}' FontWeight='{TemplateBinding FontWeight}'"
        L" HorizontalAlignment='{TemplateBinding HorizontalContentAlignment}'"
        L" VerticalAlignment='{TemplateBinding VerticalContentAlignment}'/>"
        L"</Grid></ControlTemplate>");
}

controls::TextBlock MakeText(const std::wstring& text, const std::string& variant, bool secondary, const StyleProbe* probe) {
    controls::TextBlock block{nullptr};
    if (secondary) {
        block = LoadXaml<controls::TextBlock>(L"<TextBlock " + std::wstring(kXmlns) +
                                              L" Foreground='{ThemeResource TextFillColorSecondaryBrush}'/>");
    }
    if (!block) block = controls::TextBlock();
    xaml::Style style{nullptr};
    if (probe && variant == "bodyStrong" && probe->titleText) style = probe->titleText;
    // Dimmed as Apple's own descriptions: as the native row the probe found, else as those of its Settings (0.6).
    constexpr double kAppleCaptionOpacity = 0.6;
    if (probe && variant == "caption" && probe->captionText) {
        style = probe->captionText;
        block.Opacity(probe->captionOpacity);
    } else if (secondary) {
        block.Opacity(kAppleCaptionOpacity);
    }
    if (!style) style = StyleResource(TextStyleKey(variant));
    if (style) block.Style(style);
    block.TextWrapping(xaml::TextWrapping::Wrap);
    block.Text(winrt::hstring(text));
    return block;
}

const wchar_t* KitXmlns() noexcept {
    return kXmlns;
}

IInspectable LoadKitXaml(const std::wstring& text) noexcept {
    try {
        return xaml::Markup::XamlReader::Load(winrt::hstring(text));
    } catch (...) {
        return nullptr;
    }
}

xaml::FrameworkElement MakeIconElement(const std::string& icon, double size, const wchar_t* brushKey) {
    return MakeIcon(icon, size, brushKey);
}

xaml::FrameworkElement MakeDivider() {
    xaml::FrameworkElement line = LoadXaml<xaml::FrameworkElement>(
        L"<Border " + std::wstring(kXmlns) + L" Height='1' Background='{ThemeResource DividerStrokeColorDefaultBrush}'/>");
    if (line) return line;
    controls::Border fallback;
    fallback.Height(1);
    fallback.Background(BrushResource(L"DividerStrokeColorDefaultBrush"));
    return fallback;
}

XamlRenderer::XamlRenderer(std::string elementId, UiEventSink sink)
    : m_elementId(std::move(elementId)), m_sink(std::move(sink)), m_updating(std::make_shared<int>(0)) {}

XamlRenderer::~XamlRenderer() = default;

xaml::FrameworkElement XamlRenderer::Root() const {
    return m_root ? m_root->outer : nullptr;
}

xaml::FrameworkElement XamlRenderer::Render(RenderNode tree, const StyleProbe& probe, uirender::PatchStats& stats,
                                            bool& rootReplaced) {
    auto next = std::make_unique<RenderNode>(std::move(tree));
    rootReplaced = false;
    UpdateGuard guard(*m_updating);

    bool rebuild = !m_root || !m_root->outer || (m_tree && uirender::LayoutOf(*next) != m_root->layout);
    Patch patch = uirender::Reconcile(rebuild ? nullptr : m_tree.get(), *next);
    stats = uirender::CountPatch(patch);
    if (rebuild || patch.op == PatchOp::Create || patch.op == PatchOp::Replace) {
        m_root = std::make_unique<Mounted>(Build(*next, probe));
        rootReplaced = true;
    } else {
        Apply(*m_root, patch, probe);
    }
    if (m_root->outer) Place(m_root->outer, *next, TypePlacement(*next));
    m_tree = std::move(next);
    return Root();
}

// The native container of stack, list, row, tabs and layer, after uirender::LayoutOf.
static controls::Panel MakeContainer(const RenderNode& node, Layout layout) {
    if (layout == Layout::Panel) {
        controls::StackPanel panel;
        if (node.type == "row" || node.type == "tabs") panel.Orientation(controls::Orientation::Horizontal);
        return panel;
    }
    controls::Grid grid;
    if (layout == Layout::Pair) {
        controls::ColumnDefinition first;
        first.Width(xaml::GridLength{1.0, xaml::GridUnitType::Star});
        controls::ColumnDefinition second;
        second.Width(xaml::GridLength{1.0, xaml::GridUnitType::Auto});
        grid.ColumnDefinitions().Append(first);
        grid.ColumnDefinitions().Append(second);
    }
    return grid;
}

XamlRenderer::Mounted XamlRenderer::Build(const RenderNode& node, const StyleProbe& probe) {
    Mounted mounted;
    mounted.type = node.type;
    mounted.layout = uirender::LayoutOf(node);
    mounted.callbacks = std::make_shared<Callbacks>();
    mounted.callbacks->elementId = m_elementId;
    mounted.callbacks->sink = m_sink;
    mounted.callbacks->updating = m_updating;
    std::shared_ptr<Callbacks> callbacks = mounted.callbacks;
    // A card, picture or icon that answers a click answers Enter and Space too, once Tab reaches it (SetAttributes).
    auto clickOnKeys = [callbacks](const xaml::UIElement& element) {
        element.KeyDown([callbacks](IInspectable const& sender, xaml::Input::KeyRoutedEventArgs const& args) noexcept {
            try {
                auto key = args.Key();
                if (key != winrt::Windows::System::VirtualKey::Enter && key != winrt::Windows::System::VirtualKey::Space) return;
                // Keys bubble up from a text field or a button inside a card: only the element's own focus clicks it.
                if (args.OriginalSource() != sender || callbacks->onClick <= 0) return;
                args.Handled(true);
                callbacks->Fire(true, {});
            } catch (...) {
            }
        });
    };

    xaml::FrameworkElement outer{nullptr};
    xaml::FrameworkElement control{nullptr};

    if (node.type == "stack" || node.type == "list" || node.type == "row" || node.type == "layer" ||
        node.type == "grid" || node.type == "tabs") {
        controls::Panel panel = MakeContainer(node, mounted.layout);
        outer = control = panel;
        mounted.host = panel;
    } else if (node.type == "card") {
        controls::Border card = MakeCardBorder(probe);
        mounted.baseBackground = card.Background();
        mounted.basePadding = card.Padding();
        mounted.baseCorner = card.CornerRadius();
        controls::StackPanel inner;
        inner.Spacing(8);
        controls::StackPanel header;
        header.Spacing(2);
        controls::TextBlock title = MakeText(L"", "bodyStrong", false, &probe);
        controls::TextBlock subtitle = MakeText(L"", "caption", true, &probe);
        header.Children().Append(title);
        header.Children().Append(subtitle);
        controls::StackPanel host;
        host.Spacing(8);
        inner.Children().Append(header);
        inner.Children().Append(host);
        card.Child(inner);
        card.Tapped([callbacks](IInspectable const&, xaml::Input::TappedRoutedEventArgs const&) noexcept {
            callbacks->Fire(true, {});
        });
        clickOnKeys(card);
        // A card that answers a click lights up under the pointer, as the rows of Apple's lists do, and while it has the
        // keyboard's focus, which a Border shows no other way.
        auto resting = std::make_shared<xaml::Media::Brush>(nullptr);
        auto light = [callbacks, resting](IInspectable const& sender) noexcept {
            try {
                auto border = sender.as<controls::Border>();
                auto hover = ThemeResource(L"SubtleFillColorSecondaryBrush").try_as<xaml::Media::Brush>();
                if (callbacks->onClick <= 0 || !hover || *resting) return;
                *resting = border.Background();
                border.Background(hover);
            } catch (...) {
            }
        };
        auto unlight = [resting](IInspectable const& sender) noexcept {
            try {
                if (!*resting) return;
                sender.as<controls::Border>().Background(*resting);
                *resting = nullptr;
            } catch (...) {
            }
        };
        card.PointerEntered([light](IInspectable const& sender, xaml::Input::PointerRoutedEventArgs const&) noexcept { light(sender); });
        card.PointerExited([unlight](IInspectable const& sender, xaml::Input::PointerRoutedEventArgs const&) noexcept { unlight(sender); });
        // Focus bubbles up from a switch or a button inside the card: only the card's own focus lights it.
        card.GotFocus([light](IInspectable const& sender, xaml::RoutedEventArgs const& args) noexcept {
            if (args.OriginalSource() == sender) light(sender);
        });
        card.LostFocus([unlight](IInspectable const& sender, xaml::RoutedEventArgs const& args) noexcept {
            if (args.OriginalSource() == sender) unlight(sender);
        });
        outer = control = card;
        mounted.host = host;
        mounted.label = title;
        mounted.subtitle = subtitle;
    } else if (node.type == "text") {
        controls::TextBlock block = MakeText(L"", node.Text("variant", "body"), node.Text("variant") == "caption", &probe);
        outer = control = block;
        mounted.baseOpacity = block.Opacity();
    } else if (node.type == "button" || node.type == "tab" || node.type == "menu") {
        controls::Button button;
        button.Click([callbacks](IInspectable const&, xaml::RoutedEventArgs const&) noexcept { callbacks->Fire(true, {}); });
        outer = control = button;
    } else if (node.type == "toggle") {
        controls::Grid grid;
        grid.ColumnSpacing(12);
        controls::ColumnDefinition first;
        first.Width(xaml::GridLength{1.0, xaml::GridUnitType::Star});
        controls::ColumnDefinition second;
        second.Width(xaml::GridLength{1.0, xaml::GridUnitType::Auto});
        grid.ColumnDefinitions().Append(first);
        grid.ColumnDefinitions().Append(second);
        controls::TextBlock label = MakeText(L"", "body", false, &probe);
        label.VerticalAlignment(xaml::VerticalAlignment::Center);
        controls::ToggleSwitch toggle;
        if (probe.toggle) toggle.Style(probe.toggle);
        toggle.VerticalAlignment(xaml::VerticalAlignment::Center);
        toggle.HorizontalAlignment(xaml::HorizontalAlignment::Right);
        toggle.MinWidth(0);
        controls::Grid::SetColumn(toggle, 1);
        // The states in Ampify's language (Apple Music's own words in French), not in whatever Windows would pick.
        toggle.OnContent(winrt::box_value(winrt::to_hstring(i18n::Text("ui.on"))));
        toggle.OffContent(winrt::box_value(winrt::to_hstring(i18n::Text("ui.off"))));
        grid.Children().Append(label);
        grid.Children().Append(toggle);
        toggle.Toggled([callbacks](IInspectable const& sender, xaml::RoutedEventArgs const&) noexcept {
            try {
                bool on = sender.as<controls::ToggleSwitch>().IsOn();
                callbacks->Fire(false, on ? "true" : "false");
            } catch (...) {
            }
        });
        outer = grid;
        control = toggle;
        mounted.label = label;
    } else if (node.type == "slider") {
        controls::Slider slider;
        if (probe.slider) slider.Style(probe.slider);
        slider.ValueChanged([callbacks](IInspectable const& sender, primitives::RangeBaseValueChangedEventArgs const& args) noexcept {
            try {
                ShowSliderValue(sender.as<controls::Slider>());
                callbacks->Fire(false, Json(args.NewValue()).Dump());
            } catch (...) {
            }
        });
        outer = control = slider;
    } else if (node.type == "textfield") {
        controls::TextBox box;
        if (probe.textBox) box.Style(probe.textBox);
        box.TextChanged([callbacks](IInspectable const& sender, controls::TextChangedEventArgs const&) noexcept {
            try {
                callbacks->Fire(false, Json(Utf8(sender.as<controls::TextBox>().Text())).Dump());
            } catch (...) {
            }
        });
        outer = control = box;
    } else if (node.type == "dropdown") {
        controls::ComboBox combo;
        if (probe.comboBox) combo.Style(probe.comboBox);
        combo.MinWidth(200);
        combo.SelectionChanged([callbacks](IInspectable const& sender, controls::SelectionChangedEventArgs const&) noexcept {
            try {
                auto item = sender.as<controls::ComboBox>().SelectedItem().try_as<controls::ComboBoxItem>();
                if (!item) return;
                callbacks->Fire(false, Json(Utf8(winrt::unbox_value_or<winrt::hstring>(item.Tag(), L""))).Dump());
            } catch (...) {
            }
        });
        outer = control = combo;
    } else if (node.type == "image") {
        // The picture is the frame's ImageBrush, so cornerRadius rounds it (a Border does not clip an Image child).
        // The invisible Image shares the same bitmap and gives the frame the size the picture would take. The grid
        // around it takes the layer that fades the previous cover out (src "artwork").
        controls::Border frame;
        media::ImageBrush brush;
        controls::Image sizer;
        sizer.Opacity(0);
        controls::Grid layers;
        layers.Children().Append(sizer);
        frame.Child(layers);
        frame.Background(brush);
        frame.Tapped([callbacks](IInspectable const&, xaml::Input::TappedRoutedEventArgs const&) noexcept {
            callbacks->Fire(true, {});
        });
        clickOnKeys(frame);
        outer = control = frame;
        mounted.brush = brush;
        mounted.sizer = sizer;
    } else if (node.type == "lyrics") {
        mounted.lyrics = std::make_shared<LyricsView>();
        mounted.lyrics->pick = [callbacks](int64_t startMs) { callbacks->Fire(false, Json(static_cast<double>(startMs)).Dump()); };
        outer = control = MakeLyricsView(mounted.lyrics);
    } else {  // icon, and option outside a dropdown (refused by ValidateTree, drawn empty here)
        controls::Border frame;  // holds the drawn icon, which is rebuilt when the SVG changes
        frame.Background(media::SolidColorBrush(winrt::Windows::UI::Color{0, 0, 0, 0}));  // so a tap anywhere counts
        frame.Tapped([callbacks](IInspectable const&, xaml::Input::TappedRoutedEventArgs const&) noexcept {
            callbacks->Fire(true, {});
        });
        clickOnKeys(frame);
        outer = control = frame;
    }

    mounted.outer = outer;
    mounted.control = control;
    std::string name = uirender::ElementName(m_elementId, node.id);
    control.Name(Wide(name));
    automation::AutomationProperties::SetAutomationId(control, Wide(name));
    if (outer != control) outer.Name(Wide(name + "_frame"));

    SetAttributes(mounted, node, probe, true);
    if (node.type == "dropdown") {
        FillOptions(mounted, node);
    } else if (node.type == "menu") {
        FillMenu(mounted, node);
    } else if (mounted.host) {
        for (const RenderNode& child : node.children) mounted.children.push_back(Build(child, probe));
        Rewrite(mounted);
        PlaceChildren(mounted, node);
    }
    return mounted;
}

// Sets the cells of a Grid layout (Auto, or Star for a growing child), and the cell and the place of each child. The
// parent decides the defaults: the second child of a pair on the right, the children of a row centred vertically,
// the children of a layer stretched over it.
void XamlRenderer::PlaceChildren(Mounted& mounted, const RenderNode& node) {
    auto grid = mounted.host ? mounted.host.try_as<controls::Grid>() : nullptr;
    bool horizontal = node.type == "row";
    if (mounted.layout == Layout::Columns) SetGridCells(mounted, node);
    if (grid && mounted.layout == Layout::Grid) {
        std::vector<bool> cells;
        for (const RenderNode& child : node.children) cells.push_back(child.Bool("grow", false));
        if (cells != mounted.cells) {
            grid.ColumnDefinitions().Clear();
            grid.RowDefinitions().Clear();
            for (bool grows : cells) {
                xaml::GridLength length{1.0, grows ? xaml::GridUnitType::Star : xaml::GridUnitType::Auto};
                if (horizontal) {
                    controls::ColumnDefinition column;
                    column.Width(length);
                    grid.ColumnDefinitions().Append(column);
                } else {
                    controls::RowDefinition row;
                    row.Height(length);
                    grid.RowDefinitions().Append(row);
                }
            }
            mounted.cells = std::move(cells);
        }
    }
    std::size_t count = std::min(mounted.children.size(), node.children.size());
    for (std::size_t index = 0; index < count; index++) {
        xaml::FrameworkElement element = mounted.children[index].outer;
        if (!element) continue;
        const RenderNode& child = node.children[index];
        Placement placement = TypePlacement(child);
        int cell = static_cast<int>(index);
        if (mounted.layout == Layout::Pair || (mounted.layout == Layout::Grid && horizontal)) {
            if (controls::Grid::GetColumn(element) != cell) controls::Grid::SetColumn(element, cell);
            placement.vertical = xaml::VerticalAlignment::Center;
            if (mounted.layout == Layout::Pair && index == 1) placement.horizontal = xaml::HorizontalAlignment::Right;
            // A growing child takes the space left, a button included.
            if (child.Bool("grow", false)) placement.horizontal = xaml::HorizontalAlignment::Stretch;
        } else if (mounted.layout == Layout::Grid) {
            if (controls::Grid::GetRow(element) != cell) controls::Grid::SetRow(element, cell);
        } else if (mounted.layout == Layout::Layer) {
            placement = Placement{};
        } else if (mounted.layout == Layout::Columns) {
            uirender::GridCell at = uirender::GridCellOf(index, mounted.columns);
            if (controls::Grid::GetRow(element) != at.row) controls::Grid::SetRow(element, at.row);
            if (controls::Grid::GetColumn(element) != at.column) controls::Grid::SetColumn(element, at.column);
            placement.horizontal = xaml::HorizontalAlignment::Stretch;  // a cell takes the width of its column
        }
        Place(element, child, placement);
    }
}

// The cells of a grid: `columns` equal Star columns and one Auto row per line of children, with the gap between them.
void XamlRenderer::SetGridCells(Mounted& mounted, const RenderNode& node) {
    auto grid = mounted.host ? mounted.host.try_as<controls::Grid>() : nullptr;
    if (!grid) return;
    int columns = uirender::GridColumns(node);
    std::size_t lines = (node.children.size() + static_cast<std::size_t>(columns) - 1) / static_cast<std::size_t>(columns);
    int rows = static_cast<int>(lines);
    double gap = node.Number("gap", 8);
    if (grid.ColumnSpacing() != gap) grid.ColumnSpacing(gap);
    if (grid.RowSpacing() != gap) grid.RowSpacing(gap);
    if (columns == mounted.columns && rows == mounted.rows) return;
    grid.ColumnDefinitions().Clear();
    grid.RowDefinitions().Clear();
    for (int i = 0; i < columns; i++) {
        controls::ColumnDefinition column;
        column.Width(xaml::GridLength{1.0, xaml::GridUnitType::Star});
        grid.ColumnDefinitions().Append(column);
    }
    for (int i = 0; i < rows; i++) {
        controls::RowDefinition row;
        row.Height(xaml::GridLength{1.0, xaml::GridUnitType::Auto});
        grid.RowDefinitions().Append(row);
    }
    mounted.columns = columns;
    mounted.rows = rows;
}

// background, padding, cornerRadius and the size of stack, row, layer and card. A card falls back to its own look.
void XamlRenderer::SetBox(Mounted& mounted, const RenderNode& node) {
    xaml::FrameworkElement element = mounted.outer;
    std::string background = node.Text("background");
    if (background != mounted.background) {
        media::Brush brush = background.empty() ? mounted.baseBackground : MakeBackground(background);
        SetPanelBackground(element, brush ? brush : mounted.baseBackground);
        mounted.background = background;
    }
    xaml::Thickness padding = mounted.basePadding;
    if (node.Has("padding")) {
        auto found = node.attributes.find("padding");
        if (std::optional<uirender::Edges> edges = uirender::ParsePadding(found->second)) {
            padding = xaml::Thickness{edges->left, edges->top, edges->right, edges->bottom};
        }
    }
    SetPanelPadding(element, padding);
    xaml::CornerRadius corner = mounted.baseCorner;
    if (node.Has("cornerRadius")) {
        double radius = node.Number("cornerRadius", 0);
        corner = xaml::CornerRadius{radius, radius, radius, radius};
    }
    SetPanelCorner(element, corner);
    double width = node.Number("width", kAuto);
    double height = node.Number("height", kAuto);
    double minWidth = node.Number("minWidth", 0);
    double maxWidth = node.Number("maxWidth", kInfinity);
    if (!SameLength(element.Width(), width)) element.Width(width);
    if (!SameLength(element.Height(), height)) element.Height(height);
    if (element.MinWidth() != minWidth) element.MinWidth(minWidth);
    if (element.MaxWidth() != maxWidth) element.MaxWidth(maxWidth);
}

// The Fluent type ramp's size and weight for a title and a subtitle. Apple Music's resources leave these two styles
// at body size, so a title came out as plain text: the values go on the text itself.
struct RampStep {
    double size;
    std::uint16_t weight;
};

static std::optional<RampStep> RampOf(const std::string& variant) {
    if (variant == "title") return RampStep{28, 600};
    if (variant == "subtitle") return RampStep{20, 600};
    return std::nullopt;
}

// size, weight, color, align, wrap and maxLines of a text. A removed attribute gives the text style's value back.
static void SetTextStyle(const controls::TextBlock& block, const RenderNode& node, bool& colored) {
    std::optional<RampStep> ramp = RampOf(node.Text("variant"));
    if (node.Has("size")) block.FontSize(node.Number("size", 14));
    else if (ramp) block.FontSize(ramp->size);
    else ClearLocal(block, controls::TextBlock::FontSizeProperty());
    if (node.Has("weight")) {
        block.FontWeight(winrt::Windows::UI::Text::FontWeight{static_cast<std::uint16_t>(node.Number("weight", 400))});
    } else if (ramp) {
        block.FontWeight(winrt::Windows::UI::Text::FontWeight{ramp->weight});
    } else {
        ClearLocal(block, controls::TextBlock::FontWeightProperty());
    }
    if (std::optional<uirender::Rgba> color = uirender::ParseColor(node.Text("color"))) {
        block.Foreground(media::SolidColorBrush(ToColor(*color)));
        colored = true;
    } else if (colored) {
        // A caption was made with the secondary brush: it gets it back, the other texts their style's brush.
        if (node.Text("variant") == "caption") block.Foreground(BrushResource(L"TextFillColorSecondaryBrush"));
        else ClearLocal(block, controls::TextBlock::ForegroundProperty());
        colored = false;
    }
    std::string align = node.Text("align");
    if (align == "center") block.TextAlignment(xaml::TextAlignment::Center);
    else if (align == "right") block.TextAlignment(xaml::TextAlignment::Right);
    else if (align == "left") block.TextAlignment(xaml::TextAlignment::Left);
    else ClearLocal(block, controls::TextBlock::TextAlignmentProperty());
    bool wrap = node.Bool("wrap", true);
    xaml::TextWrapping wrapping = wrap ? xaml::TextWrapping::Wrap : xaml::TextWrapping::NoWrap;
    if (block.TextWrapping() != wrapping) block.TextWrapping(wrapping);
    int lines = static_cast<int>(node.Number("maxLines", 0));
    if (block.MaxLines() != lines) block.MaxLines(lines);
    xaml::TextTrimming trimming = lines > 0 || !wrap ? xaml::TextTrimming::CharacterEllipsis : xaml::TextTrimming::None;
    if (block.TextTrimming() != trimming) block.TextTrimming(trimming);
}

// inert: neither the pointer nor Tab reaches the node (BuildRenderTree marks its whole subtree). An element at opacity 0
// still takes clicks in XAML, so a faded control needs this to let them through. Cards, images and icons set their
// own tab stop from it (SetAttributes).
static bool SetInert(const xaml::FrameworkElement& outer, const xaml::FrameworkElement& control, const RenderNode& node) {
    bool inert = node.Bool("inert", false);
    for (const xaml::FrameworkElement& element : {outer, control}) {
        if (element && element.IsHitTestVisible() == inert) element.IsHitTestVisible(!inert);
    }
    const bool ownTabStop = node.type == "card" || node.type == "image" || node.type == "icon";
    if (control && !ownTabStop) {
        if (inert) control.IsTabStop(false);
        else ClearLocal(control, xaml::UIElement::IsTabStopProperty());
    }
    return inert;
}

// opacity, scale and transition, on any node. While building, the values go first so that nothing animates in; on
// an update, the transitions go first so that the new values animate on the compositor thread.
void XamlRenderer::SetMotion(Mounted& mounted, const RenderNode& node, bool building) {
    xaml::FrameworkElement element = mounted.outer;
    auto values = [&] {
        double opacity = node.Number("opacity", 1) * mounted.baseOpacity;
        if (element.Opacity() != opacity) element.Opacity(opacity);
        float scale = static_cast<float>(node.Number("scale", 1));
        winrt::Windows::Foundation::Numerics::float3 wanted{scale, scale, 1};
        if (element.Scale() != wanted) element.Scale(wanted);
        if (scale != 1 && !mounted.centered) {
            // Around the centre: the centre follows the size, which is not known before the first layout.
            mounted.centered = true;
            element.SizeChanged([](IInspectable const& sender, xaml::SizeChangedEventArgs const& args) noexcept {
                try {
                    auto size = args.NewSize();
                    sender.as<xaml::UIElement>().CenterPoint({size.Width / 2, size.Height / 2, 0});
                } catch (...) {
                }
            });
            element.CenterPoint({static_cast<float>(element.ActualWidth() / 2), static_cast<float>(element.ActualHeight() / 2), 0});
        }
    };
    auto transitions = [&] {
        std::string text = node.Text("transition");
        if (text == mounted.transition) return;
        mounted.transition = text;
        uirender::Transitions parsed = uirender::ParseTransition(text).value_or(uirender::Transitions{});
        xaml::ScalarTransition opacity{nullptr};
        if (parsed.opacityMs) {
            opacity = xaml::ScalarTransition();
            opacity.Duration(Milliseconds(*parsed.opacityMs));
        }
        element.OpacityTransition(opacity);
        xaml::Vector3Transition scale{nullptr};
        if (parsed.scaleMs) {
            scale = xaml::Vector3Transition();
            scale.Duration(Milliseconds(*parsed.scaleMs));
        }
        element.ScaleTransition(scale);
        xaml::Vector3Transition translation{nullptr};
        if (parsed.translationMs) {
            translation = xaml::Vector3Transition();
            translation.Duration(Milliseconds(*parsed.translationMs));
        }
        element.TranslationTransition(translation);
    };
    if (building) {
        values();
        transitions();
    } else {
        transitions();
        values();
    }
}

void XamlRenderer::SetAttributes(Mounted& mounted, const RenderNode& node, const StyleProbe& probe, bool building) {
    xaml::FrameworkElement control = mounted.control;
    if (!control) return;
    mounted.callbacks->onClick = static_cast<int>(node.Number("onClick", 0));
    mounted.callbacks->onChange = static_cast<int>(node.Number("onChange", 0));
    bool disabled = node.Bool("disabled", false);
    if (auto asControl = control.try_as<controls::Control>()) {
        if (asControl.IsEnabled() == disabled) asControl.IsEnabled(!disabled);
    }
    if (mounted.outer) SetMotion(mounted, node, building);
    bool inert = SetInert(mounted.outer, control, node);

    const std::string& type = node.type;
    if (type == "card" || type == "image" || type == "icon") {
        bool clickable = mounted.callbacks->onClick > 0 && !disabled && !inert;
        if (control.IsTabStop() != clickable) control.IsTabStop(clickable);
    }
    if (type == "stack" || type == "row" || type == "layer" || type == "card") SetBox(mounted, node);
    if (type == "stack" || type == "list" || type == "row" || type == "tabs") {
        double gap = node.Number("gap", type == "list" || type == "tabs" ? 4 : (type == "row" ? 12 : 8));
        if (auto panel = control.try_as<controls::StackPanel>()) {
            if (panel.Spacing() != gap) panel.Spacing(gap);
        }
        if (auto grid = control.try_as<controls::Grid>()) {
            if (type == "row" && grid.ColumnSpacing() != gap) grid.ColumnSpacing(gap);
            if (type != "row" && grid.RowSpacing() != gap) grid.RowSpacing(gap);
        }
    } else if (type == "card") {
        if (auto title = mounted.label) {
            SetText(title, Wide(node.Text("title")));
            SetVisible(title, node.Has("title"));
        }
        if (auto subtitle = mounted.subtitle) {
            SetText(subtitle, Wide(node.Text("subtitle")));
            SetVisible(subtitle, node.Has("subtitle"));
        }
    } else if (type == "lyrics" && mounted.lyrics) {
        LyricsView& view = *mounted.lyrics;
        double size = node.Number("size", kLyricsSize);
        winrt::Windows::UI::Color color{255, 255, 255, 255};
        if (std::optional<uirender::Rgba> parsed = uirender::ParseColor(node.Text("color"))) color = ToColor(*parsed);
        if (size != view.size || color != view.color) {
            view.size = size;
            view.color = color;
            view.lines.Spacing(size * 0.6);
            view.stale = true;
        }
        SetText(view.placeholder, Wide(node.Text("placeholder")));
        if (building) PaintLyrics(view);
    } else if (type == "text") {
        auto block = control.as<controls::TextBlock>();
        SetText(block, Wide(node.Text("text")));
        xaml::Style style = StyleResource(TextStyleKey(node.Text("variant", "body")));
        if (style && block.Style() != style) block.Style(style);
        SetTextStyle(block, node, mounted.colored);
    } else if (type == "button" || type == "tab" || type == "menu") {
        auto button = control.as<controls::Button>();
        std::string variant = uirender::ButtonVariantOf(node);
        // Apple Music's own button style for every variant: its AccentButtonStyle draws text in a symbol font (the
        // label came out as icons on 1.1792). A primary button takes the accent colours on top instead.
        if (probe.button && button.Style() != probe.button) button.Style(probe.button);
        if (variant == "primary" && !m_primaryTemplate) m_primaryTemplate = PrimaryButtonTemplate();
        if (variant == "primary" && m_primaryTemplate) {
            if (button.Template() != m_primaryTemplate) button.Template(m_primaryTemplate);
        } else {
            button.ClearValue(controls::Control::TemplateProperty());  // a button that was primary before
        }
        if (variant == "tertiary") {
            button.Background(media::SolidColorBrush(winrt::Windows::UI::Color{0, 0, 0, 0}));
            button.BorderThickness(xaml::Thickness{0, 0, 0, 0});
        } else {
            button.ClearValue(controls::Control::BackgroundProperty());  // a button that was tertiary before
            button.ClearValue(controls::Control::BorderThicknessProperty());
        }
        std::string icon = node.Text("icon");
        winrt::hstring label = Wide(node.Text("label"));
        xaml::FrameworkElement drawn =
            MakeIcon(icon, 16, variant == "primary" ? L"TextOnAccentFillColorPrimaryBrush" : L"TextFillColorPrimaryBrush");
        if (drawn) {
            controls::StackPanel content;
            content.Orientation(controls::Orientation::Horizontal);
            content.Spacing(8);
            content.Children().Append(drawn);
            if (!label.empty()) {
                controls::TextBlock text;
                text.Text(label);
                text.VerticalAlignment(xaml::VerticalAlignment::Center);
                content.Children().Append(text);
            }
            button.Content(content);
        } else if (winrt::unbox_value_or<winrt::hstring>(button.Content(), L"") != label || !button.Content()) {
            button.Content(winrt::box_value(label));
        }
    } else if (type == "toggle") {
        auto toggle = control.as<controls::ToggleSwitch>();
        bool checked = node.Bool("checked", node.Bool("value", false));
        if (toggle.IsOn() != checked) {
            mounted.callbacks->echo = checked ? "true" : "false";
            toggle.IsOn(checked);
        }
        if (auto label = mounted.label) {
            SetText(label, Wide(node.Text("label")));
            SetVisible(label, !node.Text("label").empty());
        }
    } else if (type == "slider") {
        auto slider = control.as<controls::Slider>();
        double minimum = node.Number("min", 0);
        double maximum = node.Number("max", 100);
        if (maximum < minimum) maximum = minimum;
        if (slider.Minimum() != minimum) slider.Minimum(minimum);
        if (slider.Maximum() != maximum) slider.Maximum(maximum);
        double step = node.Number("step", 1);
        if (step > 0 && slider.StepFrequency() != step) slider.StepFrequency(step);
        double value = node.Number("value", minimum);
        if (slider.Value() != value) {
            mounted.callbacks->echo = Json(value).Dump();
            slider.Value(value);
        }
        slider.Tag(winrt::box_value(Wide(node.Text("label"))));
        ShowSliderValue(slider);
    } else if (type == "textfield") {
        auto box = control.as<controls::TextBox>();
        winrt::hstring value = Wide(node.Text("value"));
        if (node.Has("value") && box.Text() != value) {
            mounted.callbacks->echo = Json(Utf8(value)).Dump();
            box.Text(value);
        }
        box.PlaceholderText(Wide(node.Text("placeholder")));
        std::string label = node.Text("label");
        box.Header(label.empty() ? nullptr : winrt::box_value(Wide(label)));
    } else if (type == "dropdown") {
        auto combo = control.as<controls::ComboBox>();
        std::string label = node.Text("label");
        combo.Header(label.empty() ? nullptr : winrt::box_value(Wide(label)));
    } else if (type == "image") {
        SetImage(mounted, node);
    } else if (type == "icon") {
        auto frame = control.as<controls::Border>();
        frame.Child(MakeIcon(node.Text("icon"), node.Number("size", 20), L"TextFillColorPrimaryBrush"));
    }

    std::string accessible = AccessibleName(node);
    if (type != "text") automation::AutomationProperties::SetName(control, Wide(accessible));
    std::string tooltip = node.Text("tooltip");
    if (tooltip.empty()) {
        controls::ToolTipService::SetToolTip(control, nullptr);
    } else {
        controls::ToolTipService::SetToolTip(control, winrt::box_value(Wide(tooltip)));
        controls::ToolTipService::SetPlacement(control, TooltipPlacement(node.Text("tooltipPlacement", "top")));
        automation::AutomationProperties::SetHelpText(control, Wide(tooltip));
    }
    // tooltipDelay has no per-control setting in WinUI 3: the system delay applies.
}

// src, fit, cornerRadius, width and height. The bitmap is loaded again only when the address changes, so a fade of
// the image does not reload it. `src: "artwork"` shows the song playing (FollowPlayingArtwork).
void XamlRenderer::SetImage(Mounted& mounted, const RenderNode& node) {
    auto frame = mounted.control.as<controls::Border>();
    std::string source = node.Text("src");
    if (source != mounted.source && mounted.artwork) {
        DropFollow(frame, *mounted.artwork);
        mounted.artwork.reset();
    }
    std::optional<ampify::AppleArtwork> apple = ampify::ParseAppleArtworkSource(source);
    if (source != mounted.source && mounted.brush && mounted.sizer && apple && !apple->queueItem) {
        mounted.source = source;
        mounted.artwork = FollowPlayingArtwork(frame, mounted.brush, mounted.sizer);
    } else if (source != mounted.source && mounted.brush && mounted.sizer && apple) {
        mounted.source = source;
        mounted.brush.ClearValue(media::ImageBrush::ImageSourceProperty());
        mounted.sizer.ClearValue(controls::Image::SourceProperty());
        mounted.artwork = ShowQueueArtwork(mounted.brush, mounted.sizer, *apple->queueItem);
    } else if (source != mounted.source && mounted.brush && mounted.sizer) {
        mounted.source = source;
        mounted.brush.ClearValue(media::ImageBrush::ImageSourceProperty());
        mounted.sizer.ClearValue(controls::Image::SourceProperty());
        media::Imaging::BitmapImage bitmap{nullptr};
        try {
            if (!source.empty()) bitmap = media::Imaging::BitmapImage(ImageUri(source));
        } catch (...) {
            bitmap = nullptr;  // an address that is not a URI: nothing drawn
        }
        mounted.brush.ImageSource(bitmap);
        mounted.sizer.Source(bitmap);
    }
    std::string fit = node.Text("fit");
    media::Stretch stretch = StretchOf(fit);
    if (mounted.brush && mounted.brush.Stretch() != stretch) mounted.brush.Stretch(stretch);
    if (mounted.sizer && mounted.sizer.Stretch() != stretch) mounted.sizer.Stretch(stretch);
    double radius = node.Number("cornerRadius", 0);
    xaml::CornerRadius corner{radius, radius, radius, radius};
    if (frame.CornerRadius() != corner) frame.CornerRadius(corner);
    if (mounted.artwork) {
        mounted.artwork->corner = corner;
        if (ArtworkFade& fade = mounted.artwork->fade; fade.brush && fade.brush.Stretch() != stretch) fade.brush.Stretch(stretch);
    }
    double width = node.Number("width", kAuto);
    double height = node.Number("height", kAuto);
    double maxHeight = node.Has("height") || !fit.empty() ? kInfinity : kImageMaxHeight;
    if (!SameLength(frame.Width(), width)) frame.Width(width);
    if (!SameLength(frame.Height(), height)) frame.Height(height);
    if (frame.MaxHeight() != maxHeight) frame.MaxHeight(maxHeight);
}

void XamlRenderer::FillOptions(Mounted& mounted, const RenderNode& node) {
    auto combo = mounted.control ? mounted.control.try_as<controls::ComboBox>() : nullptr;
    if (!combo) return;
    UpdateGuard guard(*m_updating);
    std::string selected = node.Text("value");
    auto items = combo.Items();
    items.Clear();
    controls::ComboBoxItem chosen{nullptr};
    for (const RenderNode& option : node.children) {
        std::string value = option.Text("value", option.Text("label"));
        std::string label = option.Text("label", value);
        controls::ComboBoxItem item;
        item.Content(winrt::box_value(Wide(label)));
        item.Tag(winrt::box_value(Wide(value)));
        std::string name = uirender::ElementName(m_elementId, option.id);
        item.Name(Wide(name));
        automation::AutomationProperties::SetAutomationId(item, Wide(name));
        automation::AutomationProperties::SetName(item, Wide(label));
        items.Append(item);
        if (value == selected) chosen = item;
    }
    if (chosen && combo.SelectedItem() != chosen) {
        mounted.callbacks->echo = Json(selected).Dump();
        combo.SelectedItem(chosen);
    }
}

// The entries of a menu: one MenuFlyoutItem per item node, each with its own click handler. The flyout opens from the
// menu's button (a Button with a Flyout), and is built again whenever the items change, as the options of a dropdown.
void XamlRenderer::FillMenu(Mounted& mounted, const RenderNode& node) {
    auto button = mounted.control ? mounted.control.try_as<controls::Button>() : nullptr;
    if (!button) return;
    controls::MenuFlyout flyout;
    // A flyout opens above its button by default (Top), over what sits there, such as the tabs above a menu.
    flyout.Placement(primitives::FlyoutPlacementMode::BottomEdgeAlignedLeft);
    for (const RenderNode& entry : node.children) {
        std::string label = entry.Text("label");
        controls::MenuFlyoutItem item;
        item.Text(Wide(label));
        item.IsEnabled(!entry.Bool("disabled", false));
        std::string name = uirender::ElementName(m_elementId, entry.id);
        item.Name(Wide(name));
        automation::AutomationProperties::SetAutomationId(item, Wide(name));
        automation::AutomationProperties::SetName(item, Wide(label));
        auto callbacks = std::make_shared<Callbacks>();
        callbacks->elementId = m_elementId;
        callbacks->sink = m_sink;
        callbacks->updating = m_updating;
        callbacks->onClick = static_cast<int>(entry.Number("onClick", 0));
        item.Click([callbacks](IInspectable const&, xaml::RoutedEventArgs const&) noexcept { callbacks->Fire(true, {}); });
        flyout.Items().Append(item);
    }
    button.Flyout(flyout);
}

void XamlRenderer::Apply(Mounted& mounted, const Patch& patch, const StyleProbe& probe) {
    const RenderNode& node = *patch.node;
    if (patch.op == PatchOp::Update) SetAttributes(mounted, node, probe, false);

    if (node.type == "dropdown" || node.type == "menu") {  // their entries are not children: refilled on a change
        bool entriesChanged = patch.reordered;
        for (const Patch& child : patch.children) entriesChanged = entriesChanged || child.op != PatchOp::Keep;
        // A menu's flyout is rebuilt only for its entries: a redraw would close it while the user has it open.
        if (entriesChanged || (patch.op == PatchOp::Update && node.type == "dropdown")) {
            if (node.type == "menu") FillMenu(mounted, node);
            else FillOptions(mounted, node);
        }
        return;
    }
    if (!mounted.host) return;

    bool rewrite = patch.reordered;
    std::vector<Mounted> next;
    next.reserve(patch.children.size());
    for (const Patch& child : patch.children) {
        bool reuse = child.from >= 0 && child.op != PatchOp::Create && child.op != PatchOp::Replace &&
                     child.from < static_cast<int>(mounted.children.size());
        if (reuse) {
            Mounted& old = mounted.children[child.from];
            // A container whose layout changes (a row of 2 children that gets a third, a child that starts to grow)
            // changes its native panel: rebuilt.
            bool layoutChanged = uirender::LayoutOf(*child.node) != old.layout;
            if (old.outer && !layoutChanged) {
                Apply(old, child, probe);
                next.push_back(std::move(old));
                continue;
            }
        }
        next.push_back(Build(*child.node, probe));
        rewrite = true;
    }
    mounted.children = std::move(next);
    if (rewrite) Rewrite(mounted);
    PlaceChildren(mounted, node);  // a child's align, valign or grow may change without any move
}

void XamlRenderer::Rewrite(Mounted& mounted) {
    controls::Panel host = mounted.host;
    if (!host) return;
    auto children = host.Children();
    children.Clear();
    for (const Mounted& child : mounted.children) {
        if (xaml::FrameworkElement element = child.outer) children.Append(element);
    }
}

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
