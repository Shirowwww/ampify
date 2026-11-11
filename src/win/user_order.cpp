#include "user_order.hpp"

#if defined(AMPIFY_WITH_XAML)

#include <windows.h>  // before the projection, as in the PCH: one macro state for the whole file (the CI builds without it)
#ifdef GetCurrentTime
#undef GetCurrentTime  // windows.h macro that breaks Storyboard::GetCurrentTime in the projection
#endif

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.Media.Animation.h>

#include <mutex>
#include <string_view>
#include <utility>

#include "../core/user_order.hpp"
#include "win_common.hpp"
#include "xaml_chrome.hpp"

namespace ampify::win {

namespace {

namespace xaml = winrt::Microsoft::UI::Xaml;
namespace controls = xaml::Controls;
namespace media = xaml::Media;
namespace automation = xaml::Automation;
using Inspectables = winrt::Windows::Foundation::Collections::IVector<winrt::Windows::Foundation::IInspectable>;

constexpr wchar_t kTogglesName[] = L"NowPlayingToggleButtons";  // the playback bar's right panel (names.md)
constexpr std::size_t kSearchLimit = 4000;
constexpr std::size_t kLabelSearchLimit = 30;
constexpr int kSearchEvery = 4;  // keep passes between two searches while the panel is missing

std::mutex g_mutex;
std::vector<std::string> g_playbar;        // apple.order.playbar
std::vector<std::string> g_applePlaybar;   // the buttons' keys before the first move
std::vector<std::string> g_appleSidebar;   // the sidebar's keys before the theme engine's first move

struct Kept {
    winrt::weak_ref<controls::Panel> toggles;
    int ticks = 0;
};

Kept& State() {
    static Kept* state = new Kept();  // XAML references: released with the process
    return *state;
}

std::string Utf8(winrt::hstring const& text) { return winrt::to_string(text); }

template <typename T>
T FindFirst(const xaml::DependencyObject& root, std::size_t limit, auto&& accept) {
    std::vector<xaml::DependencyObject> queue{root};
    for (std::size_t i = 0; i < queue.size() && queue.size() < limit; i++) {
        if (auto found = queue[i].try_as<T>(); found && accept(found)) return found;
        int count = media::VisualTreeHelper::GetChildrenCount(queue[i]);
        for (int child = 0; child < count; child++) queue.push_back(media::VisualTreeHelper::GetChild(queue[i], child));
    }
    return nullptr;
}

xaml::UIElement MainRoot() {
    xaml::Window window = AppWindowOf(WindowRole::Main);
    return window ? window.Content().try_as<xaml::UIElement>() : nullptr;
}

controls::Panel Toggles(bool search) {
    Kept& state = State();
    if (auto panel = state.toggles.get(); panel && panel.XamlRoot()) return panel;
    if (!search) return nullptr;
    xaml::UIElement root = MainRoot();
    if (!root) return nullptr;
    auto panel = FindFirst<controls::Panel>(root, kSearchLimit, [](auto const& p) { return p.Name() == kTogglesName; });
    state.toggles = panel ? winrt::make_weak(panel) : winrt::weak_ref<controls::Panel>{};
    return panel;
}

// An Ampify button is named AMPIFY_<element>_control-<n>, n counting every control made since start.
std::string ButtonKey(const xaml::UIElement& child) {
    auto element = child.try_as<xaml::FrameworkElement>();
    std::string name = element ? Utf8(element.Name()) : std::string();
    if (name.starts_with("AMPIFY_")) {
        if (std::size_t control = name.rfind("_control-"); control != std::string::npos) name.resize(control);
    }
    if (!name.empty()) return name;
    std::string type = Utf8(winrt::get_class_name(child));
    return type.substr(type.rfind('.') + 1);  // npos + 1 is 0: a type without a namespace stays whole
}

// What a group of buttons says: the names of the buttons in it, joined.
std::string ButtonsLabel(const xaml::UIElement& child) {
    std::string label;
    std::vector<xaml::DependencyObject> queue{child};
    for (std::size_t i = 0; i < queue.size() && queue.size() < kLabelSearchLimit; i++) {
        if (auto button = queue[i].try_as<controls::Primitives::ButtonBase>()) {
            std::string name = Utf8(automation::AutomationProperties::GetName(button));
            if (name.empty()) {
                if (auto tip = controls::ToolTipService::GetToolTip(button).try_as<winrt::Windows::Foundation::IReference<winrt::hstring>>()) {
                    name = Utf8(tip.Value());
                }
            }
            if (!name.empty()) label += (label.empty() ? "" : " + ") + name;
            continue;  // a button's own parts are not other buttons
        }
        int count = media::VisualTreeHelper::GetChildrenCount(queue[i]);
        for (int c = 0; c < count; c++) queue.push_back(media::VisualTreeHelper::GetChild(queue[i], c));
    }
    return label;
}

std::string ItemLabel(const controls::NavigationViewItemBase& item) {
    std::string label = Utf8(automation::AutomationProperties::GetName(item));
    if (label.empty()) {
        if (auto text = item.Content().try_as<winrt::Windows::Foundation::IReference<winrt::hstring>>()) {
            label = Utf8(text.Value());
        }
    }
    return label;
}

}  // namespace

std::vector<OrderEntry> SidebarEntries() noexcept {
    std::vector<OrderEntry> entries;
    try {
        xaml::UIElement root = MainRoot();
        auto navigation = root ? FindFirst<controls::NavigationView>(root, kSearchLimit, [](auto const&) { return true; })
                               : nullptr;
        if (!navigation) return entries;
        // Apple Music fills its sidebar from MenuItemsSource: the entries are then the containers shown for it.
        std::vector<winrt::Windows::Foundation::IInspectable> objects;
        for (const auto& object : navigation.MenuItems()) objects.push_back(object);
        if (objects.empty()) {
            if (auto source = navigation.MenuItemsSource().try_as<Inspectables>()) {
                for (const auto& data : source) objects.push_back(navigation.ContainerFromMenuItem(data));
            }
        }
        for (const auto& object : objects) {
            auto item = object.try_as<controls::NavigationViewItemBase>();
            if (!item || item.try_as<controls::NavigationViewItemSeparator>()) continue;
            std::string key = Utf8(item.Name());
            if (key.empty()) key = Utf8(automation::AutomationProperties::GetAutomationId(item));
            if (key.empty()) continue;  // nothing stable to write it with
            std::string label = ItemLabel(item);
            entries.push_back({key, label.empty() ? key : label});
        }
    } catch (...) {
    }
    return entries;
}

std::vector<OrderEntry> PlaybarEntries() noexcept {
    std::vector<OrderEntry> entries;
    try {
        controls::Panel panel = Toggles(true);
        if (!panel) return entries;
        for (const xaml::UIElement& child : panel.Children()) {
            std::string key = ButtonKey(child);
            std::string label = ButtonsLabel(child);
            entries.push_back({key, label.empty() ? key : label});
        }
    } catch (...) {
    }
    return entries;
}

void SetPlaybarOrder(std::vector<std::string> keys) noexcept {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_playbar = std::move(keys);
}

void KeepPlaybarOrder() noexcept {
    try {
        std::vector<std::string> wanted;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            wanted = g_playbar.empty() ? g_applePlaybar : g_playbar;
        }
        if (wanted.empty()) return;
        Kept& state = State();
        controls::Panel panel = Toggles(state.ticks++ % kSearchEvery == 0);
        if (!panel) return;
        auto children = panel.Children();
        std::vector<xaml::UIElement> items;
        std::vector<std::string> keys;
        for (const xaml::UIElement& child : children) {
            items.push_back(child);
            keys.push_back(ButtonKey(child));
        }
        std::vector<std::size_t> order = UserOrder(keys, wanted);
        bool moves = false;
        for (std::size_t place = 0; place < order.size() && !moves; place++) moves = order[place] != place;
        if (!moves) return;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            if (g_applePlaybar.empty()) g_applePlaybar = keys;
        }
        if (!panel.ChildrenTransitions()) {  // the buttons slide to their new places
            xaml::Media::Animation::TransitionCollection transitions;
            transitions.Append(xaml::Media::Animation::RepositionThemeTransition());
            panel.ChildrenTransitions(transitions);
        }
        for (uint32_t place = 0; place < order.size(); place++) {
            uint32_t now = 0;
            if (!children.IndexOf(items[order[place]], now) || now == place) continue;
            children.Move(now, place);
        }
    } catch (...) {
        // The panel left the tree for a moment: the next pass finds it again.
    }
}

void NoteSidebarOrder(const std::vector<std::string>& keys) noexcept {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_appleSidebar.empty()) g_appleSidebar = keys;
}

std::vector<std::string> AppleSidebarOrder() noexcept {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_appleSidebar;
}

std::vector<std::string> ApplePlaybarOrder() noexcept {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_applePlaybar;
}

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
