// The play bar buttons that Apple's players show too (Ampify.ui.playbar.addButton with `player: true`): a copy of each
// stands in the full-screen player's transport row, just before its lyrics button, and in the mini player's display,
// right of the song (its transport row is full), with that button's style. A click on a copy is the play bar button's
// click, and the copy follows the button's label and state. The full-screen player runs on the main window's thread;
// the mini player has its own window context.

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
    ShowToggleState(copy.button, control.active);
    if (copy.disabled != control.disabled) {
        copy.disabled = control.disabled;
        copy.button.IsEnabled(!control.disabled);
    }
}

// A toggle like the lyrics button beside it: same style, padding and height, so its size, halo, hover and press are
// those of its neighbours. On, it is checked, as the lyrics button is while the lyrics show.
PlayerCopy MakeCopy(UiService::Shared& shared, const primitives::ToggleButton& lyrics, const UiControl& control,
                    bool compact) {
    PlayerCopy copy;
    primitives::ToggleButton button;
    if (auto style = lyrics.Style()) button.Style(style);
    button.Padding(lyrics.Padding());
    if (lyrics.ActualHeight() > 0) button.Height(lyrics.ActualHeight());
    if (compact) {
        button.Width(kMiniButtonWidth);
        button.MinWidth(0);
    } else {
        button.Margin(lyrics.Margin());
        button.VerticalAlignment(lyrics.VerticalAlignment());
        if (lyrics.ActualWidth() > 0) button.MinWidth(lyrics.ActualWidth());
    }
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
    // The element says whether it is on (setActive): the copy goes back until then.
    OnPersonToggle(button, [sink, elementId, id](bool) {
        if (sink) sink(UiEvent{elementId, id, true, {}});
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

// The player shows before its row or its artwork is laid out: the refresh is queued again a few times, with `timer`
// and `tries` of the context.
void RetryLater(UiService::Shared& shared, const std::shared_ptr<WindowContext>& context,
                dispatching::DispatcherQueueTimer& timer, int& tries) {
    if (tries >= kPlayerRetries) return;
    if (!timer) {
        timer = context->dispatcher.CreateTimer();
        timer.Interval(kPlayerRetryStep);
        timer.IsRepeating(false);
        WeakShared weakShared = shared.weak_from_this();
        WeakContext weakContext = context;
        OnUiTick(timer, [weakShared, weakContext](auto const&, auto const&) noexcept {
            if (auto live = weakContext.lock()) QueueRefresh(weakShared, live);
        });
    }
    if (timer.IsRunning()) return;
    tries++;
    timer.Start();
}

void RetryLater(UiService::Shared& shared, const std::shared_ptr<WindowContext>& context) {
    RetryLater(shared, context, context->playerRetry, context->playerTries);
}

}  // namespace

void ShowToggleState(const primitives::ToggleButton& button, bool on) {
    automation::AutomationProperties::SetItemStatus(button, on ? L"on" : L"");  // first: OnPersonToggle reads it
    auto checked = button.IsChecked();
    if ((checked && checked.Value()) != on) button.IsChecked(on);
}

void OnPersonToggle(const primitives::ToggleButton& button, std::function<void(bool)> changed) {
    auto handler = [changed = std::move(changed)](IInspectable const& sender, xaml::RoutedEventArgs const&) noexcept {
        try {
            auto toggle = sender.try_as<primitives::ToggleButton>();
            if (!toggle) return;
            bool shown = automation::AutomationProperties::GetItemStatus(toggle) == L"on";
            auto checked = toggle.IsChecked();
            bool asked = checked && checked.Value();
            if (asked == shown) return;  // the owner's own change (ShowToggleState)
            toggle.IsChecked(shown);
            changed(asked);
        } catch (...) {
            // A handler runs inside Apple Music's XAML frame: nothing may escape it.
        }
    };
    button.Checked(handler);
    button.Unchecked(handler);
}

primitives::ToggleButton PlayerLyricsButton(const xaml::UIElement& content) {
    return FindPlayerRow(content).lyrics;
}

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
    const bool mini = context->role == WindowRole::Mini;
    if (mini && wanted.size() > MiniButtonRoom(*context)) wanted.resize(MiniButtonRoom(*context));
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
    xaml::UIElement content = mini ? context->root.get().try_as<xaml::UIElement>() : ShownPlayerContent();
    if (!content) return;
    const std::string where = mini ? "Apple's mini player" : "Apple's full-screen player";
    PlayerRow row = FindPlayerRow(content);
    if (!row.panel) {
        shared.Once(std::string("player-row-missing|") + (mini ? "mini" : "full"),
                    "Interface: " + where + " has no lyrics button row yet, its buttons wait for it");
        RetryLater(shared, context);
        return;
    }
    context->playerTries = 0;
    controls::StackPanel target = mini ? MiniButtonHost(*context, content) : row.panel;
    if (!target) {
        shared.Once("player-row-missing|mini-display", "Interface: " + where + " has no display yet, its buttons wait for it");
        RetryLater(shared, context);
        return;
    }
    if (context->playerRow.get() != target) {
        RemoveCopies(*context);  // a new player page: the copies of the old one go with it
        context->playerRow = winrt::make_weak(target);
    }
    for (const Surface* surface : wanted) {
        const UiControl& control = surface->control;
        auto found = context->playerCopies.find(surface->key);
        if (found != context->playerCopies.end() && found->second.button.Parent() == target) {
            SyncCopy(found->second, control);
            continue;
        }
        if (found != context->playerCopies.end()) RemoveFromParent(found->second.button);
        PlayerCopy copy = MakeCopy(shared, row.lyrics, control, mini);
        std::uint32_t index = target.Children().Size();  // the mini player's: after the star
        if (!mini && !target.Children().IndexOf(row.lyrics, index)) index = target.Children().Size();
        target.Children().InsertAt(index, copy.button);
        context->playerCopies[surface->key] = copy;
        shared.Once(std::string(mini ? "mini|" : "player|") + surface->key,
                    "Interface: button \"" + control.label + "\" of " + control.elementId + " placed in " + where +
                        (mini ? ", right of the song" : ", before its lyrics button"));
    }
}

void FollowAnimatedCover(UiService::Shared& shared, const std::shared_ptr<WindowContext>& context) {
    if (context->role != WindowRole::Main) return;
    UiAnimatedCover cover;
    {
        std::lock_guard<std::mutex> lock(shared.mutex);
        cover = shared.animatedCover;
    }
    const std::string none;
    FollowArtworkMotion(context->playbarMotion, MotionPlace::Playbar, context->root.get(),
                        cover.places.playbar ? cover.video : none, shared.journal);
    // Hidden, the full-screen player lets its video go: it comes back from the cache when the player shows again.
    xaml::UIElement player = cover.places.fullscreen ? ShownPlayerContent() : nullptr;
    if (!player) {
        ReleaseArtworkMotion(context->fullScreenMotion);
        context->coverTries = 0;
        return;
    }
    if (FollowArtworkMotion(context->fullScreenMotion, MotionPlace::FullScreen, player, cover.video, shared.journal)) {
        context->coverTries = 0;
    } else {
        RetryLater(shared, context, context->coverRetry, context->coverTries);
    }
}

void ReleaseAnimatedCover(WindowContext& context) noexcept {
    try {
        if (context.coverRetry) context.coverRetry.Stop();
        context.coverRetry = nullptr;
    } catch (...) {
    }
    ReleaseArtworkMotion(context.playbarMotion);
    ReleaseArtworkMotion(context.fullScreenMotion);
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
