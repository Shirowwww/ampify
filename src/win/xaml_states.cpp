// XamlTreeBackend: the pointer, press and focus states of an element (hover values, state handlers) and the keys
// written into a control's own resources for its visual states. The rest of the class is in xaml_backend.cpp.

#if defined(AMPIFY_WITH_XAML)

#include "xaml_backend.hpp"

#include "artwork_passthrough.hpp"

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

#include <winrt/Microsoft.UI.Content.h>
#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Microsoft.UI.Xaml.Media.Animation.h>
#include <winrt/Microsoft.UI.Xaml.Navigation.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.h>
#include <winrt/Windows.UI.Text.h>
#include <winrt/Windows.Foundation.Numerics.h>

#include "../core/theme/colour_keys.hpp"
#include "../core/theme/properties.hpp"
#include "../core/text_util.hpp"
#include "../core/ui_render/render_tree.hpp"
#include "glass_brush.hpp"
#include "xaml_host.hpp"
#include "xaml_backend_detail.hpp"

namespace ampify::win {

using namespace xaml_detail;

// A control that refuses its events or a value counts as having no states: the engine goes on with the next element
// (Engine::Restore gives every element back in one loop).
bool XamlTreeBackend::SetHover(theme::NodeId id, const std::vector<theme::HoverValue>& values) {
    try {
        return FollowHover(id, values);
    } catch (...) {
        return false;
    }
}

bool XamlTreeBackend::FollowHover(theme::NodeId id, const std::vector<theme::HoverValue>& values) {
    if (*m_closed) return false;
    xaml::DependencyObject object = Resolve(id);
    xaml::UIElement element{nullptr};
    if (object) element = object.try_as<xaml::UIElement>();

    // The brushes the element's own states give or gave: the control's state keys follow them (UpdateStates). A value
    // shown in the state of another element is not one of this control's states.
    std::vector<std::string> brushes;
    for (const char* property : {"Background", "Foreground", "BorderBrush"}) {
        bool given = std::any_of(values.begin(), values.end(), [&](const auto& value) {
            return value.property == property && (value.watch == 0 || value.watch == id);
        });
        bool gave = m_hover.ValueOf(id, property) || m_hover.ValueOf(id, property, theme::Trigger::Active);
        if (given || gave) brushes.emplace_back(property);
    }
    auto follow = [&] {
        for (const std::string& property : brushes) {
            auto written = m_written.find(std::make_pair(id, property));
            UpdateStates(id, object, property, written == m_written.end() ? nullptr : written->second.object);
        }
    };
    // The elements watched before and now: each is followed while some value still watches it.
    std::vector<theme::NodeId> watched = m_hover.WatchedBy(id);
    for (const theme::HoverValue& value : values) {
        if (value.watch && value.watch != id && std::find(watched.begin(), watched.end(), value.watch) == watched.end()) {
            watched.push_back(value.watch);
        }
    }

    if (values.empty()) {
        m_hover.Set(id, values, *this);  // under the pointer, the values underneath come back first
        Refollow(id);
        for (theme::NodeId other : watched) Refollow(other);
        if (object) follow();
        return static_cast<bool>(element);
    }
    if (!element) return false;

    theme::HoverTracker::Change change = m_hover.Set(id, values, *this);
    follow();
    if (change == theme::HoverTracker::Change::Added) {
        RemoveHoverHandlers(id);  // none should be left; a stale pair would apply a state twice
    }
    if (change == theme::HoverTracker::Change::Added || change == theme::HoverTracker::Change::Updated) {
        Refollow(id);
        for (theme::NodeId other : watched) Refollow(other);
    }
    return true;
}

void XamlTreeBackend::Refollow(theme::NodeId id) {
    xaml::DependencyObject object = m_hover.UsesAny(id) ? Resolve(id) : nullptr;
    xaml::UIElement element = object ? object.try_as<xaml::UIElement>() : nullptr;
    if (element) {
        FollowStates(id, element);
    } else {
        RemoveHoverHandlers(id);
    }
}

namespace {

// What ends a press: the button let go, the pointer taken away, or the pointer gone out of the element (a press dragged
// off an element that does not capture the pointer would otherwise stay on).
std::array<xaml::RoutedEvent, 4> PressEnds() {
    return {xaml::UIElement::PointerReleasedEvent(), xaml::UIElement::PointerCaptureLostEvent(),
            xaml::UIElement::PointerCanceledEvent(), xaml::UIElement::PointerExitedEvent()};
}

// Whether the keyboard focus is on the element or one of its parts.
bool FocusWithin(const xaml::UIElement& element) {
    xaml::XamlRoot root = element.XamlRoot();
    if (!root) return false;
    auto focused = xaml::Input::FocusManager::GetFocusedElement(root).try_as<xaml::DependencyObject>();
    for (xaml::DependencyObject current = focused; current; current = media::VisualTreeHelper::GetParent(current)) {
        if (current == element) return true;
    }
    return false;
}

// A navigation entry holds its sub-entries (Library holds Artists, Albums...), so the pointer entering a sub-entry also
// entered it: the entry is under the pointer only when no sub-entry lies between them.
bool PointerOnOwnRow(const xaml::UIElement& element, const xaml::Input::PointerRoutedEventArgs& args) {
    for (auto current = args.OriginalSource().try_as<xaml::DependencyObject>(); current;
         current = media::VisualTreeHelper::GetParent(current)) {
        if (current == element) return true;
        if (current.try_as<controls::NavigationViewItem>()) return false;
    }
    return true;
}

// What takes the pointer away without a PointerExited on a watched element: the contact lost, the capture given up.
// PointerExited too, handled or not: once a slider has been pressed, its exit comes marked handled.
std::array<xaml::RoutedEvent, 3> PointerLosses() {
    return {xaml::UIElement::PointerCanceledEvent(), xaml::UIElement::PointerCaptureLostEvent(),
            xaml::UIElement::PointerExitedEvent()};
}

// Whether the pointer is within the element's bounds, in its own coordinates.
// The cursor in the element's box, in its own window (its XamlRoot's): false when the cursor is over another program,
// nothing when it cannot be told, which keeps the :hover (a list row went dark under the pointer while its window was
// not known here).
std::optional<bool> CursorWithin(const xaml::UIElement& element) {
    auto framework = element.try_as<xaml::FrameworkElement>();
    xaml::XamlRoot root = framework ? framework.XamlRoot() : nullptr;
    if (!root || !root.Content()) return std::nullopt;
    const std::uint64_t windowId = root.ContentIslandEnvironment().AppWindowId().Value;
    auto window = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(windowId));
    std::optional<CursorSpot> spot = CursorOver(window);
    if (!window || !spot) return std::nullopt;
    if (!spot->ourProcess) return false;
    if (!spot->client) return std::nullopt;
    const double scale = root.RasterizationScale() > 0 ? root.RasterizationScale() : 1.0;
    const winrt::Windows::Foundation::Rect own{0, 0, static_cast<float>(framework.ActualWidth()),
                                               static_cast<float>(framework.ActualHeight())};
    const winrt::Windows::Foundation::Rect box = framework.TransformToVisual(root.Content()).TransformBounds(own);
    const double x = spot->client->x / scale;
    const double y = spot->client->y / scale;
    return x >= box.X && y >= box.Y && x < box.X + box.Width && y < box.Y + box.Height;
}

// Corners at least half the shorter side: a disc, whose box holds more than its shape.
bool RoundShape(const xaml::UIElement& element) {
    auto framework = element.try_as<xaml::FrameworkElement>();
    if (!framework) return false;
    double radius = 0;
    if (auto control = element.try_as<controls::Control>()) radius = control.CornerRadius().TopLeft;
    else if (auto grid = element.try_as<controls::Grid>()) radius = grid.CornerRadius().TopLeft;
    else if (auto border = element.try_as<controls::Border>()) radius = border.CornerRadius().TopLeft;
    const double side = std::min(framework.ActualWidth(), framework.ActualHeight());
    return side > 0 && radius * 2 >= side - 1;
}

bool PointerInside(const xaml::UIElement& element, const xaml::Input::PointerRoutedEventArgs& args) {
    auto framework = element.try_as<xaml::FrameworkElement>();
    if (!framework) return false;
    const winrt::Windows::Foundation::Point at = args.GetCurrentPoint(element).Position();
    return at.X >= 0 && at.Y >= 0 && at.X < framework.ActualWidth() && at.Y < framework.ActualHeight();
}

constexpr int kCurrentReach = 8;  // parents walked up to the states: a lyrics line's words are 3 to 5 below them

bool NamesCurrent(const xaml::VisualState& state) {
    return state && state.Name() == L"Current";
}

// The visual states that say whether Apple shows the element as the current one: a group with a state named
// "Current", on the element's first child (the root of a lyrics line's content), the element or one of its parents.
xaml::VisualStateGroup CurrentStates(const xaml::UIElement& element) {
    auto groupOf = [](const xaml::DependencyObject& node) -> xaml::VisualStateGroup {
        auto framework = node.try_as<xaml::FrameworkElement>();
        if (!framework) return nullptr;
        for (const xaml::VisualStateGroup& group : xaml::VisualStateManager::GetVisualStateGroups(framework)) {
            for (const xaml::VisualState& state : group.States()) {
                if (NamesCurrent(state)) return group;
            }
        }
        return nullptr;
    };
    if (media::VisualTreeHelper::GetChildrenCount(element) > 0) {
        if (xaml::VisualStateGroup group = groupOf(media::VisualTreeHelper::GetChild(element, 0))) return group;
    }
    xaml::DependencyObject node = element;
    for (int up = 0; node && up < kCurrentReach; up++, node = media::VisualTreeHelper::GetParent(node)) {
        if (xaml::VisualStateGroup group = groupOf(node)) return group;
    }
    return nullptr;
}

}  // namespace

// Follows the events of the states the element's values use, and stops following the others. The handlers run inside
// Apple Music's XAML: OnState lets nothing escape. They hold a weak pointer, so a backend destroyed before them leaves
// them doing nothing.
void XamlTreeBackend::FollowStates(theme::NodeId id, const xaml::UIElement& element) {
    std::weak_ptr<bool> alive = m_alive;
    auto to = [this, alive, id](theme::Trigger trigger, bool on) {
        return [this, alive, id, trigger, on](IInspectable const&, auto const&) noexcept {
            if (!alive.expired()) OnState(id, trigger, on);
        };
    };
    HoverHandlers& handlers = m_hoverHandlers[id];
    handlers.element = winrt::make_weak(element);

    bool hover = m_hover.Uses(id, theme::Trigger::Hover);
    if (hover && !handlers.hover) {
        FollowPointer(id, element, handlers);
    } else if (!hover && handlers.hover) {
        StopFollowingPointer(element, handlers);
        m_hover.Toggle(id, theme::Trigger::Hover, false, *this);
    }
    handlers.hover = hover;

    bool active = m_hover.Uses(id, theme::Trigger::Active);
    if (active && !handlers.active) {
        handlers.pressed = winrt::box_value(xaml::Input::PointerEventHandler(to(theme::Trigger::Active, true)));
        handlers.released = winrt::box_value(xaml::Input::PointerEventHandler(to(theme::Trigger::Active, false)));
        element.AddHandler(xaml::UIElement::PointerPressedEvent(), handlers.pressed, true);
        for (const xaml::RoutedEvent& end : PressEnds()) element.AddHandler(end, handlers.released, true);
    } else if (!active && handlers.active) {
        element.RemoveHandler(xaml::UIElement::PointerPressedEvent(), handlers.pressed);
        for (const xaml::RoutedEvent& end : PressEnds()) element.RemoveHandler(end, handlers.released);
        handlers.pressed = handlers.released = nullptr;
        m_hover.Toggle(id, theme::Trigger::Active, false, *this);
    }
    handlers.active = active;

    bool focus = m_hover.Uses(id, theme::Trigger::Focus);
    if (focus && !handlers.focus) {
        handlers.gotFocus = element.GotFocus(to(theme::Trigger::Focus, true));
        // Checked once the focus has moved: from the search box's text to its own buttons, it stays within the box.
        handlers.lostFocus = element.LostFocus([this, alive, id](IInspectable const& sender, xaml::RoutedEventArgs const&) noexcept {
            try {
                auto source = sender.try_as<xaml::UIElement>();
                if (alive.expired() || !source) return;
                source.DispatcherQueue().TryEnqueue([this, alive, id]() noexcept {
                    if (!alive.expired()) OnFocusLost(id);
                });
            } catch (...) {
                // A handler runs inside Apple Music's XAML frame: nothing may escape it.
            }
        });
        if (FocusWithin(element)) m_hover.Toggle(id, theme::Trigger::Focus, true, *this);
    } else if (!focus && handlers.focus) {
        element.GotFocus(handlers.gotFocus);
        element.LostFocus(handlers.lostFocus);
        m_hover.Toggle(id, theme::Trigger::Focus, false, *this);
    }
    handlers.focus = focus;

    // An element with no such states around it never shows its :current values.
    bool current = m_hover.Uses(id, theme::Trigger::Current);
    if (current && !handlers.current) {
        xaml::VisualStateGroup group = CurrentStates(element);
        current = static_cast<bool>(group);
        if (group) {
            handlers.currentGroup = winrt::make_weak(group);
            handlers.currentChanged = group.CurrentStateChanged(
                [this, alive, id](IInspectable const&, xaml::VisualStateChangedEventArgs const& args) noexcept {
                    try {
                        if (!alive.expired()) OnState(id, theme::Trigger::Current, NamesCurrent(args.NewState()));
                    } catch (...) {
                        // A handler runs inside Apple Music's XAML frame: nothing may escape it.
                    }
                });
            m_hover.Toggle(id, theme::Trigger::Current, NamesCurrent(group.CurrentState()), *this);
        }
    } else if (!current && handlers.current) {
        if (xaml::VisualStateGroup group = handlers.currentGroup.get()) group.CurrentStateChanged(handlers.currentChanged);
        handlers.currentGroup = nullptr;
        m_hover.Toggle(id, theme::Trigger::Current, false, *this);
    }
    handlers.current = current;
}

// The pointer over the element: PointerEntered and PointerExited. An element watched by others (a song's row) also
// hears PointerCanceled, PointerCaptureLost and handled exits, and stays on while the pointer is still inside it:
// those events, and the PointerExited of a part it holds, bubble up to it.
void XamlTreeBackend::FollowPointer(theme::NodeId id, const xaml::UIElement& element, HoverHandlers& handlers) {
    std::weak_ptr<bool> alive = m_alive;
    winrt::weak_ref<xaml::UIElement> weak = winrt::make_weak(element);
    if (element.try_as<controls::NavigationViewItem>()) {
        auto follow = [this, alive, id, weak](IInspectable const&, xaml::Input::PointerRoutedEventArgs const& args) noexcept {
            try {
                xaml::UIElement self = weak.get();
                if (!alive.expired() && self) OnState(id, theme::Trigger::Hover, PointerOnOwnRow(self, args));
            } catch (...) {
                // A handler runs inside Apple Music's XAML frame: nothing may escape it.
            }
        };
        handlers.entered = element.PointerEntered(follow);
        handlers.moved = winrt::box_value(xaml::Input::PointerEventHandler(follow));
        element.AddHandler(xaml::UIElement::PointerMovedEvent(), handlers.moved, true);
    } else {
        handlers.entered = element.PointerEntered([this, alive, id](IInspectable const&, auto const&) noexcept {
            if (!alive.expired()) OnState(id, theme::Trigger::Hover, true);
        });
    }
    // The play bar's artwork: its hover's end went unseen under a filter, and these lines say which event came.
    auto named = element.try_as<xaml::FrameworkElement>();
    const bool traced = named && named.Name() == L"ThumbnailHoverGrid";
    if (traced) NoteArtworkFilter("hover followed on " + std::to_string(id % 100000));
    auto leave = [this, alive, id, weak, traced](bool exited) {
        return [this, alive, id, weak, exited, traced](IInspectable const&,
                                                       xaml::Input::PointerRoutedEventArgs const& args) noexcept {
            try {
                if (alive.expired() || *m_closed) return;
                bool watched = m_hover.WatchedByOthers(id, theme::Trigger::Hover);
                xaml::UIElement self = weak.get();
                if (traced) {
                    NoteArtworkFilter("hover end? " + std::string(exited ? "exited" : "lost or handled") +
                                      (watched ? ", watched" : "") +
                                      (self && PointerInside(self, args) ? ", pointer inside" : ", pointer out"));
                }
                // A round element (the play bar's artwork under Spinning artwork) is left through its box's corners,
                // which the box test takes for inside: its own exit ends the :hover, even marked handled, and a
                // doubtful one has the cursor looked at again. Every other element keeps the rules below.
                if (self && RoundShape(self)) {
                    const bool own = winrt::get_abi(args.OriginalSource()) ==
                                     winrt::get_abi(self.as<winrt::Windows::Foundation::IInspectable>());
                    if (!own && PointerInside(self, args)) {
                        WatchHoverEnd(id);
                        return;
                    }
                    OnState(id, theme::Trigger::Hover, false);
                    return;
                }
                if (!exited && !watched) return;  // the element's own :hover follows PointerExited only, as it always did
                if (watched && self && PointerInside(self, args)) return;
                OnState(id, theme::Trigger::Hover, false);
            } catch (...) {
                // A handler runs inside Apple Music's XAML frame: nothing may escape it.
            }
        };
    };
    handlers.exited = element.PointerExited(leave(true));
    handlers.lost = winrt::box_value(xaml::Input::PointerEventHandler(leave(false)));
    for (const xaml::RoutedEvent& end : PointerLosses()) element.AddHandler(end, handlers.lost, true);
}

void XamlTreeBackend::WatchHoverEnd(theme::NodeId id) {
    auto found = m_hoverHandlers.find(id);
    if (found == m_hoverHandlers.end()) return;
    HoverHandlers& handlers = found->second;
    xaml::UIElement element = handlers.element.get();
    if (!element) return;
    handlers.exitChecks = 0;
    if (!handlers.exitCheck) {
        constexpr auto kLookAgain = std::chrono::milliseconds(100);
        handlers.exitCheck = element.DispatcherQueue().CreateTimer();
        handlers.exitCheck.Interval(kLookAgain);
        handlers.exitCheck.IsRepeating(true);
        std::weak_ptr<bool> alive = m_alive;
        handlers.exitCheck.Tick([this, alive, id](auto const& timer, auto const&) noexcept {
            try {
                if (alive.expired() || *m_closed) return timer.Stop();
                constexpr int kMostChecks = 50;  // 5 s: a pointer still there by then is in the shape for real
                auto it = m_hoverHandlers.find(id);
                xaml::UIElement self = it == m_hoverHandlers.end() ? nullptr : it->second.element.get();
                if (!self || ++it->second.exitChecks > kMostChecks) return timer.Stop();
                if (CursorWithin(self).value_or(true)) return;
                timer.Stop();
                OnState(id, theme::Trigger::Hover, false);
            } catch (...) {
                // A handler runs inside Apple Music's XAML frame: nothing may escape it.
            }
        });
    }
    handlers.exitCheck.Start();
}

void XamlTreeBackend::StopFollowingPointer(const xaml::UIElement& element, HoverHandlers& handlers) {
    if (handlers.exitCheck) handlers.exitCheck.Stop();
    element.PointerEntered(handlers.entered);
    element.PointerExited(handlers.exited);
    if (handlers.moved) element.RemoveHandler(xaml::UIElement::PointerMovedEvent(), handlers.moved);
    if (handlers.lost) {
        for (const xaml::RoutedEvent& end : PointerLosses()) element.RemoveHandler(end, handlers.lost);
    }
    handlers.moved = nullptr;
    handlers.lost = nullptr;
}

void XamlTreeBackend::OnState(theme::NodeId id, theme::Trigger trigger, bool on) noexcept {
    try {
        if (*m_closed) return;
        if (trigger == theme::Trigger::Hover) {
            auto handlers = m_hoverHandlers.find(id);
            xaml::FrameworkElement element{nullptr};
            if (handlers != m_hoverHandlers.end()) element = handlers->second.element.get().try_as<xaml::FrameworkElement>();
            if (element && element.Name() == L"ThumbnailHoverGrid") {
                NoteArtworkFilter("hover " + std::string(on ? "on " : "off ") + std::to_string(id % 100000));
            }
        }
        m_hover.Toggle(id, trigger, on, *this);
    } catch (...) {
        // Nothing may escape into Apple Music's XAML frame.
    }
}

void XamlTreeBackend::OnFocusLost(theme::NodeId id) noexcept {
    try {
        if (*m_closed) return;
        auto it = m_hoverHandlers.find(id);
        xaml::UIElement element = it == m_hoverHandlers.end() ? nullptr : it->second.element.get();
        if (element && FocusWithin(element)) return;
        m_hover.Toggle(id, theme::Trigger::Focus, false, *this);
    } catch (...) {
        // Nothing may escape into Apple Music's XAML frame.
    }
}

void XamlTreeBackend::RemoveHoverHandlers(theme::NodeId id) {
    auto it = m_hoverHandlers.find(id);
    if (it == m_hoverHandlers.end()) return;
    HoverHandlers& handlers = it->second;
    if (xaml::UIElement element = handlers.element.get()) {
        if (handlers.hover) StopFollowingPointer(element, handlers);
        if (handlers.active) {
            element.RemoveHandler(xaml::UIElement::PointerPressedEvent(), handlers.pressed);
            for (const xaml::RoutedEvent& end : PressEnds()) element.RemoveHandler(end, handlers.released);
        }
        if (handlers.focus) {
            element.GotFocus(handlers.gotFocus);
            element.LostFocus(handlers.lostFocus);
        }
    }
    if (handlers.current) {
        if (xaml::VisualStateGroup group = handlers.currentGroup.get()) group.CurrentStateChanged(handlers.currentChanged);
    }
    m_hoverHandlers.erase(it);
}

namespace {

// The keys a control template reads for its pointer states, and the control whose resources hold them. WinUI names most
// {Control}{Property}{State}, for the control's own type and the one it derives from (Apple's transport buttons are
// AppBarButtons whose template reads AppBarButton... and Button... keys). A menu entry that opens a submenu stays lit
// while it shows. A slider names its keys after its parts: the track, the part left of the thumb, and the thumb.
struct StateKeys {
    controls::Control holder{nullptr};
    std::vector<std::wstring> keys;
    std::vector<std::wstring> plainKeys;  // read at rest: they take the written value, not the :hover one
};

// The controls whose templates change their look under the pointer and while pressed. A page, a navigation view or
// another container has no such states: holding keys for one only made it read its theme again, half a second for a
// whole page at each change of colour.
bool HasPointerStates(const controls::Control& control) {
    return control.try_as<primitives::ButtonBase>() || control.try_as<controls::Slider>() ||
           control.try_as<primitives::Thumb>() || control.try_as<controls::MenuFlyoutItemBase>() ||
           control.try_as<primitives::SelectorItem>() || control.try_as<controls::NavigationViewItem>() ||
           control.try_as<controls::ComboBox>() || control.try_as<controls::SplitButton>() ||
           control.try_as<primitives::ScrollBar>();
}

// Apple Music's progress bar (ScrubberControl) reads keys of its own, at rest too, which the mini player defines in its
// own dictionary over the theme's: written on the bar, they take the theme's value in every state.
void AddScrubberKeys(StateKeys& result, std::string_view property, bool thumb) {
    const wchar_t* base = thumb                       ? L"ScrubberThumb"
                          : property == "Foreground" ? L"ScrubberDecreaseRect"
                          : property == "Background" ? L"ScrubberTrackRect"
                                                      : nullptr;
    if (!base) return;
    std::wstring rest = std::wstring(base) + (thumb ? L"Normal" : L"Fill");
    // Apple's own spelling: ScrubberDecreaseRectPressedFill, but ScrubberTrackRectPressed.
    std::wstring pressed = std::wstring(base) + (property == "Foreground" && !thumb ? L"PressedFill" : L"Pressed");
    for (const std::wstring& key : {rest, std::wstring(base) + L"PointerOver", pressed, std::wstring(base) + L"Disabled"}) {
        result.keys.push_back(key);
    }
    result.plainKeys.push_back(rest);
}

bool IsScrubber(const controls::Control& control) {
    return control && ShortName(Utf8(winrt::get_class_name(control))) == "ScrubberControl";
}

StateKeys PointerStateKeys(const controls::Control& control, std::string_view property) {
    constexpr int kSliderDepth = 4;  // from its thumb: the thumb, two template grids, the slider
    StateKeys result{control, {}};
    std::vector<std::string> bases;
    if (control.try_as<controls::Slider>()) {
        if (property == "Background") bases = {"SliderTrackFill"};
        if (property == "Foreground") bases = {"SliderTrackValueFill"};
        if (IsScrubber(control)) AddScrubberKeys(result, property, false);
    } else if (control.try_as<primitives::Thumb>()) {
        xaml::DependencyObject current = control;
        for (int depth = 0; current && depth < kSliderDepth && !result.holder.try_as<controls::Slider>(); depth++) {
            current = media::VisualTreeHelper::GetParent(current);
            if (auto slider = current.try_as<controls::Slider>()) result.holder = slider;
        }
        if (result.holder.try_as<controls::Slider>() && property == "Background") bases = {"SliderThumbBackground"};
        if (property == "Background" && IsScrubber(result.holder)) AddScrubberKeys(result, property, true);
    } else if (control.try_as<primitives::ScrollBar>()) {
        // Away from the pointer a scroll bar is a thin line; under it, it widens to a thumb on a track. The line and the
        // track at rest take the written value, the wide thumb and the track under the pointer the :hover one.
        if (property == "Foreground") {
            result.keys = {L"ScrollBarPanningThumbBackground", L"ScrollBarThumbBackground"};
            result.plainKeys = {L"ScrollBarPanningThumbBackground"};
        }
        if (property == "Background") {
            result.keys = {L"ScrollBarTrackFill", L"ScrollBarTrackFillPointerOver"};
            result.plainKeys = {L"ScrollBarTrackFill"};
        }
        return result;
    } else {
        bases.push_back(ShortName(Utf8(winrt::get_class_name(control))) + std::string(property));
        if (control.try_as<primitives::ToggleButton>()) bases.push_back("ToggleButton" + std::string(property));
        else if (control.try_as<primitives::ButtonBase>()) bases.push_back("Button" + std::string(property));
        else if (control.try_as<controls::MenuFlyoutItem>()) bases.push_back("MenuFlyoutItem" + std::string(property));
    }
    std::vector<const char*> states{"PointerOver", "Pressed"};
    if (control.try_as<controls::MenuFlyoutSubItem>()) states.push_back("SubMenuOpened");
    // Apple Music disables the play button while a page loads with nothing queued: without its own key, the disabled
    // state wiped the theme's disc for that moment.
    if (control.try_as<primitives::ButtonBase>()) states.push_back("Disabled");
    for (const std::string& base : bases) {
        for (const char* state : states) {
            std::wstring key = winrt::to_hstring(base + state).c_str();
            if (std::find(result.keys.begin(), result.keys.end(), key) == result.keys.end()) result.keys.push_back(key);
        }
    }
    return result;
}

// A solid brush drawn the same, as a new object; any other brush as it is.
IInspectable CopyBrush(const IInspectable& brush) {
    if (auto solid = brush.try_as<media::SolidColorBrush>()) {
        media::SolidColorBrush copy(solid.Color());
        copy.Opacity(solid.Opacity());
        return copy;
    }
    return brush;
}

// A disabled control shows the theme's colour faded, as Fluent fades its own; another brush keeps Apple Music's look.
IInspectable DisabledBrush(const IInspectable& brush) {
    constexpr double kDisabledOpacity = 0.4;
    auto solid = brush.try_as<media::SolidColorBrush>();
    if (!solid) return nullptr;
    media::SolidColorBrush copy(solid.Color());
    copy.Opacity(solid.Opacity() * kDisabledOpacity);
    return copy;
}

// The engine builds a new brush at each write: two brushes that draw the same count as the same. Comparing gradients
// and acrylics by object made the control read its states again each time the pointer came or left.
bool SameBrush(const IInspectable& a, const IInspectable& b) {
    if (a == b) return true;
    if (auto first = a.try_as<media::SolidColorBrush>()) {
        auto second = b.try_as<media::SolidColorBrush>();
        return second && first.Color() == second.Color() && first.Opacity() == second.Opacity();
    }
    if (auto first = a.try_as<media::LinearGradientBrush>()) {
        auto second = b.try_as<media::LinearGradientBrush>();
        if (!second || first.StartPoint() != second.StartPoint() || first.EndPoint() != second.EndPoint() ||
            first.Opacity() != second.Opacity()) {
            return false;
        }
        auto stops = first.GradientStops();
        auto others = second.GradientStops();
        if (stops.Size() != others.Size()) return false;
        for (uint32_t i = 0; i < stops.Size(); i++) {
            media::GradientStop stop = stops.GetAt(i);
            media::GradientStop other = others.GetAt(i);
            if (stop.Color() != other.Color() || stop.Offset() != other.Offset()) return false;
        }
        return true;
    }
    if (std::optional<std::string> glass = LiquidGlassText(a.try_as<media::Brush>())) {
        std::optional<std::string> other = LiquidGlassText(b.try_as<media::Brush>());
        return other && *glass == *other;
    }
    if (auto first = a.try_as<media::AcrylicBrush>()) {
        auto second = b.try_as<media::AcrylicBrush>();
        return second && first.TintColor() == second.TintColor() && first.TintOpacity() == second.TintOpacity() &&
               first.FallbackColor() == second.FallbackColor() && first.Opacity() == second.Opacity();
    }
    return false;
}

}  // namespace

void XamlTreeBackend::HoldThroughStates(theme::NodeId id, const xaml::DependencyObject& object, std::string_view property,
                                        const IInspectable& value, const IInspectable& pressed,
                                        const IInspectable& plain) noexcept {
    // The control itself or its template's root, which the states write to (Root.Background). A deeper part keeps the
    // control's own feedback: a list row's background must not become the highlight of the row under the pointer.
    constexpr int kTemplateDepth = 2;
    try {
        controls::Control control{nullptr};
        xaml::DependencyObject current = object;
        for (int depth = 0; current && !control && depth < kTemplateDepth; depth++) {
            control = current.try_as<controls::Control>();
            current = media::VisualTreeHelper::GetParent(current);
        }
        if (!control || !HasPointerStates(control)) return;
        StateKeys states = PointerStateKeys(control, property);
        if (states.keys.empty()) return;
        auto entry = std::make_pair(id, std::string(property));
        HeldStates& held = m_heldStates[entry];
        held.element = winrt::make_weak(object);
        held.control = winrt::make_weak(states.holder);
        held.holder = winrt::get_abi(states.holder.as<winrt::Windows::Foundation::IUnknown>());
        xaml::ResourceDictionary resources = states.holder.Resources();
        // The written brush is also the element's property value, which a dictionary refuses (0x800F0902): a copy.
        IInspectable restCopy = states.plainKeys.empty() ? nullptr : CopyBrush(plain);
        IInspectable disabled = plain ? DisabledBrush(plain) : nullptr;
        bool changed = false;
        for (const std::wstring& key : states.keys) {
            bool atRest = std::find(states.plainKeys.begin(), states.plainKeys.end(), key) != states.plainKeys.end();
            // Apple's ScrubberDecreaseRectPressedFill ends in "PressedFill".
            const IInspectable& want = atRest                                                         ? restCopy
                                       : key.ends_with(L"Disabled")                                  ? disabled
                                       : key.ends_with(L"Pressed") || key.ends_with(L"PressedFill") ? pressed
                                                                                                      : value;
            IInspectable name = winrt::box_value(winrt::hstring(key));
            auto kept = std::find_if(held.keys.begin(), held.keys.end(), [&](const auto& item) { return item.first == key; });
            if (!want) {
                // No theme value for this state: Apple Music's own, given back if the theme held the key.
                if (kept == held.keys.end()) continue;
                if (kept->second) resources.Insert(name, kept->second);
                else if (resources.HasKey(name)) resources.Remove(name);
                held.keys.erase(kept);
                changed = true;
                continue;
            }
            IInspectable before = resources.HasKey(name) ? resources.Lookup(name) : nullptr;
            if (kept == held.keys.end()) held.keys.emplace_back(key, KeyOriginal(held.holder, key, entry).value_or(before));
            if (SameBrush(before, want)) continue;
            resources.Insert(name, want);
            changed = true;
        }
        // Apple's resting background is a transparent white: eased towards a dark theme's colour (a transition), the
        // entry went through a light grey first (Cinema, Greenroom). With no resting value of the theme's, it is the
        // hover colour, transparent, so only the opacity eases.
        auto hover = property == "Background" && !plain ? value.try_as<media::SolidColorBrush>() : nullptr;
        for (const std::wstring& key : hover ? states.keys : std::vector<std::wstring>{}) {
            constexpr std::wstring_view kOver = L"PointerOver";
            if (!key.ends_with(kOver)) continue;
            const std::wstring rest = key.substr(0, key.size() - kOver.size());
            IInspectable name = winrt::box_value(winrt::hstring(rest));
            IInspectable before = resources.HasKey(name) ? resources.Lookup(name) : nullptr;
            auto app = xaml::Application::Current().Resources();
            IInspectable resting = before ? before : app.HasKey(name) ? app.Lookup(name) : nullptr;
            auto now = resting.try_as<media::SolidColorBrush>();
            if (!now || now.Color().A != 0) continue;
            winrt::Windows::UI::Color clear = hover.Color();
            clear.A = 0;
            if (now.Color() == clear) continue;
            auto kept = std::find_if(held.keys.begin(), held.keys.end(),
                                     [&](const auto& item) { return item.first == rest; });
            if (kept == held.keys.end()) {
                held.keys.emplace_back(rest, KeyOriginal(held.holder, rest, entry).value_or(before));
            }
            resources.Insert(name, media::SolidColorBrush(clear));
            changed = true;
        }
        // Only on a real change: a refresh made Apple Music reset the play button's background, which was then written
        // again, every 3 s.
        if (changed) ReadThemeAgain(states.holder);
    } catch (const winrt::hresult_error& error) {
        m_refusedStates++;  // costs the states their theme value, never the value itself
        if (m_firstRefusal.empty()) {
            m_firstRefusal = std::string(property) + ": " + HresultText(error.code()) + " " + Utf8(error.message());
        }
    } catch (...) {
        m_refusedStates++;
    }
}

namespace {

// A brush for a state key from a :hover value. Not an image: it would be decoded again at each write.
media::Brush StateBrush(const theme::Value& value) {
    if (value.kind == theme::ValueKind::Color) {
        std::optional<ColorValue> color = ColorFromText(value.text);
        return color ? media::SolidColorBrush(*color) : nullptr;
    }
    if (value.kind == theme::ValueKind::Gradient) return GradientFrom(value.text);
    if (value.kind == theme::ValueKind::Acrylic) return AcrylicFrom(value.text);
    return nullptr;
}

}  // namespace

// What a control shows under the pointer and while pressed: the theme's :hover value when it gives one, else the value
// the theme wrote (`shown`), else Apple Music's own; pressed, its :active value first. Those values are in the keys
// before the pointer comes: a state shown for the first time reads them then, and an entry that opens a submenu stays
// lit after the pointer leaves it.
void XamlTreeBackend::UpdateStates(theme::NodeId id, const xaml::DependencyObject& object, std::string_view property,
                                   IInspectable shown) noexcept {
    IInspectable plain = shown;
    IInspectable pressed;
    try {
        if (std::optional<theme::Value> hover = m_hover.ValueOf(id, property)) {
            if (media::Brush brush = StateBrush(*hover)) shown = brush;
        }
        pressed = shown;
        if (std::optional<theme::Value> active = m_hover.ValueOf(id, property, theme::Trigger::Active)) {
            if (media::Brush brush = StateBrush(*active)) pressed = brush;
        }
    } catch (...) {
        // Nothing may escape into Apple Music's XAML frame.
    }
    if (shown || pressed) {
        HoldThroughStates(id, object, property, shown, pressed, plain);
    } else {
        ReleaseStates(id, property);
    }
}

void XamlTreeBackend::ReleaseStates(theme::NodeId id, std::string_view property) noexcept {
    auto it = m_heldStates.find(std::make_pair(id, std::string(property)));
    if (it == m_heldStates.end()) return;
    if (auto control = it->second.control.get()) {
        for (const auto& [key, original] : it->second.keys) {
            if (KeyOriginal(it->second.holder, key, it->first)) continue;  // another part holds it: the last gives it back
            try {
                IInspectable name = winrt::box_value(winrt::hstring(key));
                if (original) control.Resources().Insert(name, original);
                else if (control.Resources().HasKey(name)) control.Resources().Remove(name);
            } catch (...) {
                // The element may be torn down already: nothing is left to put back.
            }
        }
        try {
            ReadThemeAgain(control);
        } catch (...) {
            // The element may be torn down already: nothing is left to put back.
        }
    }
    m_heldStates.erase(it);
}

// Two parts of one control can hold the same keys of its resources (a slider's two thumbs). The first records what the
// key held before the theme; the others take that record, never the theme's value they find there.
std::optional<IInspectable> XamlTreeBackend::KeyOriginal(void* holder, const std::wstring& key,
                                                         const std::pair<theme::NodeId, std::string>& except) const {
    for (const auto& [entry, held] : m_heldStates) {
        if (entry == except || held.holder != holder) continue;
        for (const auto& [kept, original] : held.keys) {
            if (kept == key) return original;
        }
    }
    return std::nullopt;
}

std::size_t XamlTreeBackend::HeldStateCount() const {
    std::size_t count = 0;
    for (const auto& entry : m_heldStates) count += entry.second.keys.size();
    return count;
}

namespace {

// Visible from the start and kept there (a storyboard holds its end by default). Apple's states may also fade the
// element out with a theme animation, which its Opacity does not show (the play bar's times, ExitStoryboard): a fade
// in of Ampify's, begun later, takes it back.
animation::Storyboard ShownStory(const xaml::UIElement& element) {
    animation::DiscreteObjectKeyFrame frame;
    frame.KeyTime(animation::KeyTimeHelper::FromTimeSpan(winrt::Windows::Foundation::TimeSpan{0}));
    frame.Value(winrt::box_value(xaml::Visibility::Visible));
    animation::ObjectAnimationUsingKeyFrames shown;
    shown.KeyFrames().Append(frame);
    animation::Storyboard::SetTarget(shown, element);
    animation::Storyboard::SetTargetProperty(shown, L"(UIElement.Visibility)");
    animation::FadeInThemeAnimation fade;
    animation::Storyboard::SetTarget(fade, element);
    animation::Storyboard story;
    story.Children().Append(shown);
    story.Children().Append(fade);
    return story;
}

// Two re-starts closer than this are a fight with Apple's states: the next pass writes the value again instead.
constexpr std::chrono::milliseconds kHoldRetry{250};

// How far up the states that animate an element are looked for: the play bar's times are 3 levels under theirs.
constexpr int kStateOwnerDepth = 6;

// The visual state groups, on the element's ancestors, whose storyboards name it: a fade of theirs leaves its
// Visibility as it is, so only their changes say when the hold must begin again.
std::vector<xaml::VisualStateGroup> StateGroupsAnimating(const xaml::UIElement& element) {
    std::vector<xaml::VisualStateGroup> found;
    auto named = element.try_as<xaml::FrameworkElement>();
    if (!named || named.Name().empty()) return found;
    const winrt::hstring name = named.Name();
    xaml::DependencyObject current = media::VisualTreeHelper::GetParent(element);
    for (int depth = 0; current && depth < kStateOwnerDepth; depth++) {
        if (auto owner = current.try_as<xaml::FrameworkElement>()) {
            for (const xaml::VisualStateGroup& group : xaml::VisualStateManager::GetVisualStateGroups(owner)) {
                bool names = false;
                for (const xaml::VisualState& state : group.States()) {
                    animation::Storyboard board = state.Storyboard();
                    if (!board) continue;
                    for (const animation::Timeline& child : board.Children()) {
                        names = names || animation::Storyboard::GetTargetName(child) == name;
                    }
                }
                if (names) found.push_back(group);
            }
        }
        current = media::VisualTreeHelper::GetParent(current);
    }
    return found;
}

}  // namespace

// `visibility: visible` as an animation of Ampify's: it ranks above a local value, a binding and a visual state begun
// before it, and leaves them all in place under it. A later state of Apple's collapses the element again, and the hold
// begins again. Without `restart`, a hold already running on a shown element is left as it is.
void XamlTreeBackend::HoldVisible(theme::NodeId id, const xaml::UIElement& element, bool restart) noexcept {
    try {
        auto known = m_visibleHolds.find(id);
        if (known != m_visibleHolds.end() && known->second.element.get() != element) {
            ReleaseVisible(id);
            known = m_visibleHolds.end();
        }
        const bool shown = element.Visibility() == xaml::Visibility::Visible;
        if (!restart && shown && known != m_visibleHolds.end()) return;
        std::vector<xaml::VisualStateGroup> groups =
            known == m_visibleHolds.end() ? StateGroupsAnimating(element) : std::vector<xaml::VisualStateGroup>{};
        // Shown, and no state of Apple's to fade it: nothing to hold.
        if (!restart && shown && known == m_visibleHolds.end() && groups.empty()) return;
        if (known == m_visibleHolds.end()) {
            VisibleHold hold{winrt::make_weak(element), ShownStory(element)};
            std::weak_ptr<bool> alive = m_alive;
            hold.token = element.RegisterPropertyChangedCallback(
                xaml::UIElement::VisibilityProperty(),
                [this, alive, id](xaml::DependencyObject const&, xaml::DependencyProperty const&) noexcept {
                    if (!alive.expired()) OnHeldVisibility(id);
                });
            for (const xaml::VisualStateGroup& group : groups) {
                hold.stateChanges.push_back(group.CurrentStateChanged(
                    winrt::auto_revoke,
                    [this, alive, id](IInspectable const&, xaml::VisualStateChangedEventArgs const&) noexcept {
                        if (!alive.expired()) OnHeldStateChanged(id);
                    }));
            }
            known = m_visibleHolds.emplace(id, std::move(hold)).first;
        }
        known->second.begun = std::chrono::steady_clock::now();
        known->second.story.Stop();
        known->second.story.Begin();
    } catch (...) {
        // Nothing may escape into Apple Music's XAML frame.
    }
}

// A state of Apple's that fades the element began: the hold begins again after it, its visibility unchanged.
void XamlTreeBackend::OnHeldStateChanged(theme::NodeId id) noexcept {
    try {
        auto known = m_visibleHolds.find(id);
        if (*m_closed || known == m_visibleHolds.end()) return;
        xaml::UIElement element = known->second.element.get();
        if (!element) return;
        std::weak_ptr<bool> alive = m_alive;
        element.DispatcherQueue().TryEnqueue([this, alive, id]() noexcept {
            if (alive.expired() || *m_closed) return;
            auto again = m_visibleHolds.find(id);
            xaml::UIElement held = again == m_visibleHolds.end() ? nullptr : again->second.element.get();
            if (held) HoldVisible(id, held);
        });
    } catch (...) {
        // Nothing may escape into Apple Music's XAML frame.
    }
}

// Not inside the change itself: Apple's storyboard is still starting then.
void XamlTreeBackend::OnHeldVisibility(theme::NodeId id) noexcept {
    try {
        auto known = m_visibleHolds.find(id);
        if (*m_closed || known == m_visibleHolds.end()) return;
        xaml::UIElement element = known->second.element.get();
        if (!element || element.Visibility() == xaml::Visibility::Visible) return;
        if (std::chrono::steady_clock::now() - known->second.begun < kHoldRetry) return;
        std::weak_ptr<bool> alive = m_alive;
        element.DispatcherQueue().TryEnqueue([this, alive, id]() noexcept {
            if (alive.expired() || *m_closed) return;
            auto again = m_visibleHolds.find(id);
            xaml::UIElement held = again == m_visibleHolds.end() ? nullptr : again->second.element.get();
            if (held && held.Visibility() != xaml::Visibility::Visible) HoldVisible(id, held);
        });
    } catch (...) {
        // Nothing may escape into Apple Music's XAML frame.
    }
}

bool XamlTreeBackend::ReleaseVisible(theme::NodeId id, const xaml::UIElement& element) noexcept {
    auto known = m_visibleHolds.find(id);
    if (known == m_visibleHolds.end()) return false;
    bool same = true;
    try {
        known->second.story.Stop();  // Apple's own value shows again: its state, or the local value under it
        xaml::UIElement held = known->second.element.get();
        same = !element || held == element;
        if (held) held.UnregisterPropertyChangedCallback(xaml::UIElement::VisibilityProperty(), known->second.token);
    } catch (...) {
        // The element may be torn down already: nothing is left to put back.
    }
    m_visibleHolds.erase(known);
    return same;
}

// A row Apple rebuilt or recycled away: its hold would keep Ampify's storyboard, and perhaps the row, for good.
void XamlTreeBackend::DropDeadHolds() noexcept {
    std::vector<theme::NodeId> gone;
    for (const auto& [id, hold] : m_visibleHolds) {
        try {
            xaml::UIElement held = hold.element.get();
            auto element = held ? held.try_as<xaml::FrameworkElement>() : nullptr;
            bool detached = element && !element.IsLoaded() && !media::VisualTreeHelper::GetParent(element);
            if (!held || detached) gone.push_back(id);
        } catch (...) {
            gone.push_back(id);
        }
    }
    for (theme::NodeId id : gone) ReleaseVisible(id);
}

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
