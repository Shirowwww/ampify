// AutoMix's mix on Apple's progress line, as on the iPhone (mix_glow.hpp). Ampify lays its own label in the grid of
// Apple's times and its own layer over the line; Apple's elements are only read, and their hand-out visuals are never
// taken (xaml-lessons.md). The glow is a soft shadow the size of the part played, the light a band of white cut from
// the label's letters, both in the compositor's tree of Ampify's layer.

#include "mix_glow.hpp"
#include "xaml_ending.hpp"

#if defined(AMPIFY_WITH_XAML)

#include "audio/audio_sharing.hpp"
#include "win_common.hpp"
#include "xaml_chrome.hpp"

#include "../core/audio/audio_share.hpp"
#include "../core/i18n.hpp"
#include "../core/journal.hpp"

#ifdef GetCurrentTime
#undef GetCurrentTime  // windows.h macro that breaks Storyboard::GetCurrentTime in the projection
#endif

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Numerics.h>
#include <winrt/Windows.UI.h>
#include <winrt/Microsoft.UI.Composition.h>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Hosting.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <limits>
#include <vector>

namespace ampify::win {

namespace {

namespace xaml = winrt::Microsoft::UI::Xaml;
namespace controls = xaml::Controls;
namespace primitives = xaml::Controls::Primitives;
namespace media = xaml::Media;
namespace comp = winrt::Microsoft::UI::Composition;
namespace hosting = xaml::Hosting;
namespace dispatching = winrt::Microsoft::UI::Dispatching;
using winrt::Windows::Foundation::Numerics::float2;
using winrt::Windows::Foundation::Numerics::float3;
using Clock = std::chrono::steady_clock;

constexpr wchar_t kScrubberClass[] = L"AMP.UI.Controls.ScrubberControl";
constexpr wchar_t kTimeName[] = L"CurrentTime";
constexpr wchar_t kPlayedName[] = L"HorizontalDecreaseRect";  // the line's part played, in Apple's template
constexpr int kSearchLimit = 6000;   // elements walked in a window to find the line
constexpr int kPartLimit = 80;       // the line's template holds a few dozen
constexpr int kDisplayLimit = 400;   // the song's display, the line's template included, holds a few hundred
constexpr int kMaxSeenLevels = 40;   // from the line up to the window's root
constexpr wchar_t kArtistName[] = L"ArtistTitleTickerTape";  // the display's "Artist — Album" line
constexpr wchar_t kArtistTextName[] = L"ScrollingText";        // its text, in the ticker's template
constexpr wchar_t kBarName[] = L"TransportBar";  // Apple's play bar, whose edge a theme may draw (Fluent)
constexpr int kSpanAll = 64;         // rows and columns the layer spans: all of the grid's
constexpr float kLabelGap = 2;       // between the line and the label under it
constexpr auto kFollowEvery = std::chrono::milliseconds(200);
constexpr auto kSearchEvery = std::chrono::seconds(3);
constexpr auto kSearchEveryMax = std::chrono::seconds(60);  // the wait after misses doubles up to this
constexpr int64_t kLongestMixMs = 60000;  // a mix heard longer is an audio process that stopped in the middle
constexpr auto kFadeIn = std::chrono::milliseconds(250);
constexpr auto kFadeOut = std::chrono::milliseconds(200);
constexpr double kLabelOpacity = 0.75;    // the word, a little under Apple's times, the light passing over it
constexpr double kArtistOpacity = 1;      // in the artist line's place, as that line is drawn
constexpr float kGlowBlur = 10;           // the glow around the part played, in pixels
constexpr float kGlowOpacity = 0.8f;
constexpr float kLightWidth = 36;         // the light's band, in pixels
constexpr std::uint8_t kLightAlpha = 240;
constexpr auto kLightCycle = std::chrono::milliseconds(2200);  // one pass of the light and its rest
constexpr float kLightPass = 0.55f;       // the share of the cycle the light takes to cross the word

constexpr std::array<WindowRole, 2> kRoles = {WindowRole::Main, WindowRole::Player};

// Ampify's objects on one window's line. Apple's are held weakly; Ampify's strongly (xaml-lessons.md).
struct Spot {
    winrt::weak_ref<controls::Grid> grid;               // Apple's: holds the line and the times
    winrt::weak_ref<xaml::FrameworkElement> scrubber;   // Apple's line
    winrt::weak_ref<xaml::FrameworkElement> played;     // its part played
    winrt::weak_ref<controls::TextBlock> time;          // Apple's current time, which the label dresses like
    winrt::weak_ref<xaml::FrameworkElement> artist;     // the display's artist line, which the label stands in for
    winrt::weak_ref<controls::TextBlock> artistText;    // its text, which the label then dresses like
    winrt::weak_ref<xaml::FrameworkElement> bar;        // Apple's play bar, when the line is in it
    PropertyLedger artistHidden;                       // its opacity as Apple had it, while the label stands there
    controls::TextBlock label{nullptr};
    controls::Canvas layer{nullptr};    // in Apple's grid: the glow
    primitives::Popup popup{nullptr};   // the label, with its light: Apple's bar cuts what passes its foot
    comp::ContainerVisual visuals{nullptr};
    comp::ContainerVisual labelVisuals{nullptr};
    comp::SpriteVisual glow{nullptr};
    comp::DropShadow shadow{nullptr};
    comp::SpriteVisual sheen{nullptr};
    comp::CompositionLinearGradientBrush light{nullptr};
    float2 masked{0, 0};  // the label's size when its letters were taken for the light's mask
    bool shown = false;
    bool lightOn = false;
    bool dressedForArtist = false;  // the label wears the artist line's look (DressForMix)
    Clock::time_point hideAt{};
    Clock::time_point nextSearch{};
    Clock::duration searchWait = kSearchEvery;
};

struct State {
    Journal* journal = nullptr;
    std::array<Spot, kRoles.size()> spots;
    dispatching::DispatcherQueueTimer timer{nullptr};
    bool toldFound = false;
    bool toldShown = false;
};

State& Held() {
    static State* state = new State();  // never destroyed: it holds XAML objects, released with the process
    return *state;
}

template <typename Visit>
void Walk(const xaml::DependencyObject& root, int limit, Visit&& visit) {
    std::vector<xaml::DependencyObject> queue{root};
    for (std::size_t i = 0; i < queue.size() && i < static_cast<std::size_t>(limit); i++) {
        if (!visit(queue[i])) return;
        const int count = media::VisualTreeHelper::GetChildrenCount(queue[i]);
        for (int child = 0; child < count; child++) queue.push_back(media::VisualTreeHelper::GetChild(queue[i], child));
    }
}

xaml::FrameworkElement FindNamed(const xaml::DependencyObject& root, const wchar_t* name, int limit) {
    xaml::FrameworkElement found{nullptr};
    Walk(root, limit, [&](const xaml::DependencyObject& node) {
        auto element = node.try_as<xaml::FrameworkElement>();
        if (element && element.Name() == name) found = element;
        return !found;
    });
    return found;
}

struct Line {
    controls::Grid grid{nullptr};
    xaml::FrameworkElement scrubber{nullptr};
    xaml::FrameworkElement played{nullptr};
    controls::TextBlock time{nullptr};
    xaml::FrameworkElement artist{nullptr};  // the display's artist line: in the line's grid, else in the bar's
    xaml::FrameworkElement bar{nullptr};     // Apple's play bar, when the line is in it
};

xaml::FrameworkElement BarOf(const xaml::FrameworkElement& scrubber) {
    xaml::DependencyObject at = scrubber;
    for (int level = 0; at && level < kMaxSeenLevels; level++) {
        auto element = at.try_as<xaml::FrameworkElement>();
        if (element && element.Name() == kBarName) return element;
        at = media::VisualTreeHelper::GetParent(at);
    }
    return nullptr;
}

// The progress line whose grid also holds Apple's current time, beside the line (a bar layout, the full-screen
// player) or deeper in the song's display, above it (Apple's own bar, the bottom bar): the first found.
Line FindLine(const xaml::DependencyObject& root) {
    Line line;
    Walk(root, kSearchLimit, [&](const xaml::DependencyObject& node) {
        auto scrubber = node.try_as<xaml::FrameworkElement>();
        if (!scrubber || winrt::get_class_name(scrubber) != kScrubberClass) return true;
        auto grid = media::VisualTreeHelper::GetParent(scrubber).try_as<controls::Grid>();
        if (!grid) return true;
        auto time = FindNamed(grid, kTimeName, kDisplayLimit).try_as<controls::TextBlock>();
        auto played = time ? FindNamed(scrubber, kPlayedName, kPartLimit) : nullptr;
        if (!played) return true;
        xaml::FrameworkElement bar = BarOf(scrubber);
        xaml::FrameworkElement artist = FindNamed(grid, kArtistName, kDisplayLimit);
        if (!artist && bar) artist = FindNamed(bar, kArtistName, kDisplayLimit);
        line = Line{grid, scrubber, played, time, artist, bar};
        return false;
    });
    return line;
}

bool Holds(const controls::Panel& panel, const xaml::UIElement& element) {
    std::uint32_t at = 0;
    return panel && element && panel.Children().IndexOf(element, at);
}

void Remove(const controls::Panel& panel, const xaml::UIElement& element) {
    std::uint32_t at = 0;
    if (panel && element && panel.Children().IndexOf(element, at)) panel.Children().RemoveAt(at);
}

void Detach(Spot& spot) noexcept {
    spot.artistHidden.PutBackAll();
    try {
        if (spot.popup) {
            spot.popup.IsOpen(false);
            spot.popup.Child(nullptr);
        }
        if (spot.label) hosting::ElementCompositionPreview::SetElementChildVisual(spot.label, nullptr);
        if (spot.layer) hosting::ElementCompositionPreview::SetElementChildVisual(spot.layer, nullptr);
        if (auto grid = spot.grid.get()) Remove(grid, spot.layer);
    } catch (...) {
        // Apple has torn its grid down: nothing is left to take out.
    }
    Spot fresh;
    fresh.nextSearch = spot.nextSearch;
    fresh.searchWait = spot.searchWait;
    spot = std::move(fresh);
}

// The label takes the look of the Apple text it sits by, as the theme of the moment draws it.
void Dress(const controls::TextBlock& label, const controls::TextBlock& like) {
    label.FontFamily(like.FontFamily());
    label.FontSize(like.FontSize());
    label.FontWeight(like.FontWeight());
    label.CharacterSpacing(like.CharacterSpacing());
    label.Foreground(like.Foreground());
}

// In Ampify's popup, which Place moves. It fades as Windows' animations allow.
controls::TextBlock MakeLabel(const controls::TextBlock& time) {
    controls::TextBlock label;
    label.Name(FromUtf8(kMixLabelName));
    label.Text(FromUtf8(i18n::Text("automix.mixing")));
    Dress(label, time);
    label.IsHitTestVisible(false);
    label.Opacity(0);
    label.Visibility(xaml::Visibility::Collapsed);
    return label;
}

// The glow: a soft shadow of a rectangle the size of the part played (Place keeps it so); the visual draws nothing.
comp::SpriteVisual MakeGlow(const comp::Compositor& compositor, comp::DropShadow& shadow) {
    shadow = compositor.CreateDropShadow();
    shadow.BlurRadius(kGlowBlur);
    shadow.Opacity(kGlowOpacity);
    shadow.Mask(compositor.CreateColorBrush(winrt::Windows::UI::Color{255, 255, 255, 255}));
    comp::SpriteVisual glow = compositor.CreateSpriteVisual();
    glow.Shadow(shadow);
    return glow;
}

// The light: a band of white, moved across the label by the cycle; its mask comes once the label is laid out.
comp::SpriteVisual MakeSheen(const comp::Compositor& compositor, comp::CompositionLinearGradientBrush& light) {
    light = compositor.CreateLinearGradientBrush();
    light.MappingMode(comp::CompositionMappingMode::Absolute);
    light.StartPoint({0, 0});
    light.EndPoint({kLightWidth, 0});
    light.ExtendMode(comp::CompositionGradientExtendMode::Clamp);
    const winrt::Windows::UI::Color clear{0, 255, 255, 255};
    const winrt::Windows::UI::Color white{kLightAlpha, 255, 255, 255};
    light.ColorStops().Append(compositor.CreateColorGradientStop(0, clear));
    light.ColorStops().Append(compositor.CreateColorGradientStop(0.5f, white));
    light.ColorStops().Append(compositor.CreateColorGradientStop(1, clear));
    light.Offset({-kLightWidth, 0});
    return compositor.CreateSpriteVisual();
}

void TakeArtist(Spot& spot, const xaml::FrameworkElement& artist) {
    spot.artist = winrt::make_weak(artist);
    spot.artistText = {};
    if (auto text = FindNamed(artist, kArtistTextName, kPartLimit).try_as<controls::TextBlock>()) {
        spot.artistText = winrt::make_weak(text);
    }
}

// The artist line again when Apple built a new one beside a line it kept: the centred bar holds it outside the
// line's grid, where nothing else tells it was replaced.
void FindArtistAgain(Spot& spot) {
    auto bar = spot.bar.get();
    if (!bar || spot.artist.get()) return;
    auto grid = spot.grid.get();
    xaml::FrameworkElement artist = grid ? FindNamed(grid, kArtistName, kDisplayLimit) : nullptr;
    if (!artist) artist = FindNamed(bar, kArtistName, kDisplayLimit);
    if (!artist) return;
    spot.artistHidden.PutBackAll();  // the one gone, if it was standing aside
    TakeArtist(spot, artist);
}

// Builds everything first, then puts the layer in Apple's grid at the very end: a step that fails leaves nothing
// there. The popup opens with the mix.
void Attach(Spot& spot, const Line& line) {
    controls::TextBlock label = MakeLabel(line.time);
    controls::Canvas layer;
    layer.Name(FromUtf8(kMixLayerName));
    layer.IsHitTestVisible(false);
    controls::Grid::SetRowSpan(layer, kSpanAll);
    controls::Grid::SetColumnSpan(layer, kSpanAll);
    primitives::Popup popup;
    popup.IsHitTestVisible(false);
    popup.XamlRoot(line.grid.XamlRoot());
    popup.Child(label);
    const comp::Compositor compositor = hosting::ElementCompositionPreview::GetElementVisual(layer).Compositor();
    comp::ContainerVisual visuals = compositor.CreateContainerVisual();
    visuals.Opacity(0);
    comp::DropShadow shadow{nullptr};
    comp::SpriteVisual glow = MakeGlow(compositor, shadow);
    visuals.Children().InsertAtTop(glow);
    hosting::ElementCompositionPreview::SetElementChildVisual(layer, visuals);
    comp::CompositionLinearGradientBrush light{nullptr};
    comp::SpriteVisual sheen = MakeSheen(compositor, light);
    comp::ContainerVisual labelVisuals = compositor.CreateContainerVisual();
    labelVisuals.Children().InsertAtTop(sheen);
    hosting::ElementCompositionPreview::SetElementChildVisual(label, labelVisuals);  // over the letters, fading with them
    line.grid.Children().Append(layer);  // last in Apple's grid: over its line
    spot.grid = winrt::make_weak(line.grid);
    spot.scrubber = winrt::make_weak(line.scrubber);
    spot.played = winrt::make_weak(line.played);
    spot.time = winrt::make_weak(line.time);
    if (line.bar) spot.bar = winrt::make_weak(line.bar);
    if (line.artist) TakeArtist(spot, line.artist);
    spot.label = label;
    spot.layer = layer;
    spot.popup = popup;
    spot.visuals = visuals;
    spot.labelVisuals = labelVisuals;
    spot.glow = glow;
    spot.shadow = shadow;
    spot.sheen = sheen;
    spot.light = light;
}

// The label's letters as the light's mask, taken again when its size changed (another language, Apple's font size).
void MaskLight(Spot& spot, float2 size) {
    if (size.x <= 0 || size.y <= 0 || (size.x == spot.masked.x && size.y == spot.masked.y)) return;
    const comp::Compositor compositor = spot.sheen.Compositor();
    comp::CompositionMaskBrush masked = compositor.CreateMaskBrush();
    masked.Source(spot.light);
    masked.Mask(spot.label.GetAlphaMask());
    spot.sheen.Brush(masked);
    spot.masked = size;
    spot.light.StopAnimation(L"Offset");
    spot.lightOn = false;  // the band crosses the new width
}

// An element's bounds in the window's coordinates, where the popup is placed.
winrt::Windows::Foundation::Rect InWindow(const xaml::FrameworkElement& element) {
    return element.TransformToVisual(nullptr).TransformBounds(
        {0, 0, static_cast<float>(element.ActualWidth()), static_cast<float>(element.ActualHeight())});
}

struct LabelSpot {
    winrt::Windows::Foundation::Point at{};
    bool forArtist = false;  // in place of the display's artist line, which steps aside meanwhile
};

// Whether Apple's play bar ends too close under its line for a label as tall as the times: a theme may draw the bar's
// edge there (Fluent's centred bar leaves 10 px), which would cut through the word.
bool NoRoomUnder(const Spot& spot, const xaml::FrameworkElement& scrubber, const xaml::FrameworkElement& time) {
    auto bar = spot.bar.get();
    if (!bar) return false;
    const auto line = InWindow(scrubber);
    const auto frame = InWindow(bar);
    return line.Y + line.Height + kLabelGap + static_cast<float>(time.ActualHeight()) > frame.Y + frame.Height;
}

// The artist line the label stands in for: the one shown when the times stand above the line, in the song's display
// (Apple's own bar, the bottom bar), or when they stand beside it in a bar with no room under it (NoRoomUnder).
xaml::FrameworkElement StoodIn(const Spot& spot, const xaml::FrameworkElement& scrubber,
                               const xaml::FrameworkElement& time) {
    auto artist = spot.artist.get();
    if (!artist || artist.ActualWidth() <= 0) return nullptr;
    const auto line = InWindow(scrubber);
    const auto times = InWindow(time);
    const float middle = times.Y + times.Height / 2;
    if (middle >= line.Y + line.Height) return nullptr;  // the times under the line: the label goes between them
    return middle < line.Y || NoRoomUnder(spot, scrubber, time) ? artist : nullptr;
}

// Where the label goes: centred on the line, between the two times, when Apple puts them under it, as on the iPhone;
// in place of the artist line (StoodIn), as aligned as that line; under the line when the times stand beside it (a
// bar layout with room under its line, the full-screen player), where the window has room for it; else on the
// times' row.
LabelSpot LabelAt(const Spot& spot, const xaml::FrameworkElement& scrubber, const xaml::FrameworkElement& time,
                  float2 size) {
    const auto line = InWindow(scrubber);
    const auto times = InWindow(time);
    const float x = line.X + (line.Width - size.x) / 2;
    const float row = times.Y + (times.Height - size.y) / 2;
    if (times.Y + times.Height / 2 >= line.Y + line.Height) return {{x, row}};
    if (auto artist = StoodIn(spot, scrubber, time)) {
        const auto place = InWindow(artist);
        const float y = place.Y + (place.Height - size.y) / 2;
        switch (artist.HorizontalAlignment()) {
            case xaml::HorizontalAlignment::Left: return {{place.X, y}, true};
            case xaml::HorizontalAlignment::Right: return {{place.X + place.Width - size.x, y}, true};
            default: return {{place.X + (place.Width - size.x) / 2, y}, true};
        }
    }
    const float under = line.Y + line.Height + kLabelGap;
    const auto root = spot.layer.XamlRoot();
    const bool fits = root && under + size.y <= static_cast<float>(root.Size().Height);
    return {fits ? winrt::Windows::Foundation::Point{x, under} : winrt::Windows::Foundation::Point{x, row}};
}

// The artist line steps aside while the label stands in its place, and comes back as Apple had it.
void StandIn(Spot& spot, bool forArtist) {
    auto artist = spot.artist.get();
    if (forArtist && artist && spot.artistHidden.empty()) {
        spot.artistHidden.Write(artist, xaml::UIElement::OpacityProperty(), winrt::box_value(0.0));
    } else if (!forArtist) {
        spot.artistHidden.PutBackAll();
    }
}

// Puts the glow on the part played, relative to Ampify's layer, and the popup's label where LabelAt says.
void Place(Spot& spot) {
    auto played = spot.played.get();
    auto scrubber = spot.scrubber.get();
    auto time = spot.time.get();
    if (!played || !scrubber || !time) return;
    const auto at = played.TransformToVisual(spot.layer).TransformPoint({0, 0});
    spot.glow.Offset(float3{static_cast<float>(at.X), static_cast<float>(at.Y), 0});
    spot.glow.Size(float2{static_cast<float>(played.ActualWidth()), static_cast<float>(played.ActualHeight())});
    if (auto brush = time.Foreground().try_as<media::SolidColorBrush>()) spot.shadow.Color(brush.Color());
    // Its size as dressed now: the layout pass that sets ActualWidth comes after this tick.
    spot.label.Measure({std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity()});
    const float2 size{spot.label.DesiredSize().Width, spot.label.DesiredSize().Height};
    MaskLight(spot, size);
    const LabelSpot word = LabelAt(spot, scrubber, time, size);
    StandIn(spot, word.forArtist && size.x > 0);
    // Written only when it moved: a popup moved is drawn again.
    if (spot.popup.HorizontalOffset() != word.at.X) spot.popup.HorizontalOffset(word.at.X);
    if (spot.popup.VerticalOffset() != word.at.Y) spot.popup.VerticalOffset(word.at.Y);
    spot.sheen.Size(size);
}

void Fade(const comp::Visual& visual, float to, std::chrono::milliseconds length) {
    if (!MotionAllowed()) {
        visual.StopAnimation(L"Opacity");
        visual.Opacity(to);
        return;
    }
    const comp::Compositor compositor = visual.Compositor();
    comp::ScalarKeyFrameAnimation fade = compositor.CreateScalarKeyFrameAnimation();
    fade.InsertExpressionKeyFrame(0, L"this.StartingValue");
    fade.InsertKeyFrame(1, to, compositor.CreateCubicBezierEasingFunction({0.2f, 0}, {0, 1}));
    fade.Duration(length);
    visual.StartAnimation(L"Opacity", fade);
}

// The label's opacity, faded by WinUI itself while Windows' animations are on.
void FadeLabel(const controls::TextBlock& label, double to, std::chrono::milliseconds length) {
    if (MotionAllowed()) {
        xaml::ScalarTransition transition;
        transition.Duration(length);
        label.OpacityTransition(transition);
    } else {
        label.OpacityTransition(nullptr);
    }
    label.Opacity(to);
}

// The light crosses the word, then rests, again and again; off, or stopped, when Windows' animations are.
void FollowLight(Spot& spot) {
    if (!MotionAllowed()) {
        if (spot.lightOn) spot.light.StopAnimation(L"Offset");
        spot.light.Offset({-kLightWidth, 0});
        spot.lightOn = false;
        return;
    }
    if (spot.lightOn || spot.masked.x <= 0) return;
    const comp::Compositor compositor = spot.light.Compositor();
    comp::Vector2KeyFrameAnimation pass = compositor.CreateVector2KeyFrameAnimation();
    pass.InsertKeyFrame(0, {-kLightWidth, 0});
    pass.InsertKeyFrame(kLightPass, {spot.masked.x, 0}, compositor.CreateCubicBezierEasingFunction({0.4f, 0}, {0.2f, 1}));
    pass.InsertKeyFrame(1, {spot.masked.x, 0});
    pass.Duration(kLightCycle);
    pass.IterationBehavior(comp::AnimationIterationBehavior::Forever);
    spot.light.StartAnimation(L"Offset", pass);
    spot.lightOn = true;
}

// Dressed as the artist line it stands in for, else as Apple's current time; the opacity it shows at.
double DressForMix(Spot& spot) {
    auto time = spot.time.get();
    auto scrubber = spot.scrubber.get();
    if (!time || !scrubber) return kLabelOpacity;
    spot.dressedForArtist = StoodIn(spot, scrubber, time) != nullptr;
    auto artistText = spot.artistText.get();
    if (artistText && spot.dressedForArtist) {
        Dress(spot.label, artistText);
        return kArtistOpacity;
    }
    Dress(spot.label, time);
    return kLabelOpacity;
}

// The label takes the look of its new place when that changes while it shows (another bar layout).
void Redress(Spot& spot) {
    auto time = spot.time.get();
    auto scrubber = spot.scrubber.get();
    if (!time || !scrubber) return;
    if ((StoodIn(spot, scrubber, time) != nullptr) != spot.dressedForArtist) {
        FadeLabel(spot.label, DressForMix(spot), kFadeIn);
    }
}

void Follow(State& state, Spot& spot, bool heard) noexcept {
    try {
        if (!spot.label) return;
        if (heard != spot.shown) {
            spot.shown = heard;
            if (heard) {
                const double opacity = DressForMix(spot);
                spot.label.Visibility(xaml::Visibility::Visible);
                spot.popup.IsOpen(true);
                FadeLabel(spot.label, opacity, kFadeIn);
                Fade(spot.visuals, 1, kFadeIn);
                spot.hideAt = {};
                if (!state.toldShown && state.journal) {
                    state.journal->Write("Interface: AutoMix's mix shown on the progress line");
                }
                state.toldShown = true;
            } else {
                FadeLabel(spot.label, 0, kFadeOut);
                Fade(spot.visuals, 0, kFadeOut);
                spot.hideAt = Clock::now();
            }
        }
        if (spot.shown) {
            Redress(spot);
            Place(spot);
            FollowLight(spot);
        } else if (spot.hideAt != Clock::time_point{} && Clock::now() - spot.hideAt >= kFadeOut) {
            spot.label.Visibility(xaml::Visibility::Collapsed);  // out of the screen reader's way too
            spot.popup.IsOpen(false);
            StandIn(spot, false);
            spot.light.StopAnimation(L"Offset");
            spot.lightOn = false;
            spot.hideAt = {};
        }
    } catch (...) {
        // Nothing may escape into Apple Music's XAML frame.
    }
}

// Whether Apple's line is on screen: a theme may hide it, or the controls around it until the pointer comes
// (minimal-fullscreen), and the label must not float there alone.
bool LineSeen(const Spot& spot) {
    xaml::DependencyObject at = spot.scrubber.get();
    for (int level = 0; at && level < kMaxSeenLevels; level++) {
        if (auto element = at.try_as<xaml::UIElement>()) {
            if (element.Visibility() != xaml::Visibility::Visible || element.Opacity() <= 0) return false;
        }
        at = media::VisualTreeHelper::GetParent(at);
    }
    return true;
}

bool MixHeard() {
    audio::SharedAudio* block = SharedAudioBlock();
    if (!block) return false;
    const int64_t since = block->mixHeardSinceMs.load();
    const int64_t now = SharedAudioNowMs();
    return since > 0 && now >= since && now - since < kLongestMixMs;
}

void StartTimer(State& state) {
    if (state.timer) return;
    state.timer = dispatching::DispatcherQueue::GetForCurrentThread().CreateTimer();
    state.timer.Interval(kFollowEvery);
    OnUiTick(state.timer, [](auto const&, auto const&) noexcept {
        State& held = Held();
        const bool heard = MixHeard();
        for (Spot& spot : held.spots) Follow(held, spot, heard && spot.label && LineSeen(spot));
    });
    state.timer.Start();
}

// Whether Ampify's label and layer still sit beside Apple's line: Apple builds a new display, empties a grid, and a
// bar layout can move the line out of it.
bool InPlace(const Spot& spot) {
    auto grid = spot.grid.get();
    auto scrubber = spot.scrubber.get();
    if (!grid || !grid.IsLoaded() || !scrubber || !spot.played.get()) return false;
    if (media::VisualTreeHelper::GetParent(scrubber) != grid) return false;
    return Holds(grid, spot.layer);
}

// The line of one window, found again when Apple built a new one (a window opened again, a new display).
void KeepSpot(State& state, Spot& spot, WindowRole role) {
    if (spot.label && InPlace(spot)) {
        spot.searchWait = kSearchEvery;
        FindArtistAgain(spot);
        return;
    }
    if (spot.label) Detach(spot);
    const Clock::time_point now = Clock::now();
    if (now < spot.nextSearch) return;
    xaml::Window window = AppWindowOf(role);
    auto content = window ? window.Content() : nullptr;
    if (!content) return;  // the full-screen player is not open: looked for as soon as it is
    spot.nextSearch = now + spot.searchWait;
    spot.searchWait = std::min<Clock::duration>(spot.searchWait * 2, kSearchEveryMax);
    const Line line = FindLine(content);
    if (!line.grid) return;
    Attach(spot, line);
    spot.searchWait = kSearchEvery;
    if (!state.toldFound && state.journal) state.journal->Write("Interface: AutoMix's mix will show on the progress line");
    state.toldFound = true;
}

}  // namespace

void KeepMixGlow(Journal& journal) noexcept {
    State& state = Held();
    state.journal = &journal;
    for (std::size_t i = 0; i < kRoles.size(); i++) {
        try {
            KeepSpot(state, state.spots[i], kRoles[i]);
        } catch (...) {
            // Nothing may escape into Apple Music's XAML frame; this window is tried again at the next keep.
        }
    }
    try {
        StartTimer(state);
    } catch (...) {
        // Tried again at the next keep.
    }
}

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
