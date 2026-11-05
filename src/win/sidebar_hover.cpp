#include "sidebar_hover.hpp"

#if defined(AMPIFY_WITH_XAML)

#include "xaml_host.hpp"

#include "../core/journal.hpp"

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Microsoft.UI.Windowing.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string_view>

namespace ampify::win {

namespace {

namespace xaml = winrt::Microsoft::UI::Xaml;
namespace controls = xaml::Controls;
namespace media = xaml::Media;
namespace dispatching = winrt::Microsoft::UI::Dispatching;
using winrt::Windows::Foundation::IInspectable;
using PaneMode = controls::NavigationViewPaneDisplayMode;

constexpr int kSearchDepth = 32;  // the NavigationView and its pane sit some twenty levels under the window's content
constexpr auto kWatchEvery = std::chrono::milliseconds(200);  // the real cursor, while the pane is open

std::atomic<bool> g_enabled{false};
std::atomic<bool> g_folded{false};  // Take done, Release not yet

struct State {
    winrt::weak_ref<controls::NavigationView> navigation;
    winrt::weak_ref<xaml::FrameworkElement> pane;
    HWND window = nullptr;
    PaneMode apple = PaneMode::Auto;  // Apple's own mode, put back when turned off
    IInspectable entered{nullptr};    // the boxed handlers, kept to take them off again
    IInspectable exited{nullptr};
    dispatching::DispatcherQueueTimer watch{nullptr};
    bool told = false;
};

State& TheState() {
    static State state;
    return state;
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

// The real cursor, not the last pointer event: a menu takes the pointer while it is open, and a pointer that leaves in
// one jump or a window that comes over Apple Music sends no PointerExited.
bool CursorOverPane(const State& state, const xaml::FrameworkElement& pane) {
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
    return x >= 0 && y >= 0 && x < pane.ActualWidth() && y < pane.ActualHeight();
}

// A menu or flyout (a playlist's menu, the account's) is a light-dismiss popup; tooltips and Ampify's panels are not.
bool MenuOpen(const xaml::UIElement& element) {
    if (!element.XamlRoot()) return false;
    for (const auto& popup : media::VisualTreeHelper::GetOpenPopupsForXamlRoot(element.XamlRoot())) {
        if (popup.IsLightDismissEnabled()) return true;
    }
    return false;
}

// The search field of the pane has the keyboard: it stays while someone types, wherever the pointer went.
bool TypingInside(const xaml::FrameworkElement& pane) {
    if (!pane.XamlRoot()) return false;
    auto focused = xaml::Input::FocusManager::GetFocusedElement(pane.XamlRoot()).try_as<controls::TextBox>();
    for (xaml::DependencyObject at = focused; at; at = media::VisualTreeHelper::GetParent(at)) {
        if (at == pane) return true;
    }
    return false;
}

// Closes the pane once the pointer is off it and nothing holds it; true while it stays open.
bool CloseIfLeft(State& state) {
    auto navigation = state.navigation.get();
    auto pane = state.pane.get();
    if (!navigation || !pane || !navigation.IsPaneOpen()) return false;
    if (CursorOverPane(state, pane) || MenuOpen(pane) || TypingInside(pane)) return true;
    navigation.IsPaneOpen(false);
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

void Release(State& state) {
    if (state.watch) state.watch.Stop();
    if (auto pane = state.pane.get()) {
        try {
            if (state.entered) pane.RemoveHandler(xaml::UIElement::PointerEnteredEvent(), state.entered);
            if (state.exited) pane.RemoveHandler(xaml::UIElement::PointerExitedEvent(), state.exited);
        } catch (...) {
        }
    }
    if (auto navigation = state.navigation.get()) {
        try {
            navigation.PaneDisplayMode(state.apple);
            // Apple's wide mode shows the pane folded while it is closed, and does not open it again by itself.
            navigation.IsPaneOpen(navigation.DisplayMode() == controls::NavigationViewDisplayMode::Expanded);
        } catch (...) {
        }
    }
    state.navigation = nullptr;
    state.pane = nullptr;
    g_folded = false;
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
    g_folded = true;
}

}  // namespace

void KeepSidebarOnHover(Journal& journal) noexcept {
    try {
        State& state = TheState();
        if (!g_enabled) {
            if (state.navigation.get()) {
                Release(state);
                journal.Write("Sidebar: given back to Apple Music");
            }
            return;
        }
        // Cheap once in place: the tree is searched again only when Apple rebuilt it.
        if (auto navigation = state.navigation.get(); navigation && navigation.IsLoaded()) {
            // Apple puts its own mode back as the window changes size: fold it again.
            if (navigation.PaneDisplayMode() != PaneMode::LeftCompact) navigation.PaneDisplayMode(PaneMode::LeftCompact);
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
        if (!state.told) journal.Write("Sidebar: folded, it opens over the pages under the pointer");
        state.told = true;
    } catch (...) {
    }
}

void SetSidebarOnHover(bool enabled) noexcept {
    g_enabled = enabled;
}

bool SidebarKeptFolded() noexcept {
    return g_folded;
}

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
