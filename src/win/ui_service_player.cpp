// The play bar buttons that Apple's full-screen player shows too (Ampify.ui.playbar.addButton with `player: true`):
// a copy of each stands in the player's transport row, just before its lyrics button, with that button's style. A
// click on a copy is the play bar button's click, and the copy follows the button's label and state.

#if defined(AMPIFY_WITH_XAML)

#include "ui_service_detail.hpp"
#include "xaml_ending.hpp"

#include <winrt/Microsoft.UI.Windowing.h>

namespace ampify::win::ui_service_detail {

namespace {

constexpr int kPlayerSearchLimit = 4000;                     // elements walked in the player to find its row
constexpr auto kPlayerRetryStep = std::chrono::milliseconds(500);  // while the player's row is not laid out yet
constexpr int kPlayerRetries = 20;

struct PlayerRow {
    controls::StackPanel panel{nullptr};       // the right part of TransportGrid
    primitives::ToggleButton lyrics{nullptr};  // Apple's lyrics button in it
};

// The panel of the transport row that holds the lyrics button (Apple Music 1.1792: the third column of TransportGrid,
// with the hidden reactions button before it). Empty while the player has not laid it out.
PlayerRow FindPlayerRow(const xaml::UIElement& content) {
    std::vector<xaml::DependencyObject> queue{content};
    controls::Grid grid{nullptr};
    for (std::size_t i = 0; i < queue.size() && queue.size() < static_cast<std::size_t>(kPlayerSearchLimit); i++) {
        auto element = queue[i].try_as<controls::Grid>();
        if (element && element.Name() == L"TransportGrid") {
            grid = element;
            break;
        }
        int count = media::VisualTreeHelper::GetChildrenCount(queue[i]);
        for (int child = 0; child < count; child++) queue.push_back(media::VisualTreeHelper::GetChild(queue[i], child));
    }
    if (!grid) return {};
    for (const auto& child : grid.Children()) {
        auto panel = child.try_as<controls::StackPanel>();
        if (!panel) continue;
        for (const auto& item : panel.Children()) {
            if (auto lyrics = item.try_as<primitives::ToggleButton>()) return {panel, lyrics};
        }
    }
    return {};
}

void SyncCopy(PlayerCopy& copy, const UiControl& control) {
    if (copy.label != control.label) {
        copy.label = control.label;
        if (!copy.hasIcon) copy.button.Content(winrt::box_value(Wide(control.label)));
        controls::ToolTipService::SetToolTip(copy.button, winrt::box_value(Wide(control.label)));
        automation::AutomationProperties::SetName(copy.button, Wide(control.label));
    }
    auto checked = copy.button.IsChecked();
    if ((checked && checked.Value()) != control.active) copy.button.IsChecked(control.active);
    automation::AutomationProperties::SetItemStatus(copy.button, control.active ? L"on" : L"");
    if (copy.disabled != control.disabled) {
        copy.disabled = control.disabled;
        copy.button.IsEnabled(!control.disabled);
    }
}

// A toggle like the lyrics button beside it: same style, padding and height, so its size, halo, hover and press are
// those of its neighbours. On, it is checked, as the lyrics button is while the lyrics show.
PlayerCopy MakeCopy(UiService::Shared& shared, const primitives::ToggleButton& lyrics, const UiControl& control) {
    PlayerCopy copy;
    primitives::ToggleButton button;
    if (auto style = lyrics.Style()) button.Style(style);
    button.Padding(lyrics.Padding());
    button.Margin(lyrics.Margin());
    button.VerticalAlignment(lyrics.VerticalAlignment());
    if (lyrics.ActualHeight() > 0) button.Height(lyrics.ActualHeight());
    if (lyrics.ActualWidth() > 0) button.MinWidth(lyrics.ActualWidth());
    // A glyph takes the button's foreground, which the style changes under the pointer and while checked.
    if (xaml::FrameworkElement icon = MakeIconElement(control.icon, 16, L"TextFillColorPrimaryBrush")) {
        button.Content(icon);
        copy.hasIcon = true;
    } else {
        button.Content(winrt::box_value(Wide(control.label)));
    }
    controls::ToolTipService::SetToolTip(button, winrt::box_value(Wide(control.label)));
    Name(button, uirender::ElementName(control.elementId, "player-control-" + std::to_string(control.id)), control.label);
    UiEventSink sink = shared.sink;
    std::string elementId = control.elementId;
    int id = control.id;
    button.Click([sink, elementId, id](IInspectable const& sender, xaml::RoutedEventArgs const&) noexcept {
        try {
            // The click toggled it: the element says whether it is on (setActive), so it goes back until then.
            if (auto toggle = sender.try_as<primitives::ToggleButton>()) {
                auto checked = toggle.IsChecked();
                toggle.IsChecked(!(checked && checked.Value()));
            }
            if (sink) sink(UiEvent{elementId, id, true, {}});
        } catch (...) {
            // A handler runs inside Apple Music's XAML frame: nothing may escape it.
        }
    });
    copy.button = button;
    copy.label = control.label;
    SyncCopy(copy, control);
    return copy;
}

void RemoveCopies(WindowContext& context) {
    for (auto& entry : context.playerCopies) RemoveFromParent(entry.second.button);
    context.playerCopies.clear();
}

// The player shows before its row is laid out: the refresh is queued again a few times.
void RetryLater(UiService::Shared& shared, const std::shared_ptr<WindowContext>& context) {
    if (context->playerTries >= kPlayerRetries) return;
    if (!context->playerRetry) {
        context->playerRetry = context->dispatcher.CreateTimer();
        context->playerRetry.Interval(kPlayerRetryStep);
        context->playerRetry.IsRepeating(false);
        WeakShared weakShared = shared.weak_from_this();
        WeakContext weakContext = context;
        OnUiTick(context->playerRetry, [weakShared, weakContext](auto const&, auto const&) noexcept {
            if (auto live = weakContext.lock()) QueueRefresh(weakShared, live);
        });
    }
    if (context->playerRetry.IsRunning()) return;
    context->playerTries++;
    context->playerRetry.Start();
}

}  // namespace

xaml::UIElement ShownPlayerContent() noexcept {
    try {
        xaml::Window player = AppWindowOf(WindowRole::Player);
        auto queue = player ? player.DispatcherQueue() : nullptr;
        if (!queue || !queue.HasThreadAccess()) return nullptr;
        auto window = player.AppWindow();
        if (!window || !window.IsVisible()) return nullptr;
        auto content = player.Content();
        return content && content.XamlRoot() ? content : nullptr;
    } catch (...) {
        return nullptr;
    }
}

void FollowPlayerRow(UiService::Shared& shared, const std::shared_ptr<WindowContext>& context,
                     const std::vector<Surface>& surfaces) {
    std::vector<const Surface*> wanted;
    for (const Surface& surface : surfaces) {
        const UiControl& control = surface.control;
        if (surface.kind == SurfaceKind::Control && control.placement == "playbar" && control.player && control.clickable) {
            wanted.push_back(&surface);
        }
    }
    // Hidden, the player keeps its copies (Apple shows the same window again); a button taken away goes at once.
    for (auto it = context->playerCopies.begin(); it != context->playerCopies.end();) {
        bool kept = std::any_of(wanted.begin(), wanted.end(), [&](const Surface* surface) { return surface->key == it->first; });
        if (kept) {
            ++it;
            continue;
        }
        RemoveFromParent(it->second.button);
        it = context->playerCopies.erase(it);
    }
    if (wanted.empty()) return;
    xaml::UIElement content = ShownPlayerContent();
    if (!content) return;
    PlayerRow row = FindPlayerRow(content);
    if (!row.panel) {
        shared.Once("player-row-missing", "Interface: Apple's full-screen player has no lyrics button row yet, "
                                          "its buttons wait for it");
        RetryLater(shared, context);
        return;
    }
    context->playerTries = 0;
    if (context->playerRow.get() != row.panel) {
        RemoveCopies(*context);  // a new player page: the copies of the old one go with it
        context->playerRow = winrt::make_weak(row.panel);
    }
    for (const Surface* surface : wanted) {
        const UiControl& control = surface->control;
        auto found = context->playerCopies.find(surface->key);
        if (found != context->playerCopies.end() && found->second.button.Parent() == row.panel) {
            SyncCopy(found->second, control);
            continue;
        }
        if (found != context->playerCopies.end()) RemoveFromParent(found->second.button);
        PlayerCopy copy = MakeCopy(shared, row.lyrics, control);
        std::uint32_t index = 0;
        if (!row.panel.Children().IndexOf(row.lyrics, index)) index = row.panel.Children().Size();
        row.panel.Children().InsertAt(index, copy.button);
        context->playerCopies[surface->key] = copy;
        shared.Once("player|" + surface->key, "Interface: button \"" + control.label + "\" of " + control.elementId +
                                                  " placed in Apple's full-screen player, before its lyrics button");
    }
}

void ReleasePlayerRow(WindowContext& context) noexcept {
    try {
        if (context.playerRetry) context.playerRetry.Stop();
        context.playerRetry = nullptr;
        RemoveCopies(context);
        context.playerRow = {};
    } catch (...) {
        // The player may be torn down already: nothing is left to put back.
    }
}

}  // namespace ampify::win::ui_service_detail

#endif  // AMPIFY_WITH_XAML
