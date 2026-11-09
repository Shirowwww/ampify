#pragma once
// sidebar_hover.cpp and sidebar_pane_dress.cpp only: what Ampify keeps of Apple's sidebar while it holds it, and
// what it adds to the pane (the pin, the fill and edge over the pages, the light-dismiss layer, the folded groups).

#if defined(AMPIFY_WITH_XAML)

#include "win_common.hpp"
#ifdef GetCurrentTime
#undef GetCurrentTime  // windows.h macro that breaks Storyboard::GetCurrentTime in the projection
#endif

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
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ampify::win::sidebar_detail {

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
    Clock::time_point refocusUntil{};  // a key opened the pane: until then, the keyboard goes back to the hamburger
    controls::Grid fillGrid{nullptr};  // Apple's pane grid, and ours in it: the fill of the pane open over the pages
    controls::Border fill{nullptr};
    winrt::Windows::Foundation::IInspectable acrylicKeysOn{nullptr};  // NavigationView whose pane acrylic Ampify cleared
    winrt::weak_ref<controls::Grid> column;      // Apple's Grid#PaneContentGrid, found once
    winrt::weak_ref<xaml::UIElement> caster;     // Apple's Grid#ShadowCaster, found once
    xaml::UIElement casterHeld{nullptr};         // the same, while Ampify keeps it collapsed, and its local value
    IInspectable casterVisibility{nullptr};
    controls::Grid edgeHost{nullptr};            // the grid that paints the pane, and our edge in it, held strong
    controls::Border edge{nullptr};
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
    xaml::VisualStateGroup modeGroup{nullptr};  // the SplitView's DisplayModeStates, while its transitions are out
    std::vector<xaml::VisualTransition> transitions;  // what HoldPaneStill took out of it
};

State& TheState();

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

void ShowPin(State& state, bool animate);
void RemovePin(State& state);
void RestoreDismissLayer(State& state);
void SetDismissLayer(State& state, const controls::NavigationView& navigation, bool hover);
void HoldPaneStill(State& state, const controls::NavigationView& navigation, bool still);
void RemoveFill(State& state);
void RemoveEdge(State& state);
void EdgeOverPages(State& state, const controls::NavigationView& navigation, bool over);
void FillPane(State& state, const controls::NavigationView& navigation);
void HideAppleShadow(State& state, const controls::NavigationView& navigation);
void RestoreAppleShadow(State& state);
void UnfoldGroups(State& state);
void SyncGroups(State& state, const controls::NavigationView& navigation);
void AddPin(State& state, const controls::NavigationView& navigation, const xaml::RoutedEventHandler& onClick);

}  // namespace ampify::win::sidebar_detail

#endif  // AMPIFY_WITH_XAML
