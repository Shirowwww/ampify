#include "sidebar_hover.hpp"

#if defined(AMPIFY_WITH_XAML)

#include "xaml_host.hpp"

#include "../core/i18n.hpp"
#include "../core/journal.hpp"
#include "../core/sidebar_groups.hpp"
#include "../core/sidebar_hold.hpp"

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.h>
#include <winrt/Windows.UI.ViewManagement.h>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Microsoft.UI.Windowing.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.Media.Animation.h>
#include <winrt/Microsoft.UI.Xaml.Shapes.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ampify::win {

namespace {

namespace xaml = winrt::Microsoft::UI::Xaml;
namespace controls = xaml::Controls;
namespace media = xaml::Media;
namespace animation = xaml::Media::Animation;
namespace shapes = xaml::Shapes;
namespace dispatching = winrt::Microsoft::UI::Dispatching;
using winrt::Windows::Foundation::IInspectable;
using PaneMode = controls::NavigationViewPaneDisplayMode;
using Clock = std::chrono::steady_clock;
using Group = controls::NavigationViewItem;

constexpr int kSearchDepth = 32;  // the NavigationView and its pane sit some twenty levels under the window's content
constexpr auto kWatchEvery = std::chrono::milliseconds(200);  // the real cursor, while the pane is open
constexpr int kTurnMs = 180;                                  // the pin's glyph turns in this time
constexpr wchar_t kPinName[] = L"AMPIFY_SidebarPin";
constexpr double kPinSize = 28;         // the height of Apple's "···" beside it, square
constexpr double kPinGlyphSize = 16;    // the size of Apple's own icons
constexpr double kMoreWidth = 32;       // Apple's "···" before its first layout
constexpr double kHeaderSpacing = 2;    // as between "···" and Ampify's entry in the header

std::atomic<bool> g_enabled{false};
std::atomic<bool> g_folded{false};  // Take done, Release not yet, and not pinned open
std::function<void()> g_changed;    // asks the theme for a pass now: the themes' folded rules follow a pin at once

struct State {
    winrt::weak_ref<controls::NavigationView> navigation;
    winrt::weak_ref<xaml::FrameworkElement> pane;
    winrt::weak_ref<controls::Button> toggle;  // Apple's hamburger: its own toggle, and while pinned it unpins
    HWND window = nullptr;
    PaneMode apple = PaneMode::Auto;  // Apple's own mode, put back when turned off
    IInspectable entered{nullptr};    // the boxed handlers, kept to take them off again
    IInspectable exited{nullptr};
    IInspectable keyed{nullptr};
    winrt::event_token clickToken{};  // the hamburger's Click, taken off by this token in Release
    IInspectable spaceDown{nullptr};  // the hamburger's key handlers, kept to take them off again
    IInspectable spaceUp{nullptr};
    bool spacePressed = false;  // Space went down on the hamburger, and no Click came since
    bool clicked = false;
    dispatching::DispatcherQueueTimer watch{nullptr};
    bool told = false;
    bool pinned = false;  // session only: the pin button keeps the pane open, like Apple's own mode
    ampify::TypingTracker tracker;
    // What Ampify changes in Apple's pane while it holds it; each is put back as Apple had it on Release.
    controls::Button pin{nullptr};  // ours: the pin button in the open pane's header, held strong
    controls::FontIcon pinGlyph{nullptr};
    media::CompositeTransform pinTurn{nullptr};
    controls::Grid pinHeader{nullptr};  // Apple's header grid the pin was added to
    winrt::weak_ref<xaml::FrameworkElement> more;  // Apple's "···", which the pin stands beside
    int64_t moreToken = 0;  // its Margin callback, taken off by this token in Release
    winrt::event_token moreSizeToken{};  // its SizeChanged, taken off by this token in Release
    bool keysUsed = false;  // a key opened the pane or went down in it: it stays while the focus is on it
    controls::Grid fillGrid{nullptr};  // Apple's pane grid, and ours in it: the fill of the pane open over the pages
    controls::Border fill{nullptr};
    animation::Storyboard glyphStory{nullptr};
    shapes::Rectangle dismiss{nullptr};  // the sidebar's SplitView light-dismiss layer
    IInspectable dismissHit{nullptr};    // its local values before Ampify, or the unset value: put back as they were
    IInspectable dismissFill{nullptr};
    int64_t paneToken = 0;  // the IsPaneOpen callback, taken off by this token in Release
    winrt::event_token closingToken{};  // PaneClosing, taken off by this token in Release
    winrt::event_token openingToken{};  // PaneOpening, likewise
    std::vector<xaml::UIElement> openHeader;  // what Apple's header shows in the open pane
    bool repinning = false;  // RepinForWidth closes the pinned pane on purpose
    ampify::GroupMemory<Group> groups;  // the groups collapsed while the pane is folded
};

// Never destroyed: XAML objects are released on the interface thread only, never by a static destructor (a release
// while the process exits, after XAML has shut down, crashed Apple Music on every close).
State& TheState() {
    static State* state = new State();
    return *state;
}

// Depth-first, the first element under `root` that is a T, named `name` when one is given.
template <typename T>
T FindFirst(const xaml::DependencyObject& root, std::wstring_view name, int depth) {
    if (!root || depth > kSearchDepth) return nullptr;
    int count = media::VisualTreeHelper::GetChildrenCount(root);
    for (int i = 0; i < count; i++) {
        xaml::DependencyObject child = media::VisualTreeHelper::GetChild(root, i);
        if (auto found = child.try_as<T>()) {
            auto element = child.try_as<xaml::FrameworkElement>();
            if (name.empty() || (element && element.Name() == name)) return found;
        }
        if (T deeper = FindFirst<T>(child, name, depth + 1)) return deeper;
    }
    return nullptr;
}

// The window under the cursor is Apple Music's, or one it owns: WinUI draws popups in windows of their own, and after a
// tap on Alt its access-key layer (PopupWindowSiteBridge) lies over the column, which then counted as off the pane.
bool OverWindow(const State& state, HWND under) {
    return GetAncestor(under, GA_ROOT) == state.window || GetAncestor(under, GA_ROOTOWNER) == state.window;
}

// The real cursor in `element`'s own coordinates, while it is over Apple Music's window.
std::optional<winrt::Windows::Foundation::Point> CursorIn(const State& state, const xaml::UIElement& element) {
    POINT cursor{};
    if (!state.window || !GetCursorPos(&cursor)) return std::nullopt;
    if (!OverWindow(state, WindowFromPoint(cursor)) || !ScreenToClient(state.window, &cursor)) return std::nullopt;
    xaml::XamlRoot root = element.XamlRoot();
    double scale = root ? root.RasterizationScale() : 1.0;
    // Where one step along each axis lands too: a mirrored element (Mirror's pane on the right) counts from its right
    // edge, so its origin alone put every point inside it at a negative X, and the pane closed under the pointer.
    media::GeneralTransform transform = element.TransformToVisual(nullptr);
    winrt::Windows::Foundation::Point origin = transform.TransformPoint({0, 0});
    winrt::Windows::Foundation::Point right = transform.TransformPoint({1, 0});
    winrt::Windows::Foundation::Point down = transform.TransformPoint({0, 1});
    auto x = ampify::LocalOffset(cursor.x / scale, origin.X, right.X);
    auto y = ampify::LocalOffset(cursor.y / scale, origin.Y, down.Y);
    if (!x || !y) return std::nullopt;
    return winrt::Windows::Foundation::Point{static_cast<float>(*x), static_cast<float>(*y)};
}

// The width the pane shows: the open sidebar, or the folded column (its layout box stays 290 px wide).
bool CursorOverPane(const State& state, const controls::NavigationView& navigation, const xaml::FrameworkElement& pane) {
    auto at = CursorIn(state, pane);
    double width = navigation.IsPaneOpen() ? navigation.OpenPaneLength() : navigation.CompactPaneLength();
    return at && at->X >= 0 && at->Y >= 0 && at->X < width && at->Y < pane.ActualHeight();
}

bool CursorOver(const State& state, const xaml::FrameworkElement& element) {
    auto at = CursorIn(state, element);
    return at && at->X >= 0 && at->Y >= 0 && at->X < element.ActualWidth() && at->Y < element.ActualHeight();
}

// A menu or flyout (a playlist's menu, the account's) is a light-dismiss popup; tooltips and Ampify's panels are not.
bool MenuOpen(const xaml::UIElement& element) {
    if (!element.XamlRoot()) return false;
    for (const auto& popup : media::VisualTreeHelper::GetOpenPopupsForXamlRoot(element.XamlRoot())) {
        if (popup.IsLightDismissEnabled()) return true;
    }
    return false;
}

// True while the keyboard is in the pane (its search field or one of its buttons).
bool FocusInside(const xaml::FrameworkElement& pane) {
    if (!pane.XamlRoot()) return false;
    xaml::DependencyObject at = xaml::Input::FocusManager::GetFocusedElement(pane.XamlRoot()).try_as<xaml::DependencyObject>();
    for (; at; at = media::VisualTreeHelper::GetParent(at)) {
        if (at == pane) return true;
    }
    return false;
}

bool WindowInFront(const State& state) {
    return state.window && GetForegroundWindow() == state.window;
}

// Notes the search text while the keyboard is in the field, and holds the pane for typing (see HoldsForTyping).
bool TypingHolds(State& state, const xaml::FrameworkElement& pane) {
    if (!FocusInside(pane)) {
        state.tracker.Forget();
        return false;
    }
    auto field = xaml::Input::FocusManager::GetFocusedElement(pane.XamlRoot()).try_as<controls::TextBox>();
    if (field) state.tracker.Look(field.Text().c_str(), Clock::now());
    return HoldsForTyping(true, WindowInFront(state), state.tracker.SinceTyped(Clock::now()));
}

// The keyboard uses the pane (see HoldsForKeyboard): opened from the hamburger with Enter, the pointer is elsewhere.
bool KeyboardHolds(const State& state, const xaml::FrameworkElement& pane) {
    if (!state.keysUsed || !pane.XamlRoot()) return false;
    auto focused = xaml::Input::FocusManager::GetFocusedElement(pane.XamlRoot()).try_as<xaml::DependencyObject>();
    const bool onToggle = focused && state.toggle.get() == focused;
    return ampify::HoldsForKeyboard(WindowInFront(state), state.keysUsed, FocusInside(pane) || onToggle);
}

// The keyboard goes to the hamburger, or to the pin while pinned. Not the NavigationView itself: it hands focus to its
// search field, and Apple opens its search page as soon as that field gets the focus.
void ParkFocus(const State& state, const xaml::FrameworkElement& pane) {
    if (!FocusInside(pane)) return;
    auto toggle = state.toggle.get();
    if (toggle && toggle.Focus(xaml::FocusState::Programmatic)) return;
    // Pinned open, Apple's template hides the hamburger: the pin, in the open header, takes the keyboard instead.
    if (state.pin) state.pin.Focus(xaml::FocusState::Programmatic);
}

// Closes the pane, the keyboard parked first so a collapsed field does not reopen it.
void ClosePane(const State& state, const controls::NavigationView& navigation, const xaml::FrameworkElement& pane) {
    ParkFocus(state, pane);
    navigation.IsPaneOpen(false);
}

// Closes the pane once the pointer is off it and nothing holds it; true while it stays open. Pinned, it never closes.
// Every hold is read on each look, so the search text stays current even while the pointer is over the pane.
bool CloseIfLeft(State& state) {
    auto navigation = state.navigation.get();
    auto pane = state.pane.get();
    if (!navigation || !pane || !navigation.IsPaneOpen() || state.pinned) return false;
    bool cursor = CursorOverPane(state, navigation, pane);
    bool typing = TypingHolds(state, pane);
    bool menu = HoldsForMenu(WindowInFront(state), MenuOpen(pane));
    if (cursor || typing || menu || KeyboardHolds(state, pane)) return true;
    ClosePane(state, navigation, pane);
    return false;
}

void Watch(State& state) {
    if (!state.watch) {
        state.watch = dispatching::DispatcherQueue::GetForCurrentThread().CreateTimer();
        state.watch.Interval(kWatchEvery);
        state.watch.Tick([](auto const&, auto const&) noexcept {
            try {
                State& live = TheState();
                if (!CloseIfLeft(live)) live.watch.Stop();
            } catch (...) {
            }
        });
    }
    if (!state.watch.IsRunning()) state.watch.Start();
}

bool AnimationsOn() {
    try {
        return winrt::Windows::UI::ViewManagement::UISettings().AnimationsEnabled();
    } catch (...) {
        return false;
    }
}

// A quarter turn into place while the element fades in: the pin's glyph, as it changes.
animation::Storyboard EaseTurn(const media::CompositeTransform& turn, const xaml::UIElement& element) {
    animation::Storyboard story;
    animation::CubicEase spinEase;
    spinEase.EasingMode(animation::EasingMode::EaseOut);
    animation::CubicEase fadeEase;
    fadeEase.EasingMode(animation::EasingMode::EaseOut);
    auto duration = xaml::DurationHelper::FromTimeSpan(
        std::chrono::duration_cast<winrt::Windows::Foundation::TimeSpan>(std::chrono::milliseconds(kTurnMs)));
    animation::DoubleAnimation spin;
    spin.From(-90.0);
    spin.To(0.0);
    spin.Duration(duration);
    spin.EasingFunction(spinEase);
    animation::Storyboard::SetTarget(spin, turn);
    animation::Storyboard::SetTargetProperty(spin, L"Rotation");
    animation::DoubleAnimation fade;
    fade.From(0.0);
    fade.To(1.0);
    fade.Duration(duration);
    fade.EasingFunction(fadeEase);
    animation::Storyboard::SetTarget(fade, element);
    animation::Storyboard::SetTargetProperty(fade, L"Opacity");
    story.Children().Append(spin);
    story.Children().Append(fade);
    return story;
}

// Apple's "···" in its header grid (MenuButton#MoreButton), which the pin stands beside.
xaml::FrameworkElement FindMore(const controls::Grid& header) {
    for (const xaml::UIElement& child : header.Children()) {
        auto element = child.try_as<xaml::FrameworkElement>();
        if (element && element.Name() == L"MoreButton") return element;
    }
    return nullptr;
}

// Just left of "···", in its cell and level with it. Read again whenever "···" moves: a theme's @media writes its
// margin as the pane is pinned or the window changes size.
void PlacePin(const State& state) {
    auto more = state.more.get();
    if (!state.pin || !more) return;
    xaml::Thickness margin = more.Margin();
    double width = more.ActualWidth() > 0 ? more.ActualWidth() : kMoreWidth;
    xaml::Thickness wanted{0, margin.Top, ampify::LeftOfMoreMargin(margin.Right, width, kHeaderSpacing), margin.Bottom};
    if (state.pin.Margin() != wanted) state.pin.Margin(wanted);
    controls::Grid::SetRow(state.pin, controls::Grid::GetRow(more));
    controls::Grid::SetColumn(state.pin, controls::Grid::GetColumn(more));
    state.pin.VerticalAlignment(more.VerticalAlignment());
}

// A square flat button with Fluent's subtle hover, the corners of Apple's "···" (4, well under half its height).
controls::Button MakePinButton() {
    const std::wstring brushes =
        L"<StaticResource x:Key='ButtonBackground' ResourceKey='SubtleFillColorTransparentBrush'/>"
        L"<StaticResource x:Key='ButtonBackgroundPointerOver' ResourceKey='SubtleFillColorSecondaryBrush'/>"
        L"<StaticResource x:Key='ButtonBackgroundPressed' ResourceKey='SubtleFillColorTertiaryBrush'/>"
        L"<StaticResource x:Key='ButtonBorderBrush' ResourceKey='SubtleFillColorTransparentBrush'/>"
        L"<StaticResource x:Key='ButtonBorderBrushPointerOver' ResourceKey='SubtleFillColorTransparentBrush'/>"
        L"<StaticResource x:Key='ButtonBorderBrushPressed' ResourceKey='SubtleFillColorTransparentBrush'/>"
        L"<StaticResource x:Key='ButtonForegroundPointerOver' ResourceKey='TextFillColorPrimaryBrush'/>"
        L"<StaticResource x:Key='ButtonForegroundPressed' ResourceKey='TextFillColorSecondaryBrush'/>";
    const std::wstring text =
        L"<Button xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation' "
        L"xmlns:x='http://schemas.microsoft.com/winfx/2006/xaml' HorizontalAlignment='Right' "
        L"VerticalAlignment='Center' MinWidth='0' MinHeight='0' Padding='0' CornerRadius='4' BorderThickness='0'>"
        L"<Button.Resources><ResourceDictionary><ResourceDictionary.ThemeDictionaries>"
        L"<ResourceDictionary x:Key='Default'>" + brushes + L"</ResourceDictionary>"
        L"<ResourceDictionary x:Key='Light'>" + brushes + L"</ResourceDictionary>"
        L"</ResourceDictionary.ThemeDictionaries></ResourceDictionary></Button.Resources></Button>";
    controls::Button button = xaml::Markup::XamlReader::Load(text).as<controls::Button>();
    button.Name(kPinName);
    button.Width(kPinSize);
    button.Height(kPinSize);
    return button;
}

controls::FontIcon MakePinGlyph(media::CompositeTransform& turn) {
    controls::FontIcon glyph;
    glyph.FontFamily(media::FontFamily(L"Segoe Fluent Icons, Segoe MDL2 Assets"));
    glyph.FontSize(kPinGlyphSize);
    glyph.HorizontalAlignment(xaml::HorizontalAlignment::Center);
    glyph.VerticalAlignment(xaml::VerticalAlignment::Center);
    turn = media::CompositeTransform();
    glyph.RenderTransformOrigin(winrt::Windows::Foundation::Point{0.5f, 0.5f});
    glyph.RenderTransform(turn);
    return glyph;
}

// The pin shows only while the pane is open, with the glyph and the words of the pane's state, in the colour of "···".
// A change of state turns the glyph once, unless the system animations are off.
void ShowPin(State& state, bool animate) {
    auto navigation = state.navigation.get();
    if (!state.pin || !navigation) return;
    const ampify::PinFace face = ampify::PinButtonFace(state.pinned);
    const winrt::hstring label = winrt::to_hstring(i18n::Text(face.label));
    if (state.pinGlyph.Glyph() != face.glyph) state.pinGlyph.Glyph(face.glyph);
    xaml::Automation::AutomationProperties::SetName(state.pin, label);
    controls::ToolTipService::SetToolTip(state.pin, winrt::box_value(label));
    auto more = state.more.get();
    if (auto control = more ? more.try_as<controls::Control>() : nullptr) state.pinGlyph.Foreground(control.Foreground());
    state.pin.Visibility(navigation.IsPaneOpen() ? xaml::Visibility::Visible : xaml::Visibility::Collapsed);
    PlacePin(state);
    if (!animate || !AnimationsOn()) return;
    if (state.glyphStory) state.glyphStory.Stop();
    state.glyphStory = EaseTurn(state.pinTurn, state.pinGlyph);
    state.glyphStory.Begin();
}

// The pin taken out of Apple's header, and the watch on "···" taken off.
void RemovePin(State& state) {
    if (state.glyphStory) state.glyphStory.Stop();
    state.glyphStory = nullptr;
    if (auto more = state.more.get(); more && state.moreToken != 0) {
        more.UnregisterPropertyChangedCallback(xaml::FrameworkElement::MarginProperty(), state.moreToken);
        if (state.moreSizeToken.value != 0) more.SizeChanged(state.moreSizeToken);
    }
    state.moreSizeToken = {};
    uint32_t index = 0;
    if (state.pinHeader && state.pin && state.pinHeader.Children().IndexOf(state.pin, index)) {
        state.pinHeader.Children().RemoveAt(index);
    }
    state.moreToken = 0;
    state.more = nullptr;
    state.pin = nullptr;
    state.pinGlyph = nullptr;
    state.pinTurn = nullptr;
    state.pinHeader = nullptr;
}

// The light-dismiss layer of the sidebar's SplitView, searched below it but never inside another SplitView (the pages').
shapes::Rectangle FindOwnDismissLayer(const xaml::DependencyObject& root, int depth) {
    if (!root || depth > kSearchDepth) return nullptr;
    int count = media::VisualTreeHelper::GetChildrenCount(root);
    for (int i = 0; i < count; i++) {
        xaml::DependencyObject child = media::VisualTreeHelper::GetChild(root, i);
        if (auto layer = child.try_as<shapes::Rectangle>(); layer && layer.Name() == L"LightDismissLayer") return layer;
        if (child.try_as<controls::SplitView>()) continue;
        if (auto deeper = FindOwnDismissLayer(child, depth + 1)) return deeper;
    }
    return nullptr;
}

shapes::Rectangle FindDismissLayer(const controls::NavigationView& navigation) {
    auto split = FindFirst<controls::SplitView>(navigation, L"RootSplitView", 0);
    return split ? FindOwnDismissLayer(split, 0) : nullptr;
}

// Puts a property back as it was: its local value, or none, so the template's or the style's value applies again.
void PutBack(const xaml::DependencyObject& element, const xaml::DependencyProperty& property, const IInspectable& local) {
    if (!local || local == xaml::DependencyProperty::UnsetValue()) {
        element.ClearValue(property);
    } else {
        element.SetValue(property, local);
    }
}

void RestoreDismissLayer(State& state) {
    if (!state.dismiss) return;
    PutBack(state.dismiss, xaml::UIElement::IsHitTestVisibleProperty(), state.dismissHit);
    PutBack(state.dismiss, shapes::Shape::FillProperty(), state.dismissFill);
    state.dismiss = nullptr;
    state.dismissHit = nullptr;
    state.dismissFill = nullptr;
}

// While the pane is folded, the layer must not take the first click on the pages, and its veil stays clear.
void SetDismissLayer(State& state, const controls::NavigationView& navigation, bool hover) {
    if (!hover) {
        RestoreDismissLayer(state);
        return;
    }
    if (!state.dismiss) {
        state.dismiss = FindDismissLayer(navigation);
        if (!state.dismiss) return;
        state.dismissHit = state.dismiss.ReadLocalValue(xaml::UIElement::IsHitTestVisibleProperty());
        state.dismissFill = state.dismiss.ReadLocalValue(shapes::Shape::FillProperty());
    }
    state.dismiss.IsHitTestVisible(false);
    state.dismiss.Fill(media::SolidColorBrush(winrt::Windows::UI::Colors::Transparent()));
}

// A brush that shows: a theme's acrylic or image, or a colour that is not see-through.
bool Paints(const media::Brush& brush) {
    if (!brush || brush.Opacity() <= 0) return false;
    auto solid = brush.try_as<media::SolidColorBrush>();
    return !solid || solid.Color().A > 0;
}

void RemoveFill(State& state) {
    uint32_t index = 0;
    if (state.fillGrid && state.fill && state.fillGrid.Children().IndexOf(state.fill, index)) {
        state.fillGrid.Children().RemoveAt(index);
    }
    state.fillGrid = nullptr;
    state.fill = nullptr;
}

// Ours, behind everything in Apple's pane grid and over all its rows: the in-app acrylic of Windows' menus.
controls::Border MakeFill(const controls::Grid& grid) {
    auto resources = xaml::Application::Current().Resources();
    auto key = winrt::box_value(L"AcrylicInAppFillColorDefaultBrush");
    auto brush = resources.HasKey(key) ? resources.Lookup(key).try_as<media::Brush>() : nullptr;
    if (!brush) return nullptr;
    controls::Border fill;
    fill.Background(brush);
    fill.IsHitTestVisible(false);
    controls::Grid::SetRowSpan(fill, std::max<int32_t>(1, static_cast<int32_t>(grid.RowDefinitions().Size())));
    controls::Grid::SetColumnSpan(fill, std::max<int32_t>(1, static_cast<int32_t>(grid.ColumnDefinitions().Size())));
    grid.Children().InsertAt(0, fill);
    return fill;
}

// Apple leaves its pane see-through over its own window backdrop, and so does a theme that sets the pane on the
// window's material (Fluent): open over the pages, they showed through its search box and menu, and the pin sat on the
// Play button. While it is open over them and nothing paints it, a layer of Ampify's own fills it; a theme's
// background on Apple's grids would be written again by the theme.
void FillOverPages(State& state, const controls::NavigationView& navigation, bool over) {
    auto pane = state.pane.get();
    auto grid = state.fillGrid ? state.fillGrid : FindFirst<controls::Grid>(navigation, L"PaneContentGrid", 0);
    if (!pane || !grid) return;
    auto panel = pane.try_as<controls::Panel>();
    const bool painted = Paints(grid.Background()) || (panel && Paints(panel.Background()));
    if (!state.fill && over && !painted) {
        state.fill = MakeFill(grid);
        state.fillGrid = state.fill ? grid : nullptr;
    }
    if (!state.fill) return;
    state.fill.CornerRadius(grid.CornerRadius());
    state.fill.Visibility(over && !painted ? xaml::Visibility::Visible : xaml::Visibility::Collapsed);
}

// A group has children: realized ones, a MenuItems list, or an items source that the list fills in later.
bool HasChildren(const Group& item) {
    return item.HasUnrealizedChildren() || item.MenuItems().Size() > 0 || item.MenuItemsSource() != nullptr;
}

// Apple's groups: the items of its own sidebar list that have children (Library, Playlists), with whether each is open.
std::vector<std::pair<Group, bool>> SidebarGroups(const controls::NavigationView& navigation) {
    std::vector<std::pair<Group, bool>> groups;
    auto host = FindFirst<controls::ItemsRepeater>(navigation, L"MenuItemsHost", 0);
    int count = host ? media::VisualTreeHelper::GetChildrenCount(host) : 0;
    for (int i = 0; i < count; i++) {
        auto item = media::VisualTreeHelper::GetChild(host, i).try_as<Group>();
        if (item && HasChildren(item)) groups.emplace_back(item, item.IsExpanded());
    }
    return groups;
}

// Folded: the open groups collapse, and the ones collapsed here are remembered, so the same ones open again.
void FoldGroups(State& state, const controls::NavigationView& navigation) {
    for (const Group& group : state.groups.Fold(SidebarGroups(navigation))) {
        try {
            group.IsExpanded(false);
        } catch (...) {
        }
    }
}

void UnfoldGroups(State& state) {
    for (const Group& group : state.groups.Unfold()) {
        try {
            group.IsExpanded(true);
        } catch (...) {
        }
    }
}

void SyncGroups(State& state, const controls::NavigationView& navigation) {
    if (navigation.IsPaneOpen()) {
        UnfoldGroups(state);
    } else {
        FoldGroups(state, navigation);
    }
}

bool PinPushes(const controls::NavigationView& navigation);

// The pane stands over the pages: open under the pointer, or pinned in a window too narrow to push them.
bool OverPages(const State& state, const controls::NavigationView& navigation) {
    return navigation.IsPaneOpen() && (!state.pinned || !PinPushes(navigation));
}

// Follows IsPaneOpen whoever changes it (the hover, the hamburger, Apple), so the groups, the pin and the fill open and
// fold with the pane. Opened by the hamburger, the pane closes again once the pointer is off it, as after a hover.
void WatchPaneOpen(State& state, const controls::NavigationView& navigation) {
    auto changed = [](xaml::DependencyObject const&, xaml::DependencyProperty const&) noexcept {
        try {
            State& live = TheState();
            auto current = live.navigation.get();
            if (!current) return;
            SyncGroups(live, current);
            ShowPin(live, false);
            FillOverPages(live, current, OverPages(live, current));
            if (!current.IsPaneOpen()) live.keysUsed = false;
            if (current.IsPaneOpen() && !live.pinned) Watch(live);
        } catch (...) {
        }
    };
    state.paneToken = navigation.RegisterPropertyChangedCallback(controls::NavigationView::IsPaneOpenProperty(), changed);
}

// The header's elements Apple shows in an open pane (its PaneOpening handler runs before Ampify's).
void NoteOpenHeader(State& state, const controls::NavigationView& navigation) {
    state.openHeader.clear();
    auto header = navigation.PaneHeader().try_as<controls::Panel>();
    if (!header) return;
    for (const xaml::UIElement& child : header.Children()) {
        if (child.Visibility() == xaml::Visibility::Visible) state.openHeader.push_back(child);
    }
}

// Apple's PaneClosing handler hides the title and "···" before Ampify refuses the close: they show again. Only shown,
// never hidden: Ampify's pin shows after the opening, and hiding it under the keyboard sent the focus to the search.
void RestoreOpenHeader(const State& state) {
    for (const xaml::UIElement& element : state.openHeader) {
        if (element.Visibility() != xaml::Visibility::Visible) element.Visibility(xaml::Visibility::Visible);
    }
}

void ShowIfCollapsed(const xaml::UIElement& element) {
    if (element && element.Visibility() == xaml::Visibility::Collapsed) element.Visibility(xaml::Visibility::Visible);
}

// WinUI turns the NavigationView's IsPaneOpen off before its SplitView asks, so a refused close left the pane open
// but laid out closed (a squashed column), and Apple's PaneClosing handler collapsed the header's title and "···",
// which only its PaneOpening shows again. Once every handler has run, the pane is open again and so are they.
void ReopenAfterRefusedClose() {
    auto queue = dispatching::DispatcherQueue::GetForCurrentThread();
    if (!queue) return;
    queue.TryEnqueue(dispatching::DispatcherQueueHandler([]() noexcept {
        try {
            const State& live = TheState();
            auto navigation = live.navigation.get();
            if (!live.pinned || live.repinning || !navigation) return;
            if (!navigation.IsPaneOpen()) navigation.IsPaneOpen(true);
            RestoreOpenHeader(live);
            if (!live.pinHeader) return;
            ShowIfCollapsed(FindFirst<controls::TextBlock>(live.pinHeader, L"AppName", 0));
            ShowIfCollapsed(live.more.get());
        } catch (...) {
        }
    }));
}

// Over narrow pages the pinned pane is LeftCompact, which WinUI closes itself after a click on an item: refused while
// pinned, rather than reopened at the next tick. The pin button and the hamburger unpin first, and ApplyPin closes it.
// WinUI raises PaneClosing late, after a pin that switched the mode has opened the pane again: refused as well.
void KeepPinnedOpen(State& state, const controls::NavigationView& navigation) {
    state.openingToken = navigation.PaneOpening([](controls::NavigationView const& sender, IInspectable const&) noexcept {
        try {
            NoteOpenHeader(TheState(), sender);
        } catch (...) {
        }
    });
    state.closingToken = navigation.PaneClosing(
        [](controls::NavigationView const&, controls::NavigationViewPaneClosingEventArgs const& args) noexcept {
            try {
                const State& live = TheState();
                if (!live.pinned || live.repinning) return;
                args.Cancel(true);
                RestoreOpenHeader(live);
                ReopenAfterRefusedClose();
            } catch (...) {
            }
        });
}

void AddPin(State& state, const controls::NavigationView& navigation);

// What the hover needs from Apple's pane once taken: the pin, the dismiss layer, the groups and the pane watch.
void TakeChrome(State& state, const controls::NavigationView& navigation) {
    AddPin(state, navigation);
    SetDismissLayer(state, navigation, !state.pinned);
    WatchPaneOpen(state, navigation);
    KeepPinnedOpen(state, navigation);
    SyncGroups(state, navigation);
}

// Puts back everything Ampify changed in Apple's pane. Apple's own groups open again, the way they were.
void ReleaseChrome(State& state) {
    try {
        if (auto navigation = state.navigation.get(); navigation && state.paneToken != 0) {
            navigation.UnregisterPropertyChangedCallback(controls::NavigationView::IsPaneOpenProperty(), state.paneToken);
        }
        if (auto navigation = state.navigation.get(); navigation && state.closingToken.value != 0) {
            navigation.PaneClosing(state.closingToken);
        }
        if (auto navigation = state.navigation.get(); navigation && state.openingToken.value != 0) {
            navigation.PaneOpening(state.openingToken);
        }
    } catch (...) {
    }
    state.paneToken = 0;
    state.closingToken = {};
    state.openingToken = {};
    state.openHeader.clear();
    try {
        RemovePin(state);
        RestoreDismissLayer(state);
        RemoveFill(state);
    } catch (...) {
    }
    state.keysUsed = false;
}

bool PinPushes(const controls::NavigationView& navigation) {
    xaml::XamlRoot root = navigation.XamlRoot();
    return root && ampify::PinPushesPages(root.Size().Width);
}

// Left, not Apple's Auto: Auto reads the NavigationView's own width, narrower than the window under a theme with side
// panels (1171 of 1500 under Glass), and stays an overlay below 1325 instead of pushing the pages. Where the window
// itself is that narrow, the pinned pane stays over the pages (PinPushesPages).
PaneMode PinnedMode(const controls::NavigationView& navigation) {
    return PinPushes(navigation) ? PaneMode::Left : PaneMode::LeftCompact;
}

// Apple lays out the pane's header (title, "···", hamburger row) when the pane opens, for the mode it opens in: a mode
// switched under the open pane kept the other mode's header (pinned from the pointer: the hamburger over the logo
// and a blank row; unpinned: no hamburger row). The pane closes first, so its next opening lays the header out again.
// It closes in the Left mode, on either side of the switch; its late PaneClosing is refused (KeepPinnedOpen).
void SwitchClosed(State& state, const controls::NavigationView& navigation, PaneMode mode) {
    if (navigation.PaneDisplayMode() == mode) return;
    if (auto pane = state.pane.get()) ParkFocus(state, pane);  // off the search field, which WinUI picks next
    const bool fromLeft = navigation.PaneDisplayMode() == PaneMode::Left;
    if (!fromLeft) navigation.PaneDisplayMode(mode);
    state.repinning = true;
    try {
        navigation.IsPaneOpen(false);
    } catch (...) {
    }
    state.repinning = false;
    if (fromLeft) navigation.PaneDisplayMode(mode);
}

// The window crossed the width while pinned. Switched under the open pane, the template stays folded (a squashed column
// over the pages): the pane closes with the switch (SwitchClosed), and the next tick opens it again (ApplyPin).
void RepinForWidth(State& state, const controls::NavigationView& navigation) {
    auto pane = state.pane.get();
    SwitchClosed(state, navigation, PinnedMode(navigation));
    if (pane) ParkFocus(state, pane);
}

// Opens the pane as Apple Music does when pinned, or folds it back to hover. A click asks the theme for a pass.
void ApplyPin(State& state, bool notify = true) {
    auto navigation = state.navigation.get();
    auto pane = state.pane.get();
    if (!navigation || !pane) return;
    if (state.pinned) {
        if (state.watch) state.watch.Stop();
        const bool pushes = PinPushes(navigation);
        SwitchClosed(state, navigation, PinnedMode(navigation));
        navigation.IsPaneOpen(true);
        SetDismissLayer(state, navigation, !pushes);  // over the pages, a click on them must not close the pane
        g_folded = false;
    } else {
        ParkFocus(state, pane);  // the header's buttons go away as the pane folds: WinUI would pick the search field
        SwitchClosed(state, navigation, PaneMode::LeftCompact);
        navigation.IsPaneOpen(CursorOverPane(state, navigation, pane));
        ParkFocus(state, pane);  // and again after: the switch itself moved it into the pane's header (seen on screen)
        SetDismissLayer(state, navigation, true);
        g_folded = true;
        Watch(state);
    }
    FillOverPages(state, navigation, OverPages(state, navigation));
    if (notify && g_changed) g_changed();
}

// Apple's wide mode shows the pane folded while it is closed, and does not open it again by itself.
void GiveBack(const controls::NavigationView& navigation, PaneMode apple) {
    navigation.PaneDisplayMode(apple);
    navigation.IsPaneOpen(navigation.DisplayMode() == controls::NavigationViewDisplayMode::Expanded);
}

void Release(State& state) {
    if (state.watch) state.watch.Stop();
    if (auto pane = state.pane.get()) {
        try {
            if (state.entered) pane.RemoveHandler(xaml::UIElement::PointerEnteredEvent(), state.entered);
            if (state.exited) pane.RemoveHandler(xaml::UIElement::PointerExitedEvent(), state.exited);
            if (state.keyed) pane.RemoveHandler(xaml::UIElement::KeyDownEvent(), state.keyed);
        } catch (...) {
        }
    }
    if (auto toggle = state.toggle.get(); toggle && state.clickToken.value != 0) {
        try {
            toggle.Click(state.clickToken);
            toggle.RemoveHandler(xaml::UIElement::KeyDownEvent(), state.spaceDown);
            toggle.RemoveHandler(xaml::UIElement::KeyUpEvent(), state.spaceUp);
        } catch (...) {
        }
    }
    state.clickToken = {};
    state.spacePressed = false;
    ReleaseChrome(state);
    if (auto navigation = state.navigation.get()) {
        try {
            GiveBack(navigation, state.apple);
            // WinUI applies the mode a little later: the same is done again then.
            if (auto queue = dispatching::DispatcherQueue::GetForCurrentThread()) {
                queue.TryEnqueue(dispatching::DispatcherQueueHandler([navigation, apple = state.apple]() noexcept {
                    try {
                        GiveBack(navigation, apple);
                    } catch (...) {
                    }
                }));
            }
        } catch (...) {
        }
    }
    // After GiveBack: a group opened while the column is still folded and closed could be refused or open a flyout.
    UnfoldGroups(state);
    state.navigation = nullptr;
    state.pane = nullptr;
    state.toggle = nullptr;
    state.tracker.Forget();
    g_folded = false;
    if (g_changed) g_changed();
}

// Closes the pane once the click is done: on the hamburger, WinUI's own toggle flips IsPaneOpen before or after.
void CloseAfterClick() {
    auto queue = dispatching::DispatcherQueue::GetForCurrentThread();
    if (!queue) return;
    queue.TryEnqueue(dispatching::DispatcherQueueHandler([]() noexcept {
        try {
            State& live = TheState();
            auto navigation = live.navigation.get();
            auto pane = live.pane.get();
            if (navigation && pane && !live.pinned) ClosePane(live, navigation, pane);
        } catch (...) {
        }
    }));
}

// Once WinUI's toggle is done: a pane the hamburger opened with Enter stays open while the keyboard is on it
// (KeyboardHolds), since the pointer is elsewhere and the next look would close it at once.
void NoteKeyOpen(bool byKey) {
    auto queue = dispatching::DispatcherQueue::GetForCurrentThread();
    if (!queue) return;
    queue.TryEnqueue(dispatching::DispatcherQueueHandler([byKey]() noexcept {
        try {
            State& live = TheState();
            auto navigation = live.navigation.get();
            live.keysUsed = byKey && navigation && navigation.IsPaneOpen() && !live.pinned;
        } catch (...) {
        }
    }));
}

void UnpinFromToggle(State& state);

// The hamburger keeps WinUI's own toggle. While pinned, it unpins too, and the pane folds.
xaml::RoutedEventHandler MakeToggleHandler() {
    return xaml::RoutedEventHandler([](IInspectable const& sender, xaml::RoutedEventArgs const&) noexcept {
        try {
            State& live = TheState();
            live.clicked = true;
            if (!live.pinned) {
                // A click without the pointer on the button came from a key (or UI Automation). Its focus state does
                // not tell: WinUI has already moved the focus into the pane it opened.
                auto toggle = sender.try_as<xaml::FrameworkElement>();
                NoteKeyOpen(toggle && !CursorOver(live, toggle));
                return;
            }
            UnpinFromToggle(live);
        } catch (...) {
        }
    });
}

void UnpinFromToggle(State& state) {
    state.pinned = false;
    ApplyPin(state);
    ShowPin(state, true);
    CloseAfterClick();
}

// Space on the hamburger, once released without a Click (Apple Music marks Space handled before the window's content
// sees it, so the button never clicks): does what WinUI's toggle does on Enter, the pane then held for the keyboard.
void ToggleFromSpace() {
    auto queue = dispatching::DispatcherQueue::GetForCurrentThread();
    if (!queue) return;
    queue.TryEnqueue(dispatching::DispatcherQueueHandler([]() noexcept {
        try {
            State& live = TheState();
            auto navigation = live.navigation.get();
            if (!navigation || live.clicked) return;
            if (live.pinned) {
                UnpinFromToggle(live);
                return;
            }
            navigation.IsPaneOpen(!navigation.IsPaneOpen());
            live.keysUsed = navigation.IsPaneOpen();
            if (live.keysUsed) Watch(live);
        } catch (...) {
        }
    }));
}

// Boxed once, so Release can take them off. Handled events too: what took Space marked it handled.
void MakeSpaceHandlers(State& state) {
    if (state.spaceDown) return;
    state.spaceDown = winrt::box_value(xaml::Input::KeyEventHandler(
        [](IInspectable const&, xaml::Input::KeyRoutedEventArgs const& args) noexcept {
            try {
                if (args.Key() != winrt::Windows::System::VirtualKey::Space || args.KeyStatus().WasKeyDown) return;
                State& live = TheState();
                live.spacePressed = true;
                live.clicked = false;
            } catch (...) {
            }
        }));
    state.spaceUp = winrt::box_value(xaml::Input::KeyEventHandler(
        [](IInspectable const&, xaml::Input::KeyRoutedEventArgs const& args) noexcept {
            try {
                State& live = TheState();
                if (args.Key() != winrt::Windows::System::VirtualKey::Space || !live.spacePressed) return;
                live.spacePressed = false;
                ToggleFromSpace();
            } catch (...) {
            }
        }));
}

// The pin button: pins the pane, or unpins it and folds it, the keyboard parked on the hamburger (seen on screen: a
// pane unpinned under the pointer folded anyway, the focus left on the hidden pin). Pinning can hand the keyboard to
// the search field, where Apple opens its search page: it goes back to the pin.
xaml::RoutedEventHandler MakePinHandler() {
    return xaml::RoutedEventHandler([](IInspectable const&, xaml::RoutedEventArgs const&) noexcept {
        try {
            State& live = TheState();
            const xaml::FocusState focus = live.pin ? live.pin.FocusState() : xaml::FocusState::Unfocused;
            live.pinned = !live.pinned;
            ApplyPin(live);
            ShowPin(live, true);
            if (!live.pinned) {
                CloseAfterClick();
            } else if (live.pin && focus != xaml::FocusState::Unfocused) {
                live.pin.Focus(focus);
            }
        } catch (...) {
        }
    });
}

// The pin button in Apple's header grid (the logo, "Apple Music" and "···"), beside "···". Found again when Apple
// rebuilt its header.
void AddPin(State& state, const controls::NavigationView& navigation) {
    auto header = navigation.PaneHeader().try_as<controls::Grid>();
    xaml::FrameworkElement more = header ? FindMore(header) : nullptr;
    if (!more) return;
    state.pin = MakePinButton();
    state.pinGlyph = MakePinGlyph(state.pinTurn);
    state.pin.Content(state.pinGlyph);
    state.pin.Click(MakePinHandler());
    uint32_t index = 0;
    header.Children().IndexOf(more, index);
    header.Children().InsertAt(index, state.pin);  // before "···", so the Tab key meets them in the order they show
    state.pinHeader = header;
    state.more = winrt::make_weak(more);
    state.moreToken = more.RegisterPropertyChangedCallback(
        xaml::FrameworkElement::MarginProperty(), [](xaml::DependencyObject const&, xaml::DependencyProperty const&) noexcept {
            try {
                PlacePin(TheState());
            } catch (...) {
            }
        });
    // Its width too: a theme that sized "···" (Mirror) gives it back after its margin, at the next layout. Read with the
    // margin alone, the first open after that theme left the pin 46 pixels short of "···" (seen at 125%).
    state.moreSizeToken = more.SizeChanged([](IInspectable const&, xaml::SizeChangedEventArgs const&) noexcept {
        try {
            PlacePin(TheState());
        } catch (...) {
        }
    });
    ShowPin(state, false);
}

// The pane's handlers, made once and boxed so Release can take them off: the pointer opens and closes it, and a key
// in it (but not in a text field, which HoldsForTyping already holds for a moment) keeps it for the keyboard.
void MakePaneHandlers(State& state) {
    if (state.entered) return;
    state.entered = winrt::box_value(xaml::Input::PointerEventHandler(
        [](IInspectable const&, xaml::Input::PointerRoutedEventArgs const&) noexcept {
            try {
                State& live = TheState();
                auto navigation = live.navigation.get();
                if (!navigation) return;
                if (!navigation.IsPaneOpen()) navigation.IsPaneOpen(true);
                Watch(live);
            } catch (...) {
            }
        }));
    // PointerExited bubbles from each child the pointer leaves for its neighbour: the cursor decides.
    state.exited = winrt::box_value(xaml::Input::PointerEventHandler(
        [](IInspectable const&, xaml::Input::PointerRoutedEventArgs const&) noexcept {
            try {
                CloseIfLeft(TheState());
            } catch (...) {
            }
        }));
    state.keyed = winrt::box_value(xaml::Input::KeyEventHandler(
        [](IInspectable const&, xaml::Input::KeyRoutedEventArgs const& args) noexcept {
            try {
                State& live = TheState();
                auto navigation = live.navigation.get();
                if (navigation && navigation.IsPaneOpen() && !args.OriginalSource().try_as<controls::TextBox>()) {
                    live.keysUsed = true;
                }
            } catch (...) {
            }
        }));
}

void Take(State& state, const controls::NavigationView& navigation, const xaml::FrameworkElement& pane) {
    state.apple = navigation.PaneDisplayMode();
    state.navigation = winrt::make_weak(navigation);
    state.pane = winrt::make_weak(pane);
    MakePaneHandlers(state);
    navigation.PaneDisplayMode(PaneMode::LeftCompact);
    navigation.IsPaneOpen(false);
    pane.AddHandler(xaml::UIElement::PointerEnteredEvent(), state.entered, true);
    pane.AddHandler(xaml::UIElement::PointerExitedEvent(), state.exited, true);
    pane.AddHandler(xaml::UIElement::KeyDownEvent(), state.keyed, true);
    if (auto toggle = FindFirst<controls::Button>(navigation, L"TogglePaneButton", 0)) {
        state.toggle = winrt::make_weak(toggle);
        state.clickToken = toggle.Click(MakeToggleHandler());
        MakeSpaceHandlers(state);
        toggle.AddHandler(xaml::UIElement::KeyDownEvent(), state.spaceDown, true);
        toggle.AddHandler(xaml::UIElement::KeyUpEvent(), state.spaceUp, true);
    }
    TakeChrome(state, navigation);
    g_folded = !state.pinned;
}

std::string Px(double value) {
    return std::to_string(static_cast<int>(value)) + " px";
}

// The sizes the hit test uses, measured once the pane is taken: the coordinator checks them on screen.
std::string MeasureLine(const controls::NavigationView& navigation, const xaml::FrameworkElement& pane) {
    return "Sidebar: pane " + Px(pane.ActualWidth()) + ", folded column " + Px(navigation.CompactPaneLength()) + ", open " +
           Px(navigation.OpenPaneLength());
}

// The layout has not run yet when the pane is taken: its width is read once the queue is idle.
void MeasureLater(Journal& journal, const controls::NavigationView& navigation, const xaml::FrameworkElement& pane) {
    auto queue = dispatching::DispatcherQueue::GetForCurrentThread();
    if (!queue) return;
    queue.TryEnqueue(dispatching::DispatcherQueueHandler([&journal, navigation, pane]() noexcept {
        try {
            journal.Write(MeasureLine(navigation, pane));
        } catch (...) {
        }
    }));
}

}  // namespace

void KeepSidebarOnHover(Journal& journal) noexcept {
    try {
        State& state = TheState();
        if (!g_enabled) {
            state.pinned = false;
            if (state.navigation.get()) {
                Release(state);
                journal.Write("Sidebar: given back to Apple Music");
            }
            return;
        }
        // Cheap once in place: the tree is searched again only when Apple rebuilt it.
        if (auto navigation = state.navigation.get(); navigation && navigation.IsLoaded()) {
            // Apple puts its own mode back as the window changes size: fold it again, or pin again for the new width.
            if (!state.pinned && navigation.PaneDisplayMode() != PaneMode::LeftCompact) {
                navigation.PaneDisplayMode(PaneMode::LeftCompact);
            } else if (state.pinned && navigation.PaneDisplayMode() != PinnedMode(navigation)) {
                RepinForWidth(state, navigation);
            } else if (state.pinned && !navigation.IsPaneOpen()) {
                ApplyPin(state);
            }
            if (!state.pin || navigation.PaneHeader() != state.pinHeader) {
                RemovePin(state);
                AddPin(state, navigation);
            }
            return;
        }
        Release(state);
        xaml::Window window = AppWindow(L"MainWindow");
        xaml::UIElement content = window ? window.Content() : nullptr;
        if (!content) return;
        auto navigation = FindFirst<controls::NavigationView>(content, L"", 0);
        auto pane = navigation ? FindFirst<xaml::FrameworkElement>(navigation, L"PaneRoot", 0) : nullptr;
        if (!pane) return;
        state.window = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(window.AppWindow().Id().Value));
        Take(state, navigation, pane);
        if (state.pinned) ApplyPin(state, false);
        MeasureLater(journal, navigation, pane);
        if (!state.told) journal.Write("Sidebar: folded, it opens over the pages under the pointer");
        state.told = true;
    } catch (...) {
    }
}

void SetSidebarOnHover(bool enabled) noexcept {
    g_enabled = enabled;
}

void SetSidebarChangedListener(std::function<void()> changed) {
    g_changed = std::move(changed);
}

bool SidebarKeptFolded() noexcept {
    return g_folded;
}

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
