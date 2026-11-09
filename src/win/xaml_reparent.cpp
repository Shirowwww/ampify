// `move-to`: an element moved into another, and the stand-in that keeps Apple's visual states working.

#if defined(AMPIFY_WITH_XAML)

#include "xaml_backend.hpp"

#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <optional>
#include <string_view>
#include <utility>
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <set>

#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Automation.Peers.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Microsoft.UI.Xaml.Media.Animation.h>
#include <winrt/Microsoft.UI.Xaml.Navigation.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.UI.h>
#include <winrt/Windows.UI.Text.h>
#include <winrt/Windows.Foundation.Numerics.h>

#include "../core/color_extract.hpp"
#include "../core/theme/colour_keys.hpp"
#include "../core/theme/properties.hpp"
#include "../core/theme/selector.hpp"
#include "../core/text_util.hpp"
#include "../core/ui_render/render_tree.hpp"
#include "sidebar_hover.hpp"
#include "xaml_host.hpp"
#include "xaml_backend_detail.hpp"

namespace ampify::win {

using namespace xaml_detail;

// The parents walked up from a moved element to the visual states that name it: the times sit 2 to 4 below the root of
// the song display's template.
constexpr int kStatesReach = 8;

// Puts the element into `panel` at `index` (or last). False when the panel refuses it.
static bool InsertChild(const controls::Panel& panel, const xaml::UIElement& element, int index) noexcept {
    try {
        controls::UIElementCollection children = panel.Children();
        const std::uint32_t size = children.Size();
        const auto place = index < 0 ? size : std::min(static_cast<std::uint32_t>(index), size);
        children.InsertAt(place, element);
        return true;
    } catch (...) {
        return false;
    }
}

// A state's target given as the name, or as an element of that name (the element itself, or its stand-in).
static bool NamesElement(const IInspectable& target, const winrt::hstring& name) {
    if (!target) return false;
    if (auto framework = target.try_as<xaml::FrameworkElement>()) return framework.Name() == name;
    return winrt::unbox_value_or<winrt::hstring>(target, L"") == name;
}

// A state's animations that name the element point at `to` (with no `to`, nothing changes). True when one names it.
static bool PointStory(const animation::Storyboard& story, const winrt::hstring& name,
                       const xaml::DependencyObject& to) {
    bool named = false;
    bool running = false;
    for (const animation::Timeline& timeline : story.Children()) {
        try {
            if (animation::Storyboard::GetTargetName(timeline) != name) continue;
            if (!to) return true;
            // A state that ran keeps its end on its old target (the song's times faded out): it lets go, and plays
            // again on the new one, so the element back in place looks as Apple left the state.
            if (!named) {
                running = story.GetCurrentState() != animation::ClockState::Stopped;
                story.Stop();
            }
            named = true;
            animation::Storyboard::SetTarget(timeline, to);
        } catch (...) {
        }
    }
    try {
        if (running) story.Begin();
    } catch (...) {
    }
    return named;
}

// One state's setters and animations that name the element point at `to` (with no `to`, nothing changes). True when
// the state names it.
static bool PointState(const xaml::VisualState& state, const winrt::hstring& name, const xaml::DependencyObject& to) {
    bool named = false;
    for (const xaml::SetterBase& base : state.Setters()) {
        try {
            auto setter = base.try_as<xaml::Setter>();
            xaml::TargetPropertyPath path = setter ? setter.Target() : nullptr;
            if (!path || !NamesElement(path.Target(), name)) continue;
            named = true;
            if (to) path.Target(to);
        } catch (...) {
        }
    }
    animation::Storyboard story = state.Storyboard();
    return (story && PointStory(story, name, to)) || named;
}

// The states of `from` and the parents above it that name the element point at `to`. True when one of them names it.
static bool PointStates(const xaml::DependencyObject& from, const winrt::hstring& name,
                        const xaml::DependencyObject& to) {
    bool named = false;
    xaml::DependencyObject node = from;
    for (int up = 0; node && up < kStatesReach; up++, node = media::VisualTreeHelper::GetParent(node)) {
        auto owner = node.try_as<xaml::FrameworkElement>();
        if (!owner) continue;
        for (const xaml::VisualStateGroup& group : xaml::VisualStateManager::GetVisualStateGroups(owner)) {
            for (const xaml::VisualState& state : group.States()) named = PointState(state, name, to) || named;
        }
    }
    return named;
}

// An element of the same class and name, empty, sized 0 and out of the walk (Snapshot), that stands where a moved
// element was. Null when the class cannot be made outside its template.
xaml::FrameworkElement XamlTreeBackend::StandIn(const xaml::FrameworkElement& element) {
    const std::wstring key = std::wstring(winrt::get_class_name(element)) + L"#" + std::wstring(element.Name());
    if (auto known = m_standIns.find(key); known != m_standIns.end()) return known->second;
    xaml::FrameworkElement made{nullptr};
    try {
        made = winrt::get_activation_factory(winrt::get_class_name(element)).ActivateInstance<xaml::FrameworkElement>();
        made.Name(element.Name());
        made.Width(0);
        made.Height(0);
        made.IsHitTestVisible(false);
        made.Visibility(xaml::Visibility::Collapsed);
        m_standInIds.insert(winrt::get_abi(made.as<xaml::DependencyObject>()));  // as the walk sees it
    } catch (...) {
        made = nullptr;
    }
    m_standIns.emplace(key, made);
    return made;
}

// Apple's states find their targets by name in the template, and an element that leaves the tree leaves its name
// there: moved, the times made the song display's pointer states fail ("animation target not specified", 0x800F1000)
// and Apple Music closed at the first hover of the song. The stand-in takes the element's place and name, and the
// states play on it, so they hide nothing the theme placed elsewhere; back in place, they drive the element again.
void XamlTreeBackend::HandStatesOver(const xaml::UIElement& element, const controls::Panel& from, std::uint32_t at,
                                     const controls::Panel& to) noexcept {
    try {
        auto framework = element.try_as<xaml::FrameworkElement>();
        if (!framework || framework.Name().empty()) return;
        PointStates(to, framework.Name(), element);
        if (!PointStates(from, framework.Name(), nullptr)) return;  // no state names it: nothing to stand in for
        xaml::FrameworkElement standIn = StandIn(framework);
        PointStates(from, framework.Name(), standIn ? xaml::DependencyObject(standIn) : element);
        if (!standIn) return;
        // Still in a template Apple Music has since rebuilt: it moves to the new one.
        if (auto old = media::VisualTreeHelper::GetParent(standIn).try_as<controls::Panel>()) {
            if (old == from) return;
            WithdrawStandIn(element, old);
        }
        from.Children().InsertAt(std::min(at, from.Children().Size()), standIn);
    } catch (...) {
    }
}

// Before the element comes back to `panel`, its stand-in leaves it: the name is the element's again.
void XamlTreeBackend::WithdrawStandIn(const xaml::UIElement& element, const controls::Panel& panel) noexcept {
    try {
        auto framework = element.try_as<xaml::FrameworkElement>();
        if (!framework || framework.Name().empty()) return;
        const std::wstring key = std::wstring(winrt::get_class_name(framework)) + L"#" + std::wstring(framework.Name());
        auto known = m_standIns.find(key);
        std::uint32_t at = 0;
        if (known == m_standIns.end() || !known->second || !panel.Children().IndexOf(known->second, at)) return;
        panel.Children().RemoveAt(at);
    } catch (...) {
    }
}

bool XamlTreeBackend::IsStandIn(const xaml::DependencyObject& object) const {
    return !m_standInIds.empty() && m_standInIds.count(winrt::get_abi(object)) > 0;
}

// The element leaves its panel and joins the other one at `index` (or last): the visual parent says which panel it
// leaves, so no snapshot is needed. When the other panel refuses it, the element goes back to its old place.
bool XamlTreeBackend::Reparent(theme::NodeId id, theme::NodeId parent, int index) {
    try {
        auto element = Resolve(id).try_as<xaml::UIElement>();
        auto target = Resolve(parent).try_as<controls::Panel>();
        if (!element || !target) return false;
        auto from = media::VisualTreeHelper::GetParent(element).try_as<controls::Panel>();
        std::uint32_t at = 0;
        if (!from || !from.Children().IndexOf(element, at)) return false;
        from.Children().RemoveAt(at);
        WithdrawStandIn(element, target);
        if (InsertChild(target, element, index)) {
            HandStatesOver(element, from, at, target);
            return true;
        }
        // Without this the scrubber and the times would vanish from Apple Music's bar.
        return InsertChild(from, element, static_cast<int>(at));
    } catch (...) {
        return false;
    }
}

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
