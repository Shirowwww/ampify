#include "topbar_search.hpp"

#if defined(AMPIFY_WITH_XAML)

#include "../core/i18n.hpp"
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
#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>

// After the projection: windows.h, which they bring, renames IAutomationPeer::GetClassName.
#include "win_common.hpp"
#include "xaml_backend_detail.hpp"
#include "xaml_chrome.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
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

// A property callback registered on one of Apple's elements, taken off by its token in Release.
struct Watched {
    xaml::DependencyObject element{nullptr};
    xaml::DependencyProperty property{nullptr};
    int64_t token = 0;
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
    xaml::VisualState closedCompact{nullptr};  // Apple's state, and the setter taken out of it (TakePresenterSetter)
    xaml::SetterBase presenterSetter{nullptr};
    uint32_t presenterSetterIndex = 0;
    PropertyLedger saved;  // Apple's local values that the top bar replaced
    // Apple's sidebar header and its title, kept where Apple lays them out without its arrow (ArrowRoom).
    xaml::FrameworkElement header{nullptr};
    xaml::FrameworkElement title{nullptr};
    ArrowRoom headerRoom{kAppleHeaderLeft};
    ArrowRoom titleRoom{kAppleTitleLeft};
    std::vector<Watched> watched;
    // Ampify's: held strongly, since a weak reference to an element Ampify made never resolves (xaml-lessons.md).
    primitives::Popup popup{nullptr};
    controls::Grid host{nullptr};      // the window's width, the strip's height: the veil, then the strip over it
    controls::Border veil{nullptr};
    controls::StackPanel strip{nullptr};
    controls::Button back{nullptr};
    controls::Grid field{nullptr};
    controls::Border fieldBacking{nullptr};  // under Apple's box, in the field's cell
    winrt::weak_ref<xaml::FrameworkElement> pageRoot;  // Apple's Grid#PageRoot, which carries a theme's picture
    winrt::weak_ref<xaml::FrameworkElement> pages;     // Apple's page panel, whose top ends the strip (StripHeight)
    controls::Control box{nullptr};    // Apple's TextBox, whose corners the backing follows
    std::optional<std::pair<bool, bool>> backed;  // the backing last set: a picture under the strip, the dark mode
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

// Depth-first; an empty `name` matches any T.
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

// Invokes Apple's back button through its automation peer, so Apple's own navigation goes back.
void PressAppleBack() noexcept {
    try {
        namespace peers = xaml::Automation::Peers;
        controls::Button apple = TheState().appleBack;
        auto peer = apple ? peers::FrameworkElementAutomationPeer::CreatePeerForElement(apple) : nullptr;
        auto pattern = peer ? peer.GetPattern(peers::PatternInterface::Invoke) : nullptr;
        if (auto invoke = pattern.try_as<xaml::Automation::Provider::IInvokeProvider>()) invoke.Invoke();
    } catch (...) {
        // Nothing may escape into Apple Music's XAML frame.
    }
}

// A flat arrow with the subtle fill of Apple's own under the pointer (SubtleButtonResources): the default Button's
// near-white fill did not show on Glass's light strip.
controls::Button MakeBack() {
    const std::wstring text =
        L"<Button xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation' "
        L"xmlns:x='http://schemas.microsoft.com/winfx/2006/xaml' Padding='0' BorderThickness='0'>" +
        SubtleButtonResources() + L"</Button>";
    controls::Button back = xaml::Markup::XamlReader::Load(text).as<controls::Button>();
    back.Name(L"AMPIFY_TopbarBack");
    back.Width(kTopbarBackWidth);
    back.Height(kTopbarControlHeight);
    controls::FontIcon glyph;
    glyph.FontFamily(media::FontFamily(L"Segoe Fluent Icons, Segoe MDL2 Assets"));
    glyph.Glyph(kBackGlyph);
    glyph.FontSize(16);
    back.Content(glyph);
    back.Click([](IInspectable const&, xaml::RoutedEventArgs const&) noexcept { PressAppleBack(); });
    return back;
}

// Decoration only: it never takes the pointer, so the caption region and the pages under it keep their clicks.
controls::Border MakeBacking(const wchar_t* name) {
    controls::Border backing;
    backing.Name(name);
    backing.IsHitTestVisible(false);
    backing.Visibility(xaml::Visibility::Collapsed);
    xaml::Automation::AutomationProperties::SetAccessibilityView(backing, xaml::Automation::Peers::AccessibilityView::Raw);
    return backing;
}

controls::StackPanel MakeStrip(State& state) {
    controls::StackPanel strip;
    strip.Name(L"AMPIFY_Topbar");
    strip.Orientation(controls::Orientation::Horizontal);
    strip.Spacing(kTopbarGap);
    strip.HorizontalAlignment(xaml::HorizontalAlignment::Left);
    strip.VerticalAlignment(xaml::VerticalAlignment::Top);
    strip.Children().Append(state.back);
    strip.Children().Append(state.field);
    return strip;
}

// The strip's elements are made once and kept: only the popup is new for each window, since its XamlRoot is set once.
void BuildStrip(State& state, const xaml::XamlRoot& root) {
    if (!state.host) {
        state.back = MakeBack();
        state.fieldBacking = MakeBacking(L"AMPIFY_TopbarSearchBacking");
        state.field = controls::Grid();
        state.field.Name(L"AMPIFY_TopbarSearch");
        state.field.Height(kTopbarControlHeight);
        state.field.Children().Append(state.fieldBacking);
        state.strip = MakeStrip(state);
        state.veil = MakeBacking(L"AMPIFY_TopbarVeil");
        state.host = controls::Grid();
        state.host.Height(kTopbarHeight);
        state.host.Children().Append(state.veil);
        state.host.Children().Append(state.strip);
    }
    state.popup = primitives::Popup();
    state.popup.XamlRoot(root);
    state.popup.Child(state.host);
}

// Fallback when the presenter's setter could not be taken out of ClosedCompact (TakePresenterSetter): the NavigationView
// is held in NotClosedCompact. Apple's template also shows the hamburger and resizes the header in ClosedCompact, so the
// folded sidebar loses its hamburger meanwhile.
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

// Apple's ClosedCompact state, in the PaneStateGroup of its NavigationView template, or null.
xaml::VisualState ClosedCompactState(const controls::NavigationView& navigation) {
    if (media::VisualTreeHelper::GetChildrenCount(navigation) == 0) return nullptr;
    auto root = media::VisualTreeHelper::GetChild(navigation, 0).try_as<xaml::FrameworkElement>();
    if (!root) return nullptr;
    for (const xaml::VisualStateGroup& group : xaml::VisualStateManager::GetVisualStateGroups(root)) {
        if (group.Name() != L"PaneStateGroup") continue;
        for (const xaml::VisualState& visual : group.States()) {
            if (visual.Name() == L"ClosedCompact") return visual;
        }
    }
    return nullptr;
}

bool Collapses(const xaml::Setter& setter) {
    IInspectable value = setter.Value();
    std::optional<int> number;
    if (auto visibility = value.try_as<winrt::Windows::Foundation::IReference<xaml::Visibility>>()) {
        number = static_cast<int>(visibility.Value());
    } else if (auto boxed = value.try_as<winrt::Windows::Foundation::IPropertyValue>();
               boxed && boxed.Type() == winrt::Windows::Foundation::PropertyType::Int32) {
        number = boxed.GetInt32();
    }
    return VisibilityCollapses(number, winrt::unbox_value_or<winrt::hstring>(value, L""));
}

// Each part is read on its own: the target of a template's setter often does not resolve (StateSetterLook), and that
// failure must not end the search.
StateSetterLook LookAt(const xaml::SetterBase& base, const xaml::FrameworkElement& presenter) {
    StateSetterLook look;
    auto setter = base.try_as<xaml::Setter>();
    auto target = setter ? setter.Target() : nullptr;
    auto path = target ? target.Path() : nullptr;
    if (!path || path.Path() != L"Visibility") return look;
    look.visibility = true;
    try {
        look.targetsPresenter = target.Target() == presenter;
    } catch (const winrt::hresult_error&) {
    }
    try {
        look.collapses = Collapses(setter);
    } catch (const winrt::hresult_error&) {
    }
    return look;
}

// ClosedCompact (the sidebar folded and closed) collapses the presenter, and in Apple's template also shows the
// hamburger and a search button and sets the header's height. Only the presenter's setter leaves the state while the
// field is in the strip, so the folded sidebar keeps the rest. The state is left before the setter goes, so WinUI
// undoes the collapse it applied; false when the setter was not found or the collection refused the change.
bool TakePresenterSetter(State& state, const controls::NavigationView& navigation) {
    try {
        xaml::VisualState visual = ClosedCompactState(navigation);
        auto setters = visual ? visual.Setters() : nullptr;
        if (!setters) return false;
        std::vector<StateSetterLook> looks;
        for (const xaml::SetterBase& setter : setters) looks.push_back(LookAt(setter, state.presenter));
        std::optional<std::size_t> found = PresenterSetterIndex(looks);
        if (!found) {
            Note(state, "Top bar: the search field's setter was not found in Apple Music's folded sidebar state");
            return false;
        }
        uint32_t i = static_cast<uint32_t>(*found);
        xaml::SetterBase setter = setters.GetAt(i);
        xaml::VisualStateManager::GoToState(navigation, L"NotClosedCompact", false);
        setters.RemoveAt(i);
        state.closedCompact = visual;
        state.presenterSetter = setter;
        state.presenterSetterIndex = i;
        PutBackPaneState(navigation);
        return true;
    } catch (const winrt::hresult_error& error) {
        Note(state, "Top bar: Apple Music's folded sidebar state kept as it is (" + HresultText(error.code()) + ")");
    }
    return false;
}

// The setter back in its place, entered again from NotClosedCompact so a folded sidebar collapses the presenter anew.
void PutBackPresenterSetter(State& state, const controls::NavigationView& navigation) {
    if (!state.closedCompact || !state.presenterSetter) return;
    if (navigation) xaml::VisualStateManager::GoToState(navigation, L"NotClosedCompact", false);
    auto setters = state.closedCompact.Setters();
    setters.InsertAt(std::min(state.presenterSetterIndex, setters.Size()), state.presenterSetter);
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
                // A handler runs inside Apple Music's XAML frame: nothing may escape it.
            }
        }));
    };
    state.visibilityToken = state.presenter.RegisterPropertyChangedCallback(xaml::UIElement::VisibilityProperty(), changed);
}

// The empty search row of the pane and Apple's back arrow stop showing; each value goes back as it was. The arrow
// keeps its place, so the sidebar's header does not move.
void HideApples(State& state, const controls::NavigationView& navigation) {
    state.saved.Write(state.area, xaml::UIElement::VisibilityProperty(), winrt::box_value(xaml::Visibility::Collapsed));
    state.appleBack = FindFirst<controls::Button>(navigation, L"NavigationViewBackButton", 0);
    if (state.appleBack) {
        state.saved.Write(state.appleBack, xaml::UIElement::OpacityProperty(), winrt::box_value(0.0));
        state.saved.Write(state.appleBack, xaml::UIElement::IsHitTestVisibleProperty(), winrt::box_value(false));
        state.saved.Write(state.appleBack, xaml::UIElement::IsTabStopProperty(), winrt::box_value(false));
    }
}

bool AppleArrowShows(const State& state) {
    return state.appleBack && state.appleBack.Visibility() == xaml::Visibility::Visible;
}

void SetLeft(const xaml::FrameworkElement& element, std::optional<double> left) {
    if (!element || !left) return;
    xaml::Thickness margin = element.Margin();
    margin.Left = *left;
    element.Margin(margin);
}

// Apple's code moves the header right for its arrow, and the title closer to the logo, as soon as there is a page to
// go back to: the logo jumped some 30 pixels after the first navigation, beside an arrow that shows in the strip.
void HoldHeader(State& state) {
    const bool arrow = AppleArrowShows(state);
    if (state.header) SetLeft(state.header, state.headerRoom.Next(arrow, state.header.Margin().Left));
    if (state.title) SetLeft(state.title, state.titleRoom.Next(arrow, state.title.Margin().Left));
}

// Apple writes the arrow's visibility, then both margins, in one go: the hold looks once that is over.
void HoldHeaderLater() {
    auto queue = dispatching::DispatcherQueue::GetForCurrentThread();
    if (!queue) return;
    queue.TryEnqueue(dispatching::DispatcherQueueHandler([]() noexcept {
        try {
            State& live = TheState();
            if (!live.closed && live.header) HoldHeader(live);
        } catch (...) {
            // A handler runs inside Apple Music's XAML frame: nothing may escape it.
        }
    }));
}

void WatchFor(State& state, const xaml::DependencyObject& element, const xaml::DependencyProperty& property) {
    if (!element) return;
    auto changed = [](xaml::DependencyObject const&, xaml::DependencyProperty const&) noexcept { HoldHeaderLater(); };
    state.watched.push_back({element, property, element.RegisterPropertyChangedCallback(property, changed)});
}

void TakeHeader(State& state, const controls::NavigationView& navigation) {
    state.header = FindFirst<xaml::FrameworkElement>(navigation, L"PaneHeaderGrid", 0);
    state.title = state.header ? FindFirst<xaml::FrameworkElement>(state.header, L"AppName", 0) : nullptr;
    WatchFor(state, state.appleBack, xaml::UIElement::VisibilityProperty());
    WatchFor(state, state.header, xaml::FrameworkElement::MarginProperty());
    WatchFor(state, state.title, xaml::FrameworkElement::MarginProperty());
    HoldHeader(state);
}

// The callbacks off, and Apple's room for its arrow back while the arrow shows, since it shows there again.
void GiveBackHeader(State& state) {
    for (const Watched& watched : state.watched) {
        try {
            watched.element.UnregisterPropertyChangedCallback(watched.property, watched.token);
        } catch (...) {
            // The element may be torn down already: nothing is left to put back.
        }
    }
    state.watched.clear();
    try {
        const bool arrow = AppleArrowShows(state);
        SetLeft(state.header, state.headerRoom.Restore(arrow));
        SetLeft(state.title, state.titleRoom.Restore(arrow));
    } catch (...) {
        // The element may be torn down already: nothing is left to put back.
    }
    state.header = nullptr;
    state.title = nullptr;
    state.headerRoom = ArrowRoom{kAppleHeaderLeft};
    state.titleRoom = ArrowRoom{kAppleTitleLeft};
}

// The popup without the strip, as the veil alone leaves it (KeepVeilOnly).
void CloseVeil(State& state) {
    if (!state.popup) return;
    state.popup.IsOpen(false);
    state.popup.Child(nullptr);
    state.popup = nullptr;
}

// Apple's presenter of the search box leaves the pane for the strip. The box inside it stays Apple's object, with its
// handlers; the data context it inherited goes with it, so its bindings keep their source.
bool Take(State& state, const xaml::UIElement& content, const controls::NavigationView& navigation) {
    auto presenter = FindFirst<xaml::FrameworkElement>(navigation, L"PaneAutoSuggestBoxPresenter", 0);
    auto area = presenter ? media::VisualTreeHelper::GetParent(presenter).try_as<controls::Panel>() : nullptr;
    uint32_t index = 0;
    if (!area || !area.Children().IndexOf(presenter, index)) return false;
    CloseVeil(state);  // the host goes to a new popup, and an element has one parent
    BuildStrip(state, content.XamlRoot());
    state.strip.Visibility(xaml::Visibility::Visible);
    state.field.DataContext(presenter.DataContext());
    area.Children().RemoveAt(index);
    try {
        state.field.Children().Append(presenter);
    } catch (...) {
        area.Children().InsertAt(index, presenter);  // never leave Apple's field out of every tree
        CloseVeil(state);
        return false;
    }
    state.presenter = presenter;
    // In the pane, the presenter keeps 10 on each side: in the strip, the box fills the field's cell, its frame then
    // matching the region that takes the pointer, 8 from the arrow.
    state.saved.Write(presenter, xaml::FrameworkElement::MarginProperty(), winrt::box_value(xaml::Thickness{0, 0, 0, 0}));
    state.box = FindFirst<controls::TextBox>(presenter, L"", 0);
    state.area = area;
    state.presenterIndex = index;
    state.navigation = winrt::make_weak(navigation);
    HideApples(state, navigation);
    TakeHeader(state, navigation);
    if (!TakePresenterSetter(state, navigation)) WatchPresenter(state);
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
        // The element may be torn down already: nothing is left to put back.
    }
    try {
        if (state.presenter && state.visibilityToken != 0) {
            state.presenter.UnregisterPropertyChangedCallback(xaml::UIElement::VisibilityProperty(), state.visibilityToken);
        }
    } catch (...) {
        // The element may be torn down already: nothing is left to put back.
    }
    state.visibilityToken = 0;
    GiveBackHeader(state);
    try {
        GiveBackPresenter(state);
        auto navigation = state.navigation.get();
        PutBackPresenterSetter(state, navigation);
        if (navigation) PutBackPaneState(navigation);
    } catch (...) {
        // The element may be torn down already: nothing is left to put back.
    }
    state.saved.PutBackAll();
    try {
        CloseVeil(state);
    } catch (...) {
        // The element may be torn down already: nothing is left to put back.
    }
    state.popup = nullptr;  // even when closing threw
    state.presenter = nullptr;
    state.box = nullptr;
    state.area = nullptr;
    state.appleBack = nullptr;
    state.closedCompact = nullptr;
    state.presenterSetter = nullptr;
    state.regions.clear();
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

// The caption buttons' height as Windows draws them now (Apple may ask for a tall title bar); the standard one if unread.
double CaptionHeight(const State& state, double scale) {
    try {
        auto window = winrt::Microsoft::UI::Windowing::AppWindow::GetFromWindowId(state.windowId);
        return TopbarCaptionHeight(window ? window.TitleBar().Height() : 0, scale);
    } catch (const winrt::hresult_error&) {
        return kTopbarCaptionHeight;
    }
}

// The strip a theme leaves above the pages: where its page panel (Apple's Grid#ContentRoot in the pages' split view)
// starts. Looked for again only when `search` allows a walk of the tree; the standard strip while it is not known.
double StripHeight(State& state, const xaml::UIElement& content, bool search) {
    auto pages = state.pages.get();
    if ((!pages || !pages.IsLoaded()) && search) {
        auto split = FindFirst<controls::SplitView>(content, L"ContentSplitView", 0);
        pages = split ? FindFirst<xaml::FrameworkElement>(split, L"ContentRoot", 0) : nullptr;
        state.pages = pages ? winrt::make_weak(pages) : winrt::weak_ref<xaml::FrameworkElement>{};
    }
    if (!pages || !pages.IsLoaded()) return kTopbarHeight;
    return pages.TransformToVisual(content).TransformPoint({0, 0}).Y;
}

// Places the strip for the window's width, follows Apple's back button and mode, and sets the passthrough regions in
// device pixels at the window's current scale.
void LayOut(State& state, const xaml::UIElement& content, bool search) {
    KeepPresenterShown(state);
    xaml::XamlRoot root = content.XamlRoot();
    const double scale = root.RasterizationScale();
    const TopbarLayout layout =
        LayOutTopbar(root.Size().Width, CaptionHeight(state, scale), StripHeight(state, content, search));
    state.popup.HorizontalOffset(0);
    state.popup.VerticalOffset(0);
    state.host.Width(root.Size().Width);
    state.strip.Margin(xaml::Thickness{layout.back.x, layout.back.y, 0, 0});
    state.field.Width(layout.search.width);
    if (auto page = content.try_as<xaml::FrameworkElement>()) state.strip.RequestedTheme(page.ActualTheme());
    if (state.area) state.field.DataContext(state.area.DataContext());
    controls::Button apple = state.appleBack;
    state.back.IsEnabled(apple && apple.IsEnabled() && apple.Visibility() == xaml::Visibility::Visible);
    if (apple) {
        winrt::hstring name = xaml::Automation::AutomationProperties::GetName(apple);
        xaml::Automation::AutomationProperties::SetName(state.back, name.empty() ? winrt::to_hstring(i18n::Text("page.back")) : name);
    }
    std::vector<DeviceRect> rects{ToDeviceRect(layout.back, scale), ToDeviceRect(layout.search, scale)};
    AddSuggestions(state, scale, rects);
    SetRegions(state, rects);
}

winrt::Windows::UI::Color ToColor(theme::Argb colour) {
    return winrt::Windows::UI::Color{colour.a, colour.r, colour.g, colour.b};
}

// Whether a theme's picture reaches the strip: an image or gradient brush on Apple's page root, or a ::before or
// ::after image on it. Unknown while the root is lost and `search` forbids walking the tree, so nothing flickers.
std::optional<bool> PictureUnderStrip(State& state, const xaml::UIElement& content, bool search) {
    auto root = state.pageRoot.get();
    if ((!root || !root.IsLoaded()) && search) {
        auto own = content.try_as<xaml::FrameworkElement>();
        root = own && own.Name() == L"PageRoot" ? own : FindFirst<xaml::FrameworkElement>(content, L"PageRoot", 0);
        state.pageRoot = root ? winrt::make_weak(root) : winrt::weak_ref<xaml::FrameworkElement>{};
    }
    auto panel = root && root.IsLoaded() ? root.try_as<controls::Panel>() : nullptr;
    if (!panel) return std::nullopt;
    media::Brush brush = panel.Background();
    if (brush && (brush.try_as<media::ImageBrush>() || brush.try_as<media::GradientBrush>())) return true;
    for (const xaml::UIElement& child : panel.Children()) {
        if (xaml_detail::IsDecor(child) && child.Visibility() == xaml::Visibility::Visible && child.Opacity() > 0) return true;
    }
    return false;
}

// The veil's colour, faded by TopbarVeilShare from the window's top to the strip's foot.
media::LinearGradientBrush VeilBrush(theme::Argb colour) {
    media::LinearGradientBrush brush;
    brush.StartPoint({0, 0});
    brush.EndPoint({0, 1});
    for (double y : {0.0, kTopbarVeilHold, kTopbarHeight}) {
        theme::Argb stop = colour;
        stop.a = static_cast<std::uint8_t>(std::lround(colour.a * TopbarVeilShare(y)));
        media::GradientStop gradient;
        gradient.Color(ToColor(stop));
        gradient.Offset(y / kTopbarHeight);
        brush.GradientStops().Append(gradient);
    }
    return brush;
}

// The backing covers Apple's box exactly, corners included: the presenter insets the box a few pixels in the cell, and a
// backing filling the cell showed as a second, darker frame around it.
void FollowBox(State& state) {
    if (!state.box || !state.box.IsLoaded()) return;
    if (state.fieldBacking.CornerRadius() != state.box.CornerRadius()) {
        state.fieldBacking.CornerRadius(state.box.CornerRadius());
    }
    const winrt::Windows::Foundation::Point at = state.box.TransformToVisual(state.field).TransformPoint({0, 0});
    const double right = state.field.ActualWidth() - at.X - state.box.ActualWidth();
    const double bottom = state.field.ActualHeight() - at.Y - state.box.ActualHeight();
    const xaml::Thickness margin{std::max(0.0, double(at.X)), std::max(0.0, double(at.Y)), std::max(0.0, right),
                                 std::max(0.0, bottom)};
    if (state.fieldBacking.Margin() != margin) state.fieldBacking.Margin(margin);
}

// Written only when the picture or the mode changes.
void KeepBacking(State& state, const xaml::UIElement& content, bool search) {
    FollowBox(state);
    std::optional<bool> picture = PictureUnderStrip(state, content, search);
    auto page = content.try_as<xaml::FrameworkElement>();
    if (!picture || !page) return;
    const bool dark = page.ActualTheme() == xaml::ElementTheme::Dark;
    if (state.backed == std::pair{*picture, dark}) return;
    state.backed = std::pair{*picture, dark};
    const xaml::Visibility shown = *picture ? xaml::Visibility::Visible : xaml::Visibility::Collapsed;
    state.veil.Visibility(shown);
    state.fieldBacking.Visibility(shown);
    const TopbarBacking backing = TopbarBackingFor(dark);
    state.veil.Background(VeilBrush(backing.veil));
    state.fieldBacking.Background(media::SolidColorBrush(ToColor(backing.field)));
}

// Off, or in a window too narrow for the strip, the veil alone stays under the caption buttons: over a theme's picture
// their white glyphs read on whatever it shows (white on a white artwork under Ember). It takes no pointer.
void KeepVeilOnly(State& state, const xaml::UIElement& content, bool search) {
    if (!state.popup) {
        BuildStrip(state, content.XamlRoot());
        state.strip.Visibility(xaml::Visibility::Collapsed);
    }
    state.popup.HorizontalOffset(0);
    state.popup.VerticalOffset(0);
    state.host.Width(content.XamlRoot().Size().Width);
    if (!state.popup.IsOpen()) state.popup.IsOpen(true);
    KeepBacking(state, content, search);
}

void WatchClose(State& state, const xaml::Window& window) {
    HWND hwnd = WindowHandle(window);
    if (hwnd == state.window) return;
    state.window = hwnd;
    state.windowId = window.AppWindow().Id();
    window.Closed([](IInspectable const&, xaml::WindowEventArgs const&) noexcept {
        State& live = TheState();
        live.closed = true;
        if (live.timer) live.timer.Stop();
    });
}

// One look: the strip when it is on and fits, the veil alone when it is off or does not fit, nothing when the playback
// bar is at the top. Cheap once in place: the whole tree is walked only with `search` (the theme's tick), never on the
// quarter-second timer.
void Step(State& state, bool search) {
    if (state.closed) return;
    if (!g_enabled && state.presenter) Release(state);
    xaml::Window window = AppWindowOf(WindowRole::Main);
    xaml::UIElement content = window ? window.Content() : nullptr;
    if (!content || !content.XamlRoot()) return;
    WatchClose(state, window);
    if (!WindowOnScreen(state.window)) return;
    if (state.presenter && !StillHeld(state)) Release(state);
    std::optional<bool> atTop = TransportOnTop(state, content, search);
    if (!atTop) return;
    if (*atTop) {
        if (state.presenter) Release(state);
        CloseVeil(state);
        Note(state, "Top bar: hidden, the playback bar is at the top");
        return;
    }
    if (!g_enabled || !TopbarFits(content.XamlRoot().Size().Width)) {
        if (state.presenter) Release(state);
        KeepVeilOnly(state, content, search);
        Note(state, g_enabled ? "Top bar: hidden, the window is narrower than " +
                                    std::to_string(static_cast<int>(kTopbarMinWidth)) + " px"
                              : "Top bar: off, Apple Music's search field stays in its sidebar");
        return;
    }
    if (search) state.refused = false;
    if (!state.presenter) {
        controls::NavigationView navigation{nullptr};
        if (!state.refused) navigation = Navigation(state, content, search);
        if (!navigation) return;
        if (!Take(state, content, navigation)) {
            state.refused = true;  // tried again at the theme's next tick, not four times a second
            Note(state, "Top bar: Apple Music's search field was not found in its sidebar");
            return;
        }
    }
    LayOut(state, content, search);
    KeepBacking(state, content, search);
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
                // A handler runs inside Apple Music's XAML frame: nothing may escape it.
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
        StartTimer(state);  // the veil follows the window's width with the strip off too
        Step(state, true);
    } catch (...) {
        // Nothing may escape into Apple Music's XAML frame.
    }
}

void SetTopbarSearch(bool enabled) noexcept {
    g_enabled = enabled;
}

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
