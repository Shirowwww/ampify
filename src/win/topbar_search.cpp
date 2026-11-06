#include "topbar_search.hpp"

#if defined(AMPIFY_WITH_XAML)

#include "xaml_host.hpp"

#include "../core/journal.hpp"
#include "../core/topbar.hpp"

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Graphics.h>
#include <winrt/Windows.UI.h>
#include <winrt/Microsoft.UI.h>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Microsoft.UI.Windowing.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Automation.Peers.h>
#include <winrt/Microsoft.UI.Xaml.Automation.Provider.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ampify::win {

namespace {

namespace xaml = winrt::Microsoft::UI::Xaml;
namespace controls = xaml::Controls;
namespace primitives = xaml::Controls::Primitives;
namespace media = xaml::Media;
namespace dispatching = winrt::Microsoft::UI::Dispatching;
using winrt::Microsoft::UI::Input::InputNonClientPointerSource;
using winrt::Microsoft::UI::Input::NonClientRegionKind;
using winrt::Windows::Foundation::IInspectable;
using winrt::Windows::Graphics::RectInt32;

constexpr int kSearchDepth = 40;  // the transport bar sits some twenty-five levels under the window's content
constexpr auto kLayoutEvery = std::chrono::milliseconds(250);
constexpr wchar_t kBackGlyph[] = L"\xE72B";  // Back in Segoe Fluent Icons, as Apple's own arrow

std::atomic<bool> g_enabled{false};

// A local value written on one of Apple's elements, with the one before (or the unset value) to put back.
struct Saved {
    xaml::DependencyObject element{nullptr};
    xaml::DependencyProperty property{nullptr};
    IInspectable before{nullptr};
};

struct State {
    Journal* journal = nullptr;
    HWND window = nullptr;
    winrt::Microsoft::UI::WindowId windowId{};
    bool closed = false;  // the main window closed: nothing touches XAML after it
    winrt::weak_ref<controls::NavigationView> navigation;
    winrt::weak_ref<xaml::FrameworkElement> transport;  // Grid#TransportBar, looked for again when Apple rebuilds it
    // Apple's: the field's presenter, moved into the strip, and the panel it came from with its place there.
    xaml::FrameworkElement presenter{nullptr};
    controls::Panel area{nullptr};
    uint32_t presenterIndex = 0;
    controls::Button appleBack{nullptr};
    int64_t visibilityToken = 0;  // the presenter's Visibility callback, taken off by this token in Release
    std::vector<Saved> saved;
    // Ampify's: held strongly, since a weak reference to an element Ampify made never resolves (xaml-lessons.md).
    primitives::Popup popup{nullptr};
    controls::StackPanel strip{nullptr};
    controls::Button back{nullptr};
    controls::Grid field{nullptr};
    std::vector<DeviceRect> regions;  // the passthrough regions Ampify added last
    dispatching::DispatcherQueueTimer timer{nullptr};
    std::string note;      // the last line written, so each change is logged once
    bool refused = false;  // the field was not found: looked for again at the theme's next tick
};

// Never destroyed: XAML objects are released on the interface thread only, never by a static destructor.
State& TheState() {
    static State* state = new State();
    return *state;
}

void Note(State& state, const std::string& line) {
    if (state.note == line || !state.journal) return;
    state.note = line;
    state.journal->Write(line);
}

// Depth-first, the first element under `root` that is a T named `name` (any T when `name` is empty).
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

void WriteKept(State& state, const xaml::DependencyObject& element, const xaml::DependencyProperty& property,
               const IInspectable& value) {
    state.saved.push_back({element, property, element.ReadLocalValue(property)});
    element.SetValue(property, value);
}

// Last written, first put back, so a property written twice ends as Apple had it.
void PutBackAll(State& state) {
    for (auto it = state.saved.rbegin(); it != state.saved.rend(); ++it) {
        try {
            if (!it->before || it->before == xaml::DependencyProperty::UnsetValue()) {
                it->element.ClearValue(it->property);
            } else {
                it->element.SetValue(it->property, it->before);
            }
        } catch (...) {
        }
    }
    state.saved.clear();
}

// Presses Apple's back button, so Apple's own navigation goes back: the same as a click on it.
void PressAppleBack() noexcept {
    try {
        namespace peers = xaml::Automation::Peers;
        controls::Button apple = TheState().appleBack;
        auto peer = apple ? peers::FrameworkElementAutomationPeer::CreatePeerForElement(apple) : nullptr;
        auto pattern = peer ? peer.GetPattern(peers::PatternInterface::Invoke) : nullptr;
        if (auto invoke = pattern.try_as<xaml::Automation::Provider::IInvokeProvider>()) invoke.Invoke();
    } catch (...) {
    }
}

controls::Button MakeBack() {
    controls::Button back;
    back.Name(L"AMPIFY_TopbarBack");
    back.Width(kTopbarBackWidth);
    back.Height(kTopbarControlHeight);
    back.Padding(xaml::Thickness{0, 0, 0, 0});
    back.BorderThickness(xaml::Thickness{0, 0, 0, 0});
    back.Background(media::SolidColorBrush(winrt::Windows::UI::Colors::Transparent()));
    controls::FontIcon glyph;
    glyph.FontFamily(media::FontFamily(L"Segoe Fluent Icons, Segoe MDL2 Assets"));
    glyph.Glyph(kBackGlyph);
    glyph.FontSize(16);
    back.Content(glyph);
    back.Click([](IInspectable const&, xaml::RoutedEventArgs const&) noexcept { PressAppleBack(); });
    return back;
}

// The strip's elements are made once and kept: only the popup is new for each window, since its XamlRoot is set once.
void BuildStrip(State& state, const xaml::XamlRoot& root) {
    if (!state.strip) {
        state.back = MakeBack();
        state.field = controls::Grid();
        state.field.Name(L"AMPIFY_TopbarSearch");
        state.field.Height(kTopbarControlHeight);
        state.strip = controls::StackPanel();
        state.strip.Name(L"AMPIFY_Topbar");
        state.strip.Orientation(controls::Orientation::Horizontal);
        state.strip.Spacing(kTopbarGap);
        state.strip.Children().Append(state.back);
        state.strip.Children().Append(state.field);
    }
    state.popup = primitives::Popup();
    state.popup.XamlRoot(root);
    state.popup.Child(state.strip);
}

// The NavigationView's ClosedCompact state (the sidebar folded and closed) collapses the presenter and shows a search
// button in the column. While the field is in the strip the NavigationView stays in NotClosedCompact, an empty state:
// the presenter shows and the button stays collapsed, nothing else changes (WinUI's NavigationView.xaml).
void KeepPresenterShown(State& state) {
    auto navigation = state.navigation.get();
    if (!navigation || !state.presenter || state.presenter.Visibility() == xaml::Visibility::Visible) return;
    xaml::VisualStateManager::GoToState(navigation, L"NotClosedCompact", false);
}

// Apple's own state again, as the NavigationView works it out (NavigationView::UpdateIsClosedCompact).
void PutBackPaneState(const controls::NavigationView& navigation) {
    auto split = FindFirst<controls::SplitView>(navigation, L"RootSplitView", 0);
    if (!split) return;
    auto mode = split.DisplayMode();
    bool compact = mode == controls::SplitViewDisplayMode::CompactOverlay || mode == controls::SplitViewDisplayMode::CompactInline;
    xaml::VisualStateManager::GoToState(navigation, !split.IsPaneOpen() && compact ? L"ClosedCompact" : L"NotClosedCompact",
                                        false);
}

// The state change is answered after it, never inside WinUI's own GoToState.
void WatchPresenter(State& state) {
    auto changed = [](xaml::DependencyObject const&, xaml::DependencyProperty const&) noexcept {
        auto queue = dispatching::DispatcherQueue::GetForCurrentThread();
        if (!queue) return;
        queue.TryEnqueue(dispatching::DispatcherQueuePriority::High, dispatching::DispatcherQueueHandler([]() noexcept {
            try {
                KeepPresenterShown(TheState());
            } catch (...) {
            }
        }));
    };
    state.visibilityToken = state.presenter.RegisterPropertyChangedCallback(xaml::UIElement::VisibilityProperty(), changed);
}

// The empty search row of the pane and Apple's back arrow stop showing; each value goes back as it was. The arrow
// keeps its place, so the sidebar's header does not move.
void HideApples(State& state, const controls::NavigationView& navigation) {
    WriteKept(state, state.area, xaml::UIElement::VisibilityProperty(), winrt::box_value(xaml::Visibility::Collapsed));
    state.appleBack = FindFirst<controls::Button>(navigation, L"NavigationViewBackButton", 0);
    if (state.appleBack) {
        WriteKept(state, state.appleBack, xaml::UIElement::OpacityProperty(), winrt::box_value(0.0));
        WriteKept(state, state.appleBack, xaml::UIElement::IsHitTestVisibleProperty(), winrt::box_value(false));
        WriteKept(state, state.appleBack, xaml::UIElement::IsTabStopProperty(), winrt::box_value(false));
    }
}

// Apple's presenter of the search box leaves the pane for the strip. The box inside it stays Apple's object, with its
// handlers; the data context it inherited goes with it, so its bindings keep their source.
bool Take(State& state, const xaml::UIElement& content, const controls::NavigationView& navigation) {
    auto presenter = FindFirst<xaml::FrameworkElement>(navigation, L"PaneAutoSuggestBoxPresenter", 0);
    auto area = presenter ? media::VisualTreeHelper::GetParent(presenter).try_as<controls::Panel>() : nullptr;
    uint32_t index = 0;
    if (!area || !area.Children().IndexOf(presenter, index)) return false;
    BuildStrip(state, content.XamlRoot());
    state.field.DataContext(presenter.DataContext());
    area.Children().RemoveAt(index);
    try {
        state.field.Children().Append(presenter);
    } catch (...) {
        area.Children().InsertAt(index, presenter);  // never leave Apple's field out of every tree
        state.popup = nullptr;
        return false;
    }
    state.presenter = presenter;
    state.area = area;
    state.presenterIndex = index;
    state.navigation = winrt::make_weak(navigation);
    HideApples(state, navigation);
    WatchPresenter(state);
    KeepPresenterShown(state);
    state.popup.IsOpen(true);
    return true;
}

std::vector<RectInt32> ToRects(const std::vector<DeviceRect>& rects) {
    std::vector<RectInt32> out;
    for (const DeviceRect& rect : rects) out.push_back(RectInt32{rect.x, rect.y, rect.width, rect.height});
    return out;
}

// Ampify's regions replace its previous ones among the window's passthrough regions; Apple's stay. Apple sets its own
// again as its layout changes, so this runs on every layout step, and writes only when something is missing.
void SetRegions(State& state, const std::vector<DeviceRect>& now) {
    try {
        auto source = InputNonClientPointerSource::GetForWindowId(state.windowId);
        std::vector<DeviceRect> current;
        for (const RectInt32& rect : source.GetRegionRects(NonClientRegionKind::Passthrough)) {
            current.push_back(DeviceRect{rect.X, rect.Y, rect.Width, rect.Height});
        }
        std::vector<DeviceRect> merged = MergeRegions(current, state.regions, now);
        if (merged != current) source.SetRegionRects(NonClientRegionKind::Passthrough, ToRects(merged));
        state.regions = now;
    } catch (const winrt::hresult_error& error) {
        Note(state, "Top bar: no passthrough regions (" + HresultText(error.code()) + ")");
    }
}

void GiveBackPresenter(State& state) {
    if (!state.presenter) return;
    uint32_t at = 0;
    if (state.field && state.field.Children().IndexOf(state.presenter, at)) state.field.Children().RemoveAt(at);
    if (state.area && !media::VisualTreeHelper::GetParent(state.presenter)) {
        uint32_t size = state.area.Children().Size();
        state.area.Children().InsertAt(std::min(state.presenterIndex, size), state.presenter);
    }
}

// Everything back as Apple had it: the regions, the presenter in its place, then the values written on Apple's elements.
void Release(State& state) {
    try {
        if (!state.regions.empty()) SetRegions(state, {});
    } catch (...) {
    }
    try {
        if (state.presenter && state.visibilityToken != 0) {
            state.presenter.UnregisterPropertyChangedCallback(xaml::UIElement::VisibilityProperty(), state.visibilityToken);
        }
    } catch (...) {
    }
    state.visibilityToken = 0;
    try {
        GiveBackPresenter(state);
        if (auto navigation = state.navigation.get()) PutBackPaneState(navigation);
    } catch (...) {
    }
    PutBackAll(state);
    try {
        if (state.popup) {
            state.popup.IsOpen(false);
            state.popup.Child(nullptr);
        }
    } catch (...) {
    }
    state.popup = nullptr;
    state.presenter = nullptr;
    state.area = nullptr;
    state.appleBack = nullptr;
    state.regions.clear();
}

bool Taken(const State& state) {
    return static_cast<bool>(state.presenter);
}

// Still in place: Apple's sidebar loaded, and the presenter still in the strip (Apple did not rebuild its template).
bool StillHeld(const State& state) {
    auto navigation = state.navigation.get();
    return navigation && navigation.IsLoaded() && media::VisualTreeHelper::GetParent(state.presenter) == state.field;
}

// Apple's own layout and a theme's top bar both put the transport bar in the strip; a missing bar counts as there.
// Unknown when the bar must be looked for again and `search` forbids walking the tree.
std::optional<bool> TransportOnTop(State& state, const xaml::UIElement& content, bool search) {
    auto bar = state.transport.get();
    if (!bar || !bar.IsLoaded()) {
        if (!search) return std::nullopt;
        bar = FindFirst<xaml::FrameworkElement>(content, L"TransportBar", 0);
        state.transport = bar ? winrt::make_weak(bar) : winrt::weak_ref<xaml::FrameworkElement>{};
    }
    if (!bar) return true;
    bool visible = bar.Visibility() == xaml::Visibility::Visible && bar.ActualHeight() > 0;
    return TransportAtTop(visible, bar.TransformToVisual(content).TransformPoint({0, 0}).Y);
}

// Apple's NavigationView, kept between takes; looked for again only when `search` allows a walk of the whole tree.
controls::NavigationView Navigation(State& state, const xaml::UIElement& content, bool search) {
    auto navigation = state.navigation.get();
    if ((!navigation || !navigation.IsLoaded()) && search) {
        navigation = FindFirst<controls::NavigationView>(content, L"", 0);
        state.navigation = navigation ? winrt::make_weak(navigation) : winrt::weak_ref<controls::NavigationView>{};
    }
    return navigation && navigation.IsLoaded() ? navigation : nullptr;
}

// The suggestions open under the field, partly inside the title bar: their list passes through as well while open.
void AddSuggestions(const State& state, double scale, std::vector<DeviceRect>& rects) {
    auto popup = FindFirst<primitives::Popup>(state.presenter, L"SuggestionsPopup", 0);
    auto list = popup && popup.IsOpen() ? popup.Child().try_as<xaml::FrameworkElement>() : nullptr;
    if (!list) return;
    winrt::Windows::Foundation::Point origin = list.TransformToVisual(nullptr).TransformPoint({0, 0});
    rects.push_back(ToDeviceRect(TopbarRect{origin.X, origin.Y, list.ActualWidth(), list.ActualHeight()}, scale));
}

// Places the strip for the window's width, follows Apple's back button and mode, and sets the passthrough regions in
// device pixels at the window's current scale.
void LayOut(State& state, const xaml::UIElement& content) {
    KeepPresenterShown(state);
    xaml::XamlRoot root = content.XamlRoot();
    const double scale = root.RasterizationScale();
    const TopbarLayout layout = LayOutTopbar(root.Size().Width);
    state.popup.HorizontalOffset(layout.back.x);
    state.popup.VerticalOffset(layout.back.y);
    state.field.Width(layout.search.width);
    if (auto page = content.try_as<xaml::FrameworkElement>()) state.strip.RequestedTheme(page.ActualTheme());
    if (state.area) state.field.DataContext(state.area.DataContext());
    controls::Button apple = state.appleBack;
    state.back.IsEnabled(apple && apple.IsEnabled() && apple.Visibility() == xaml::Visibility::Visible);
    if (apple) {
        winrt::hstring name = xaml::Automation::AutomationProperties::GetName(apple);
        xaml::Automation::AutomationProperties::SetName(state.back, name.empty() ? winrt::hstring(L"Back") : name);
    }
    std::vector<DeviceRect> rects{ToDeviceRect(layout.back, scale), ToDeviceRect(layout.search, scale)};
    AddSuggestions(state, scale, rects);
    SetRegions(state, rects);
}

// Why the strip does not show, for the log: empty when it shows, nullopt when that is not known yet.
std::optional<std::string> HiddenBecause(State& state, const xaml::UIElement& content, bool search) {
    std::optional<bool> atTop = TransportOnTop(state, content, search);
    if (!atTop) return std::nullopt;
    if (*atTop) return "the playback bar is at the top";
    if (!TopbarShown(true, false, content.XamlRoot().Size().Width)) {
        return "the window is narrower than " + std::to_string(static_cast<int>(kTopbarMinWidth)) + " px";
    }
    return std::string();
}

void WatchClose(State& state, const xaml::Window& window) {
    HWND hwnd = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(window.AppWindow().Id().Value));
    if (hwnd == state.window) return;
    state.window = hwnd;
    state.windowId = window.AppWindow().Id();
    window.Closed([](IInspectable const&, xaml::WindowEventArgs const&) noexcept {
        State& live = TheState();
        live.closed = true;
        if (live.timer) live.timer.Stop();
    });
}

// One look: give back when turned off or hidden, take when shown, then lay out. Cheap once in place: the whole tree is
// walked only with `search` (the theme's tick), never on the quarter-second timer.
void Step(State& state, bool search) {
    if (state.closed) return;
    if (!g_enabled) {
        if (Taken(state)) Release(state);
        if (state.timer) state.timer.Stop();
        if (!state.note.empty()) Note(state, "Top bar: off, Apple Music's search field is back in its sidebar");
        return;
    }
    xaml::Window window = AppWindow(L"MainWindow");
    xaml::UIElement content = window ? window.Content() : nullptr;
    if (!content || !content.XamlRoot()) return;
    WatchClose(state, window);
    if (!IsWindowVisible(state.window) || IsIconic(state.window)) return;
    if (Taken(state) && !StillHeld(state)) Release(state);
    std::optional<std::string> why = HiddenBecause(state, content, search);
    if (!why) return;
    if (!why->empty()) {
        if (Taken(state)) Release(state);
        Note(state, "Top bar: hidden, " + *why);
        return;
    }
    if (search) state.refused = false;
    if (!Taken(state)) {
        controls::NavigationView navigation{nullptr};
        if (!state.refused) navigation = Navigation(state, content, search);
        if (!navigation) return;
        if (!Take(state, content, navigation)) {
            state.refused = true;  // tried again at the theme's next tick, not four times a second
            Note(state, "Top bar: Apple Music's search field was not found in its sidebar");
            return;
        }
    }
    LayOut(state, content);
    Note(state, "Top bar: Apple Music's search field and a back arrow in the strip at the top");
}

void StartTimer(State& state) {
    if (!state.timer) {
        state.timer = dispatching::DispatcherQueue::GetForCurrentThread().CreateTimer();
        state.timer.Interval(kLayoutEvery);
        state.timer.Tick([](auto const&, auto const&) noexcept {
            try {
                Step(TheState(), false);
            } catch (...) {
            }
        });
    }
    if (!state.timer.IsRunning()) state.timer.Start();
}

}  // namespace

void KeepTopbarSearch(Journal& journal) noexcept {
    try {
        State& state = TheState();
        state.journal = &journal;
        if (state.closed) return;
        if (g_enabled) StartTimer(state);
        Step(state, true);
    } catch (...) {
    }
}

void SetTopbarSearch(bool enabled) noexcept {
    g_enabled = enabled;
}

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
