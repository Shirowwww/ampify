// Apple's mini player in the interface service: a window of its own, on its own thread, with its own WindowContext.
// What the elements put in the play bar's audio badge and in the players' transport row reaches it too, and its window
// takes what they ask of it (Ampify.ui.miniPlayer.set): its opacity, a favourite star, the album's animated cover.
// Ampify's buttons stand right of the song, in the display's empty column: the transport row is full at Apple's width.
// Opening and closing it run on the main window's thread.

#if defined(AMPIFY_WITH_XAML)

#include "ui_service_detail.hpp"

#include "player_controls.hpp"
#include "xaml_ending.hpp"

namespace ampify::win::ui_service_detail {

namespace {

constexpr wchar_t kHostName[] = L"AMPIFY_MiniButtons";
constexpr int kDisplaySearchLimit = 400;  // the display sits near the top of the mini player's tree
constexpr std::size_t kMiniSlots = 2;     // buttons of kMiniButtonWidth in the display's right column
constexpr double kStarGlyphSize = 15;     // the size of Apple's glyphs in the mini player's transport row
constexpr double kHostMargin = 8;         // from the display's bottom (and right edge when it has no third column)
// The star reads Apple's state this often: a click, Apple's own menus and a new song all change it.
constexpr auto kStarWatch = std::chrono::milliseconds(1000);
constexpr auto kStarHold = std::chrono::milliseconds(2500);  // Apple applies a favourite within this time
constexpr wchar_t kStarGlyph[] = L"\uE734";        // FavoriteStar
constexpr wchar_t kStarFilledGlyph[] = L"\uE735";  // FavoriteStarFill
constexpr wchar_t kIconFonts[] = L"Segoe Fluent Icons, Segoe MDL2 Assets";

// The display's grid (Grid#LCD inside MiniNowPlayingControl#LCD).
controls::Grid FindDisplay(const xaml::UIElement& content) {
    std::vector<xaml::DependencyObject> queue{content};
    for (std::size_t i = 0; i < queue.size() && queue.size() < static_cast<std::size_t>(kDisplaySearchLimit); i++) {
        auto grid = queue[i].try_as<controls::Grid>();
        if (grid && grid.Name() == L"LCD") return grid;
        int count = media::VisualTreeHelper::GetChildrenCount(queue[i]);
        for (int child = 0; child < count; child++) queue.push_back(media::VisualTreeHelper::GetChild(queue[i], child));
    }
    return nullptr;
}

void ShowStar(const primitives::ToggleButton& star, bool favorite) {
    controls::FontIcon icon;
    icon.Glyph(favorite ? kStarFilledGlyph : kStarGlyph);
    icon.FontFamily(media::FontFamily(kIconFonts));
    icon.FontSize(kStarGlyphSize);
    star.Content(icon);
    ShowToggleState(star, favorite);
}

// Only the star changes on a tick; a song that cannot be a favourite takes it away through a refresh.
void WatchStar(UiService::Shared& shared, const std::shared_ptr<WindowContext>& context) {
    if (context->miniStarCheck) return;
    context->miniStarCheck = context->dispatcher.CreateTimer();
    context->miniStarCheck.Interval(kStarWatch);
    context->miniStarCheck.IsRepeating(true);
    WeakShared weakShared = shared.weak_from_this();
    WeakContext weakContext = context;
    OnUiTick(context->miniStarCheck, [weakShared, weakContext](auto const&, auto const&) noexcept {
        auto live = weakContext.lock();
        if (!live || !live->miniStar || std::chrono::steady_clock::now() < live->miniStarHeld) return;
        std::optional<bool> favorite = PlayingFavorite();
        if (!favorite) {
            QueueRefresh(weakShared, live);
        } else if (live->miniStarShown != favorite) {
            live->miniStarShown = favorite;
            ShowStar(live->miniStar, *favorite);
        }
    });
    context->miniStarCheck.Start();
}

primitives::ToggleButton MakeStar(UiService::Shared& shared, const std::shared_ptr<WindowContext>& context,
                                  const primitives::ToggleButton& style) {
    primitives::ToggleButton star;
    if (style) {
        if (auto look = style.Style()) star.Style(look);
        star.Padding(style.Padding());
        if (style.ActualHeight() > 0) star.Height(style.ActualHeight());
    }
    star.Width(kMiniButtonWidth);
    star.MinWidth(0);
    winrt::hstring label = Wide(i18n::Text("mini.favorite"));
    controls::ToolTipService::SetToolTip(star, winrt::box_value(label));
    automation::AutomationProperties::SetName(star, label);
    star.Name(L"AMPIFY_MiniFavorite");
    WeakShared weakShared = shared.weak_from_this();
    WeakContext weakContext = context;
    // The star shows what Apple does at once, then the watch shows the state Apple ends up with.
    OnPersonToggle(star, [weakShared, weakContext](bool asked) {
        auto live = weakContext.lock();
        if (!live || !live->miniStar) return;
        bool changed = SetPlayingFavorite(asked);
        bool shown = changed ? asked : PlayingFavorite().value_or(false);
        live->miniStarShown = shown;
        live->miniStarHeld = std::chrono::steady_clock::now() + kStarHold;
        ShowStar(live->miniStar, shown);
        auto shared = weakShared.lock();
        if (shared) {
            shared->journal.Write(changed ? std::string("Interface: the mini player's star made the song ") +
                                                (asked ? "a favourite" : "no longer a favourite")
                                          : std::string("Interface: Apple refused to change the song's favourite"));
        }
    });
    return star;
}

void ReleaseStar(WindowContext& context) {
    if (context.miniStarCheck) context.miniStarCheck.Stop();
    context.miniStarCheck = nullptr;
    RemoveFromParent(context.miniStar);
    context.miniStar = nullptr;
    context.miniStarShown.reset();
}

// The star, first in the host, while an element asks for it and Apple can make the song playing a favourite.
void FollowMiniStar(UiService::Shared& shared, const std::shared_ptr<WindowContext>& context,
                    const xaml::UIElement& content, bool wanted) {
    std::optional<bool> favorite = wanted ? PlayingFavorite() : std::nullopt;
    if (!favorite) {
        ReleaseStar(*context);
        return;
    }
    controls::StackPanel host = MiniButtonHost(*context, content);
    if (!host) return;
    if (!context->miniStar) {
        context->miniStar = MakeStar(shared, context, PlayerLyricsButton(content));
        shared.Once("mini-favorite", "Interface: favourite star placed in Apple's mini player");
    }
    WatchStar(shared, context);
    if (context->miniStar.Parent() != host) {
        RemoveFromParent(context->miniStar);
        host.Children().InsertAt(0, context->miniStar);
    }
    if (context->miniStarShown != favorite) {
        context->miniStarShown = favorite;
        ShowStar(context->miniStar, *favorite);
    }
}

}  // namespace

namespace {

// Right of the song, in the display's last column, under the caption buttons (which show under the pointer).
void PlaceInDisplay(const controls::StackPanel& host, const controls::Grid& display) {
    host.VerticalAlignment(xaml::VerticalAlignment::Bottom);
    host.Margin(xaml::Thickness{0, 0, 0, kHostMargin});
    std::int32_t columns = static_cast<std::int32_t>(display.ColumnDefinitions().Size());
    if (columns >= 3) {
        controls::Grid::SetColumn(host, columns - 1);
        host.HorizontalAlignment(xaml::HorizontalAlignment::Center);
    } else {
        controls::Grid::SetColumnSpan(host, std::max(columns, 1));
        host.HorizontalAlignment(xaml::HorizontalAlignment::Right);
        host.Margin(xaml::Thickness{0, 0, kHostMargin, kHostMargin});
    }
    std::int32_t rows = static_cast<std::int32_t>(display.RowDefinitions().Size());
    if (rows > 1) controls::Grid::SetRowSpan(host, rows);
}

// The compact mini player: at the left of the band above Apple's raised row of controls, inside that row, so the
// buttons show and hide with it (the Minimal mini player snippet fades it out of the pointer).
void PlaceOverControls(const controls::StackPanel& host, const controls::Grid& row, double band) {
    host.VerticalAlignment(xaml::VerticalAlignment::Top);
    host.HorizontalAlignment(xaml::HorizontalAlignment::Left);
    host.Margin(xaml::Thickness{0, -band, 0, 0});
    controls::Grid::SetColumnSpan(host, std::max(static_cast<std::int32_t>(row.ColumnDefinitions().Size()), 1));
}

}  // namespace

controls::StackPanel MiniButtonHost(WindowContext& context, const xaml::UIElement& content) {
    controls::Grid overControls = context.miniCompact.on ? context.miniCompact.controls.get() : nullptr;
    controls::Grid parent = overControls ? overControls : (content ? FindDisplay(content) : nullptr);
    if (!parent) return nullptr;
    if (context.miniHost && context.miniHost.Parent() == parent) {
        // The band is known once Apple laid the window out, maybe after the buttons moved over the controls.
        if (overControls) context.miniHost.Margin(xaml::Thickness{0, -context.miniCompact.band, 0, 0});
        return context.miniHost;
    }
    if (!context.miniHost) {
        context.miniHost = controls::StackPanel();
        context.miniHost.Name(kHostName);
        context.miniHost.Orientation(controls::Orientation::Horizontal);
    }
    controls::StackPanel host = context.miniHost;  // moved whole: the star and the buttons in it stay
    RemoveFromParent(host);
    for (const auto& property : {controls::Grid::RowProperty(), controls::Grid::ColumnProperty(),
                                 controls::Grid::RowSpanProperty(), controls::Grid::ColumnSpanProperty()}) {
        host.ClearValue(property);
    }
    if (overControls) {
        PlaceOverControls(host, overControls, context.miniCompact.band);
    } else {
        PlaceInDisplay(host, parent);
    }
    parent.Children().Append(host);
    return host;
}

std::size_t MiniButtonRoom(const WindowContext& context) {
    return context.miniStar ? kMiniSlots - 1 : kMiniSlots;
}

void FollowMiniPlayer(UiService::Shared& shared, const std::shared_ptr<WindowContext>& context) {
    if (context->role != WindowRole::Mini) return;
    std::vector<Surface> surfaces;
    UiMiniPlayer wish;
    {
        std::lock_guard<std::mutex> lock(shared.mutex);
        surfaces = shared.surfaces;
        wish = shared.miniPlayer;
    }
    auto content = context->root.get().try_as<xaml::UIElement>();
    FollowMiniCompact(shared, *context, content);  // first: it decides where Ampify's buttons stand
    FollowAudioBadge(shared, context);
    FollowMiniStar(shared, context, content, wish.favorite);
    FollowPlayerRow(shared, context, surfaces);
    context->miniWindow =
        FollowMiniOpacity(WindowHandle(context->window.get()), wish.opacity, context->miniWindow, &shared.journal);
    FollowMiniMotion(context->miniMotion, context->root.get(), wish.motion, shared.journal);
}

void ReleaseMiniPlayer(WindowContext& context) noexcept {
    if (context.role != WindowRole::Mini) return;
    ReleaseMiniMotion(context.miniMotion);
    ReleaseMiniCompact(context);
    try {
        ReleaseStar(context);
        RemoveFromParent(context.miniHost);
        context.miniHost = nullptr;
        context.miniWindow = FollowMiniOpacity(WindowHandle(context.window.get()), 1.0, context.miniWindow, nullptr);
    } catch (...) {
        // The window is going: nothing is left to give back.
    }
}

void RunPendingMiniAction(UiService::Shared& shared) {
    std::optional<MiniPlayerAction> action;
    {
        std::lock_guard<std::mutex> lock(shared.mutex);
        action = std::exchange(shared.miniPlayerPending, std::nullopt);
    }
    if (action) RunMiniPlayerAction(*action, shared.journal);
}

}  // namespace ampify::win::ui_service_detail

#endif  // AMPIFY_WITH_XAML
