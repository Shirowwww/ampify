#include "sidebar_hover.hpp"

#include "sidebar_hover_state.hpp"
#if defined(AMPIFY_WITH_XAML)

#include "win_common.hpp"
#ifdef GetCurrentTime
#undef GetCurrentTime  // windows.h macro that breaks Storyboard::GetCurrentTime in the projection
#endif
#include "xaml_chrome.hpp"

#include "../core/i18n.hpp"
#include "../core/journal.hpp"
#include "../core/sidebar_groups.hpp"
#include "../core/sidebar_hold.hpp"

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.h>
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
#include <cmath>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ampify::win::sidebar_detail {

// Never destroyed: XAML objects are released on the interface thread only, never by a static destructor (a release
// while the process exits, after XAML has shut down, crashed Apple Music on every close).
State& TheState() {
    static State* state = new State();
    return *state;
}

}  // namespace ampify::win::sidebar_detail

namespace ampify::win {

namespace {

using namespace sidebar_detail;

constexpr auto kRefocusFor = std::chrono::milliseconds(1500);  // a pane opened by a key: the hamburger keeps the keyboard

std::atomic<bool> g_enabled{false};
std::atomic<bool> g_folded{false};  // Take done, Release not yet, and not pinned open
std::atomic<bool> g_over{false};    // the pane is open over the pages (OverPages), as the theme last heard
std::function<void()> g_changed;    // asks the theme for a pass now: the themes' folded rules follow a pin at once
Journal* g_journal = nullptr;       // KeepSidebarOnHover's, for Trace
int g_traced = 0;                   // Trace lines this session, kept few
constexpr int kMostTraced = 60;

// One line on why the pane opened or folded, for a theme where it never opens on screen (Mirror's pane on the right).
void Trace(const std::string& line) {
    if (!g_journal || g_traced >= kMostTraced) return;
    g_traced++;
    g_journal->Write("Sidebar: " + line);
}

// The window under the cursor is Apple Music's, or one it owns: WinUI draws popups in windows of their own, and after a
// tap on Alt its access-key layer (PopupWindowSiteBridge) lies over the column, which then counted as off the pane.
bool OverWindow(const State& state, const CursorSpot& spot) {
    return spot.root == state.window || spot.rootOwner == state.window;
}

// The real cursor in `element`'s own coordinates, while it is over Apple Music's window.
std::optional<winrt::Windows::Foundation::Point> CursorIn(const State& state, const xaml::UIElement& element) {
    std::optional<CursorSpot> spot = state.window ? CursorOver(state.window) : std::nullopt;
    if (!spot || !OverWindow(state, *spot) || !spot->client) return std::nullopt;
    ClientPixel cursor = *spot->client;
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

// The pane is the open sidebar, or the folded column (its layout box stays 290 px wide, so it is not the width used).
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

// The tooltips open in the window. The pane that opens or folds under the pointer moves the entries away from it: the
// entry never hears the pointer leave, and its tooltip stayed on the page ("Create", up to a dozen after some hovers).
void CloseToolTips(const xaml::UIElement& element) {
    if (!element.XamlRoot()) return;
    for (const auto& popup : media::VisualTreeHelper::GetOpenPopupsForXamlRoot(element.XamlRoot())) {
        if (auto tip = popup.Child().try_as<controls::ToolTip>()) tip.IsOpen(false);
    }
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

// Opened by a key, WinUI and Apple's header move the focus into the pane, onto the "···": the next Space or Enter
// went there and never folded the pane. The keyboard goes back to the hamburger, so a second press folds it again.
// The focus can move more than once while the pane opens, so this holds for a moment, not for one look.
void RefocusToggle(State& state, const xaml::FrameworkElement& pane) {
    if (Clock::now() > state.refocusUntil || state.pinned || !FocusInside(pane)) return;
    if (auto toggle = state.toggle.get()) toggle.Focus(xaml::FocusState::Keyboard);
}

// Tab and the arrows send the keyboard somewhere on purpose; any other key (an injected one included) does not.
bool MovesFocus(winrt::Windows::System::VirtualKey key) {
    using winrt::Windows::System::VirtualKey;
    return key == VirtualKey::Tab || key == VirtualKey::Up || key == VirtualKey::Down || key == VirtualKey::Left ||
           key == VirtualKey::Right;
}

void HoldToggleFocus(State& state) {
    state.refocusUntil = state.keysUsed ? Clock::now() + kRefocusFor : Clock::time_point{};
}

// Closes the pane once the pointer is off it and nothing holds it; true while it stays open. Pinned, it never closes.
// Every hold is read on each look, so the search text stays current even while the pointer is over the pane.
bool CloseIfLeft(State& state) {
    auto navigation = state.navigation.get();
    auto pane = state.pane.get();
    if (!navigation || !pane || !navigation.IsPaneOpen() || state.pinned) return false;
    RefocusToggle(state, pane);
    bool cursor = CursorOverPane(state, navigation, pane);
    bool typing = TypingHolds(state, pane);
    bool menu = HoldsForMenu(WindowInFront(state), MenuOpen(pane));
    if (cursor || typing || menu || KeyboardHolds(state, pane)) return true;
    auto at = CursorIn(state, pane);
    Trace("folds, the cursor off the pane (in the pane's own pixels: " +
          (at ? std::to_string(std::lround(at->X)) + "," + std::to_string(std::lround(at->Y)) : std::string("none")) +
          ", the pane " + std::to_string(std::lround(pane.ActualWidth())) + "x" +
          std::to_string(std::lround(pane.ActualHeight())) + ")");
    ClosePane(state, navigation, pane);
    return false;
}

void Watch(State& state) {
    if (!state.watch) {
        state.watch = dispatching::DispatcherQueue::GetForCurrentThread().CreateTimer();
        state.watch.Interval(kCursorPollEvery);  // the real cursor, while the pane is open
        state.watch.Tick([](auto const&, auto const&) noexcept {
            try {
                State& live = TheState();
                if (!CloseIfLeft(live)) live.watch.Stop();
            } catch (...) {
                // A handler runs inside Apple Music's XAML frame: nothing may escape it.
            }
        });
    }
    if (!state.watch.IsRunning()) state.watch.Start();
}


bool PinPushes(const controls::NavigationView& navigation);

// The pane stands over the pages: open under the pointer, or pinned in a window too narrow to push them.
bool OverPages(const State& state, const controls::NavigationView& navigation) {
    return navigation.IsPaneOpen() && (!state.pinned || !PinPushes(navigation));
}

// The pane open over the pages gets its fill and its edge there, and the theme hears when it opens over them or leaves
// them: a dimmed sidebar stays opaque over them (KeepSidebarOpaque in the theme engine).
void DressOverPages(State& state, const controls::NavigationView& navigation) {
    const bool over = OverPages(state, navigation);
    HideAppleShadow(state, navigation);
    FillPane(state, navigation);
    EdgeOverPages(state, navigation, over);
    if (g_over.exchange(over) != over && g_changed) g_changed();
}

// Follows IsPaneOpen whoever changes it (the hover, the hamburger, Apple), so the groups, the pin and the fill open and
// fold with the pane. Opened by the hamburger, the pane closes again once the pointer is off it, as after a hover.
void WatchPaneOpen(State& state, const controls::NavigationView& navigation) {
    auto changed = [](xaml::DependencyObject const&, xaml::DependencyProperty const&) noexcept {
        try {
            State& live = TheState();
            auto current = live.navigation.get();
            if (!current) return;
            // Pinned over narrow pages, WinUI closes the pane after a click on an item: open again here, before any
            // frame is drawn. Reopened from the queue (ReopenAfterRefusedClose), the squashed column showed for some
            // 100 ms while the next page loaded. This set calls back here with the pane open.
            if (!current.IsPaneOpen() && live.pinned && !live.repinning) {
                current.IsPaneOpen(true);
                return;
            }
            if (auto pane = live.pane.get()) {
                auto box = pane.TransformToVisual(nullptr).TransformBounds(
                    {0, 0, static_cast<float>(pane.ActualWidth()), static_cast<float>(pane.ActualHeight())});
                Trace(std::string(current.IsPaneOpen() ? "open" : "folded") + ", pane drawn at " +
                      std::to_string(std::lround(box.X)) + "," + std::to_string(std::lround(box.Y)) + " " +
                      std::to_string(std::lround(box.Width)) + "x" + std::to_string(std::lround(box.Height)) +
                      ", mode " + std::to_string(static_cast<int>(current.DisplayMode())));
            }
            CloseToolTips(current);
            SyncGroups(live, current);
            ShowPin(live, false);
            DressOverPages(live, current);
            if (!current.IsPaneOpen()) live.keysUsed = false;
            if (current.IsPaneOpen() && !live.pinned) Watch(live);
        } catch (...) {
            // A handler runs inside Apple Music's XAML frame: nothing may escape it.
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
            // A handler runs inside Apple Music's XAML frame: nothing may escape it.
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
            // A handler runs inside Apple Music's XAML frame: nothing may escape it.
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
                // A handler runs inside Apple Music's XAML frame: nothing may escape it.
            }
        });
}

xaml::RoutedEventHandler MakePinHandler();

// What the hover needs from Apple's pane once taken: the pin, the dismiss layer, the groups and the pane watch.
void TakeChrome(State& state, const controls::NavigationView& navigation) {
    AddPin(state, navigation, MakePinHandler());
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
        // The element may be torn down already: nothing is left to put back.
    }
    state.paneToken = 0;
    state.closingToken = {};
    state.openingToken = {};
    state.openHeader.clear();
    try {
        RemovePin(state);
        RestoreDismissLayer(state);
        RemoveFill(state);
        RemoveEdge(state);
        RestoreAppleShadow(state);
        HoldPaneStill(state, nullptr, false);
    } catch (...) {
        // The element may be torn down already: nothing is left to put back.
    }
    state.column = nullptr;
    state.caster = nullptr;
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
        // WinUI may refuse the close: the pane stays as it is.
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
        HoldPaneStill(state, navigation, false);  // the pane folds with Apple's transition, as under the pointer
        ParkFocus(state, pane);  // the header's buttons go away as the pane folds: WinUI would pick the search field
        SwitchClosed(state, navigation, PaneMode::LeftCompact);
        navigation.IsPaneOpen(CursorOverPane(state, navigation, pane));
        ParkFocus(state, pane);  // and again after: the switch itself moved it into the pane's header (seen on screen)
        SetDismissLayer(state, navigation, true);
        g_folded = true;
        Watch(state);
    }
    DressOverPages(state, navigation);
    HoldPaneStill(state, navigation, state.pinned && !PinPushes(navigation));
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
            // The element may be torn down already: nothing is left to put back.
        }
    }
    if (auto toggle = state.toggle.get(); toggle && state.clickToken.value != 0) {
        try {
            toggle.Click(state.clickToken);
            toggle.RemoveHandler(xaml::UIElement::KeyDownEvent(), state.spaceDown);
            toggle.RemoveHandler(xaml::UIElement::KeyUpEvent(), state.spaceUp);
        } catch (...) {
            // The element may be torn down already: nothing is left to put back.
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
                        // A handler runs inside Apple Music's XAML frame: nothing may escape it.
                    }
                }));
            }
        } catch (...) {
            // The element may be torn down already: nothing is left to put back.
        }
    }
    // After GiveBack: a group opened while the column is still folded and closed could be refused or open a flyout.
    UnfoldGroups(state);
    state.navigation = nullptr;
    state.pane = nullptr;
    state.toggle = nullptr;
    state.tracker.Forget();
    g_folded = false;
    g_over = false;
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
            // A handler runs inside Apple Music's XAML frame: nothing may escape it.
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
            HoldToggleFocus(live);
            if (auto pane = live.pane.get(); pane && live.keysUsed) RefocusToggle(live, pane);
        } catch (...) {
            // A handler runs inside Apple Music's XAML frame: nothing may escape it.
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
            Trace("hamburger clicked");
            if (!live.pinned) {
                // A click without the pointer on the button came from a key (or UI Automation). Its focus state does
                // not tell: WinUI has already moved the focus into the pane it opened.
                auto toggle = sender.try_as<xaml::FrameworkElement>();
                NoteKeyOpen(toggle && !CursorOver(live, toggle));
                return;
            }
            UnpinFromToggle(live);
        } catch (...) {
            // A handler runs inside Apple Music's XAML frame: nothing may escape it.
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
            HoldToggleFocus(live);
            if (live.keysUsed) Watch(live);
        } catch (...) {
            // A handler runs inside Apple Music's XAML frame: nothing may escape it.
        }
    }));
}

// Boxed once, so Release can take them off. Handled events too: what took Space marked it handled.
void MakeSpaceHandlers(State& state) {
    if (state.spaceDown) return;
    state.spaceDown = winrt::box_value(xaml::Input::KeyEventHandler(
        [](IInspectable const&, xaml::Input::KeyRoutedEventArgs const& args) noexcept {
            try {
                State& live = TheState();
                // The keyboard is on the hamburger, outside the pane, where WinUI's own Esc does not reach: folded here.
                if (args.Key() == winrt::Windows::System::VirtualKey::Escape) {
                    auto navigation = live.navigation.get();
                    auto pane = live.pane.get();
                    if (!navigation || !pane || !navigation.IsPaneOpen() || live.pinned) return;
                    ClosePane(live, navigation, pane);
                    args.Handled(true);
                    return;
                }
                if (MovesFocus(args.Key())) live.refocusUntil = {};  // Tab from the hamburger: the keyboard goes there
                if (args.Key() != winrt::Windows::System::VirtualKey::Space || args.KeyStatus().WasKeyDown) return;
                live.spacePressed = true;
                live.clicked = false;
            } catch (...) {
                // A handler runs inside Apple Music's XAML frame: nothing may escape it.
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
                // A handler runs inside Apple Music's XAML frame: nothing may escape it.
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
            // A handler runs inside Apple Music's XAML frame: nothing may escape it.
        }
    });
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
                Trace(std::string("pointer entered the pane, open ") + (navigation.IsPaneOpen() ? "already" : "now"));
                if (!navigation.IsPaneOpen()) navigation.IsPaneOpen(true);
                Watch(live);
            } catch (...) {
                // A handler runs inside Apple Music's XAML frame: nothing may escape it.
            }
        }));
    // PointerExited bubbles from each child the pointer leaves for its neighbour: the cursor decides.
    state.exited = winrt::box_value(xaml::Input::PointerEventHandler(
        [](IInspectable const&, xaml::Input::PointerRoutedEventArgs const&) noexcept {
            try {
                CloseIfLeft(TheState());
            } catch (...) {
                // A handler runs inside Apple Music's XAML frame: nothing may escape it.
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
                if (MovesFocus(args.Key())) live.refocusUntil = {};  // Tab in the pane: the keyboard stays where it went
            } catch (...) {
                // A handler runs inside Apple Music's XAML frame: nothing may escape it.
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

// Where the pane Ampify follows stands, each time that changes: a pane that never answers the pointer under a theme
// (Mirror) shows here whether Ampify still holds the one on screen.
void TraceState(const State& state, const controls::NavigationView& navigation) {
    if (!g_journal || g_traced >= kMostTraced) return;  // nothing more is written: no need to measure
    static std::string last;
    auto pane = state.pane.get();
    std::string line = "held, mode " + std::to_string(static_cast<int>(navigation.DisplayMode())) + ", " +
                       (navigation.IsPaneOpen() ? "open" : "folded");
    if (pane) {
        auto box = pane.TransformToVisual(nullptr).TransformBounds(
            {0, 0, static_cast<float>(pane.ActualWidth()), static_cast<float>(pane.ActualHeight())});
        line += ", pane " + std::to_string(std::lround(box.X)) + "," + std::to_string(std::lround(box.Y)) + " " +
                std::to_string(std::lround(box.Width)) + "x" + std::to_string(std::lround(box.Height)) +
                (pane.IsLoaded() ? "" : " unloaded") + (pane.IsHitTestVisible() ? "" : " not hit-testable");
    } else {
        line += ", no pane";
    }
    if (line == last) return;
    last = line;
    Trace(line);
}

}  // namespace

void KeepSidebarOnHover(Journal& journal) noexcept {
    try {
        g_journal = &journal;
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
                AddPin(state, navigation, MakePinHandler());
            }
            DressOverPages(state, navigation);  // a theme switched live shows or hides Apple's shadow, or paints the pane
            TraceState(state, navigation);
            return;
        }
        Trace(std::string("taken again, the last one ") + (state.navigation.get() ? "unloaded" : "gone"));
        Release(state);
        xaml::Window window = AppWindowOf(WindowRole::Main);
        xaml::UIElement content = window ? window.Content() : nullptr;
        if (!content) return;
        auto navigation = FindFirst<controls::NavigationView>(content, L"", 0);
        auto pane = navigation ? FindFirst<xaml::FrameworkElement>(navigation, L"PaneRoot", 0) : nullptr;
        if (!pane) return;
        state.window = WindowHandle(window);
        Take(state, navigation, pane);
        if (state.pinned) ApplyPin(state, false);
        if (!state.told) journal.Write("Sidebar: folded, it opens over the pages under the pointer");
        state.told = true;
    } catch (...) {
        // Nothing may escape into Apple Music's XAML frame.
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

bool SidebarOverPages() noexcept {
    return g_over;
}

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
