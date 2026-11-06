#include "sidebar_hover.hpp"

#if defined(AMPIFY_WITH_XAML)

#include "xaml_host.hpp"

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
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.Media.Animation.h>
#include <winrt/Microsoft.UI.Xaml.Shapes.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
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
constexpr wchar_t kPinGlyph[] = L"\xE718";                    // Pin in Segoe Fluent Icons (Unpin is E77A)
constexpr int kTurnMs = 180;                                  // the hamburger and the pin turn in this time

std::atomic<bool> g_enabled{false};
std::atomic<bool> g_folded{false};  // Take done, Release not yet, and not pinned open
std::function<void()> g_changed;    // asks the theme for a pass now: the themes' folded rules follow a pin at once

struct State {
    winrt::weak_ref<controls::NavigationView> navigation;
    winrt::weak_ref<xaml::FrameworkElement> pane;
    winrt::weak_ref<controls::Button> toggle;  // Apple's hamburger, outside the pane: a click pins the pane
    HWND window = nullptr;
    PaneMode apple = PaneMode::Auto;  // Apple's own mode, put back when turned off
    IInspectable entered{nullptr};    // the boxed handlers, kept to take them off again
    IInspectable exited{nullptr};
    winrt::event_token clickToken{};  // the hamburger's Click, taken off by this token in Release
    dispatching::DispatcherQueueTimer watch{nullptr};
    bool told = false;
    bool pinned = false;  // session only: a click on the hamburger keeps the pane open, like Apple's own mode
    ampify::TypingTracker tracker;
    // What Ampify changes in Apple's pane while it holds it; each is put back as Apple had it on Release.
    controls::FontIcon pinGlyph{nullptr};  // ours: the pin, shown while pinned
    media::CompositeTransform pinTurn{nullptr};
    controls::Grid glyphGrid{nullptr};  // the grid the pin was added to
    controls::AnimatedIcon appleGlyph{nullptr};  // Apple's hamburger icon
    xaml::Visibility appleVisibility = xaml::Visibility::Visible;
    media::Transform appleTransform{nullptr};
    winrt::Windows::Foundation::Point appleOrigin{};
    media::CompositeTransform appleTurn{nullptr};
    animation::Storyboard glyphStory{nullptr};
    shapes::Rectangle dismiss{nullptr};  // the sidebar's SplitView light-dismiss layer
    IInspectable dismissHit{nullptr};    // its local values before Ampify, or the unset value: put back as they were
    IInspectable dismissFill{nullptr};
    int64_t paneToken = 0;  // the IsPaneOpen callback, taken off by this token in Release
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

// The width the pane shows: the open sidebar, or the folded column (its layout box stays 290 px wide).
bool CursorOverPane(const State& state, const controls::NavigationView& navigation, const xaml::FrameworkElement& pane) {
    POINT cursor{};
    if (!state.window || !GetCursorPos(&cursor)) return false;
    if (GetAncestor(WindowFromPoint(cursor), GA_ROOT) != state.window || !ScreenToClient(state.window, &cursor)) {
        return false;
    }
    xaml::XamlRoot root = pane.XamlRoot();
    double scale = root ? root.RasterizationScale() : 1.0;
    winrt::Windows::Foundation::Point origin = pane.TransformToVisual(nullptr).TransformPoint({0, 0});
    double x = cursor.x / scale - origin.X;
    double y = cursor.y / scale - origin.Y;
    double width = navigation.IsPaneOpen() ? navigation.OpenPaneLength() : navigation.CompactPaneLength();
    return x >= 0 && y >= 0 && x < width && y < pane.ActualHeight();
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

// The keyboard leaves the pane for the hamburger, which shows in both modes. Not the NavigationView itself: it hands
// focus to its search field, and Apple opens its search page as soon as that field gets the focus.
void ParkFocus(const State& state, const xaml::FrameworkElement& pane) {
    if (!FocusInside(pane)) return;
    if (auto toggle = state.toggle.get()) toggle.Focus(xaml::FocusState::Programmatic);
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
    if (cursor || typing || menu) return true;
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

// A quarter turn into place while the element fades in: the pin and Apple's hamburger both use it.
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

// Our pin, in the cell of Apple's hamburger glyph. Apple's icon stays in the tree; its own turn is put back on Release.
void AddPinGlyph(State& state, const controls::Button& toggle) {
    auto apple = FindFirst<controls::AnimatedIcon>(toggle, L"Icon", 0);
    xaml::DependencyObject border = apple ? media::VisualTreeHelper::GetParent(apple) : nullptr;
    auto grid = border ? media::VisualTreeHelper::GetParent(border).try_as<controls::Grid>() : nullptr;
    if (!apple || !grid) return;
    controls::FontIcon pin;
    pin.FontFamily(xaml::Media::FontFamily(L"Segoe Fluent Icons, Segoe MDL2 Assets"));
    pin.Glyph(kPinGlyph);
    pin.FontSize(16);
    pin.Foreground(toggle.Foreground());
    pin.HorizontalAlignment(xaml::HorizontalAlignment::Center);
    pin.VerticalAlignment(xaml::VerticalAlignment::Center);
    pin.Visibility(xaml::Visibility::Collapsed);
    media::CompositeTransform pinTurn;
    pin.RenderTransformOrigin(winrt::Windows::Foundation::Point{0.5f, 0.5f});
    pin.RenderTransform(pinTurn);
    grid.Children().Append(pin);
    state.pinGlyph = pin;
    state.pinTurn = pinTurn;
    state.glyphGrid = grid;
    state.appleGlyph = apple;
    state.appleVisibility = apple.Visibility();
    state.appleTransform = apple.RenderTransform();
    state.appleOrigin = apple.RenderTransformOrigin();
    state.appleTurn = media::CompositeTransform();
    apple.RenderTransformOrigin(winrt::Windows::Foundation::Point{0.5f, 0.5f});
    apple.RenderTransform(state.appleTurn);
}

// The pin while pinned, Apple's glyph while folded. A change turns once, unless the system animations are off.
void ShowGlyph(State& state, bool animate) {
    if (!state.pinGlyph || !state.appleGlyph) return;
    state.appleGlyph.Visibility(state.pinned ? xaml::Visibility::Collapsed : state.appleVisibility);
    state.pinGlyph.Visibility(state.pinned ? xaml::Visibility::Visible : xaml::Visibility::Collapsed);
    if (!animate || !AnimationsOn()) return;
    if (state.glyphStory) state.glyphStory.Stop();
    if (state.pinned) {
        state.glyphStory = EaseTurn(state.pinTurn, state.pinGlyph);
    } else {
        state.glyphStory = EaseTurn(state.appleTurn, state.appleGlyph);
    }
    state.glyphStory.Begin();
}

// Apple's hamburger glyph back as Apple had it, and the pin taken out of its grid.
void RestoreGlyph(State& state) {
    if (state.glyphStory) state.glyphStory.Stop();
    state.glyphStory = nullptr;
    if (state.appleGlyph) {
        state.appleGlyph.Visibility(state.appleVisibility);
        state.appleGlyph.RenderTransform(state.appleTransform);
        state.appleGlyph.RenderTransformOrigin(state.appleOrigin);
    }
    uint32_t index = 0;
    if (state.glyphGrid && state.pinGlyph && state.glyphGrid.Children().IndexOf(state.pinGlyph, index)) {
        state.glyphGrid.Children().RemoveAt(index);
    }
    state.pinGlyph = nullptr;
    state.pinTurn = nullptr;
    state.glyphGrid = nullptr;
    state.appleGlyph = nullptr;
    state.appleTurn = nullptr;
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

// Follows IsPaneOpen whoever changes it (the hover, a click, Apple), so the groups open and fold with the pane.
void WatchPaneOpen(State& state, const controls::NavigationView& navigation) {
    auto changed = [](xaml::DependencyObject const&, xaml::DependencyProperty const&) noexcept {
        try {
            State& live = TheState();
            if (auto current = live.navigation.get()) SyncGroups(live, current);
        } catch (...) {
        }
    };
    state.paneToken = navigation.RegisterPropertyChangedCallback(controls::NavigationView::IsPaneOpenProperty(), changed);
}

// What the hover needs from Apple's pane once taken: the pin, the dismiss layer, the groups and the pane watch.
void TakeChrome(State& state, const controls::NavigationView& navigation, const controls::Button& toggle) {
    if (toggle) AddPinGlyph(state, toggle);
    ShowGlyph(state, false);
    SetDismissLayer(state, navigation, !state.pinned);
    WatchPaneOpen(state, navigation);
    SyncGroups(state, navigation);
}

// Puts back everything Ampify changed in Apple's pane. Apple's own groups open again, the way they were.
void ReleaseChrome(State& state) {
    try {
        if (auto navigation = state.navigation.get(); navigation && state.paneToken != 0) {
            navigation.UnregisterPropertyChangedCallback(controls::NavigationView::IsPaneOpenProperty(), state.paneToken);
        }
    } catch (...) {
    }
    state.paneToken = 0;
    try {
        RestoreGlyph(state);
        RestoreDismissLayer(state);
    } catch (...) {
    }
}

// Opens the pane as Apple Music does when pinned, or folds it back to hover. A click asks the theme for a pass.
void ApplyPin(State& state, bool notify = true) {
    auto navigation = state.navigation.get();
    auto pane = state.pane.get();
    if (!navigation || !pane) return;
    if (state.pinned) {
        if (state.watch) state.watch.Stop();
        // Left, not Apple's Auto: Auto reads the NavigationView's own width, narrower than the window under a theme
        // with side panels (1171 of 1500 under Glass), and stays an overlay below 1325 instead of pushing the pages.
        navigation.PaneDisplayMode(PaneMode::Left);
        navigation.IsPaneOpen(true);
        SetDismissLayer(state, navigation, false);
        g_folded = false;
    } else {
        ParkFocus(state, pane);  // the header's buttons go away as the pane folds: WinUI would pick the search field
        navigation.PaneDisplayMode(PaneMode::LeftCompact);
        navigation.IsPaneOpen(CursorOverPane(state, navigation, pane));
        ParkFocus(state, pane);  // and again after: the switch itself moved it into the pane's header (seen on screen)
        SetDismissLayer(state, navigation, true);
        g_folded = true;
        Watch(state);
    }
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
        } catch (...) {
        }
    }
    if (auto toggle = state.toggle.get(); toggle && state.clickToken.value != 0) {
        try {
            toggle.Click(state.clickToken);
        } catch (...) {
        }
    }
    state.clickToken = {};
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

// WinUI's own toggle flips IsPaneOpen on the same click: the state is applied again once that is done.
xaml::RoutedEventHandler MakePinHandler() {
    return xaml::RoutedEventHandler([](IInspectable const&, xaml::RoutedEventArgs const&) noexcept {
        try {
            State& live = TheState();
            live.pinned = !live.pinned;
            ApplyPin(live);
            ShowGlyph(live, true);
            if (auto queue = dispatching::DispatcherQueue::GetForCurrentThread()) {
                queue.TryEnqueue(dispatching::DispatcherQueueHandler([]() noexcept {
                    try {
                        ApplyPin(TheState(), false);
                    } catch (...) {
                    }
                }));
            }
        } catch (...) {
        }
    });
}

void Take(State& state, const controls::NavigationView& navigation, const xaml::FrameworkElement& pane) {
    state.apple = navigation.PaneDisplayMode();
    state.navigation = winrt::make_weak(navigation);
    state.pane = winrt::make_weak(pane);
    if (!state.entered) {
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
    }
    navigation.PaneDisplayMode(PaneMode::LeftCompact);
    navigation.IsPaneOpen(false);
    pane.AddHandler(xaml::UIElement::PointerEnteredEvent(), state.entered, true);
    pane.AddHandler(xaml::UIElement::PointerExitedEvent(), state.exited, true);
    if (auto toggle = FindFirst<controls::Button>(navigation, L"TogglePaneButton", 0)) {
        state.toggle = winrt::make_weak(toggle);
        state.clickToken = toggle.Click(MakePinHandler());
    }
    TakeChrome(state, navigation, state.toggle.get());
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
            // Apple puts its own mode back as the window changes size: fold it again, unless the pane is pinned.
            if (!state.pinned && navigation.PaneDisplayMode() != PaneMode::LeftCompact) {
                navigation.PaneDisplayMode(PaneMode::LeftCompact);
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
