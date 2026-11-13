// Ampify's buttons in the top bar and the play bar, their "More" menu when the bar lacks room, and the room
// of the song's display beside the transport.

#if defined(AMPIFY_WITH_XAML)

#include "ui_service_detail.hpp"

namespace ampify::win::ui_service_detail {

namespace {

// The accent colour while a top bar or play bar button is on, the text colour otherwise. An icon is drawn again with the
// colour; a text takes it as its foreground. The state is also said to screen readers.
void TintControl(const controls::Button& button, const UiControl& control, bool hasIcon) {
    const wchar_t* key = control.active ? L"AccentTextFillColorPrimaryBrush" : L"TextFillColorPrimaryBrush";
    if (hasIcon) {
        if (xaml::FrameworkElement icon = MakeIconElement(control.icon, 16, key)) {
            // A glyph takes the button's own colour unless it is given one; a drawing already follows the key.
            if (auto glyph = icon.try_as<controls::FontIcon>(); glyph && control.active) {
                if (auto brush = ThemeResource(key).try_as<media::Brush>()) glyph.Foreground(brush);
            }
            button.Content(icon);
        }
    } else if (control.active) {
        if (auto brush = ThemeResource(key).try_as<media::Brush>()) button.Foreground(brush);
    } else {
        button.ClearValue(controls::Control::ForegroundProperty());
    }
    automation::AutomationProperties::SetItemStatus(button, control.active ? L"on" : L"");
}

// Ampify's buttons keep one order whatever is switched on, off or reloaded: by element, then in the order the
// element added them.
bool PlacedBefore(const PlacedControl& a, const std::string& elementId, int id) {
    return a.elementId != elementId ? a.elementId < elementId : a.id < id;
}

}  // namespace

// Where a new button goes among the children of `parent`: after its anchor, and among the Ampify buttons already
// there, at its place in the order above.
std::uint32_t StableIndex(const std::map<std::string, PlacedControl>& placed, const controls::StackPanel& parent,
                          std::optional<std::uint32_t> anchor, const std::string& elementId, int id) {
    auto children = parent.Children();
    std::optional<std::uint32_t> firstAfter, lastBefore;
    for (const auto& [key, other] : placed) {
        std::uint32_t index = 0;
        if (!other.button || other.elementId.empty() || !children.IndexOf(other.button, index)) continue;
        if (PlacedBefore(other, elementId, id)) {
            if (!lastBefore || index > *lastBefore) lastBefore = index;
        } else if (!firstAfter || index < *firstAfter) {
            firstAfter = index;
        }
    }
    if (firstAfter) return *firstAfter;
    if (lastBefore) return *lastBefore + 1;
    return anchor ? *anchor + 1 : children.Size();
}

namespace {

void QueueSongRoom(const UiEventSink& sink, const std::shared_ptr<WindowContext>& context) noexcept;

}  // namespace

void PlaceControls(UiService::Shared& shared, const std::shared_ptr<WindowContext>& context,
                   const std::vector<Surface>& surfaces) {
    std::set<std::string> present;
    std::vector<UiControl> appEntries;
    std::vector<UiControl> contextEntries;
    for (const Surface& surface : surfaces) {
        if (surface.kind != SurfaceKind::Control) continue;
        present.insert(surface.key);
        const UiControl& control = surface.control;
        if (control.placement == "sidebar") {
            PlaceSidebarItem(shared, context, context->placed[surface.key], control);
            continue;
        }
        if (control.placement == "menu-app" || control.placement == "menu-context") {
            bool app = control.placement == "menu-app";
            (app ? appEntries : contextEntries).push_back(control);
            shared.Once("menu|" + surface.key, "Interface: menu entry \"" + control.label + "\" of " + control.elementId +
                                                   (app ? " ready for the main menu" : " ready for the menus of items"));
            continue;
        }
        if (control.placement != "topbar" && control.placement != "playbar") continue;
        PlacedControl& placed = context->placed[surface.key];
        if (placed.button && placed.button.Parent()) {  // in place: only its label may have changed (setLabel)
            if (placed.label != control.label) {
                placed.label = control.label;
                if (!placed.hasIcon) placed.button.Content(winrt::box_value(Wide(control.label)));
                controls::ToolTipService::SetToolTip(placed.button, winrt::box_value(Wide(control.label)));
                automation::AutomationProperties::SetName(placed.button, Wide(control.label));
            }
            if (placed.active != control.active || std::exchange(placed.restain, false)) {
                placed.active = control.active;
                TintControl(placed.button, control, placed.hasIcon);
            }
            if (placed.disabled != control.disabled) {
                placed.disabled = control.disabled;
                placed.button.IsEnabled(!control.disabled);
            }
            continue;
        }

        std::optional<uirender::Insertion> insertion = control.placement == "playbar"
                                                           ? uirender::ChoosePlaybarInsertion(context->probe, control.beside)
                                                           : uirender::ChooseTopbarInsertion(context->probe);
        controls::StackPanel parent{nullptr};
        if (insertion) parent = context->probeObjects[insertion->parent].get().try_as<controls::StackPanel>();
        if (!parent) {
            if (!placed.reported) {
                placed.reported = true;
                shared.journal.Write("Interface: placement \"" + control.placement + "\" not found for button \"" +
                                     control.label + "\" of " + control.elementId + " (provisional landmarks)");
            }
            continue;
        }

        controls::Button button;
        if (insertion->styleFrom >= 0) {
            auto neighbour = context->probeObjects[insertion->styleFrom].get();
            if (!neighbour.try_as<controls::Button>()) {
                // A ToggleButton (Apple Music's lyrics or queue button): its style does not fit a Button, so Ampify
                // button stays flat like it, and takes its size below.
                button.Background(media::SolidColorBrush(winrt::Windows::UI::Color{0, 0, 0, 0}));
                button.BorderThickness(xaml::Thickness{0, 0, 0, 0});
            }
            if (auto native = neighbour.try_as<controls::Control>()) {
                // An AppBarButton's style does not fit a Button: only a plain Button lends its style.
                bool plainButton = winrt::get_class_name(neighbour) == L"Microsoft.UI.Xaml.Controls.Button";
                if (auto style = native.Style(); style && plainButton) {
                    button.Style(style);
                }
                button.Width(native.Width());
                button.Height(native.Height());
                button.MinWidth(native.MinWidth());
                button.MinHeight(native.MinHeight());
                button.Padding(native.Padding());
                button.Margin(native.Margin());
                button.VerticalAlignment(native.VerticalAlignment());
            }
        }
        xaml::FrameworkElement icon = MakeIconElement(control.icon, 16, L"TextFillColorPrimaryBrush");
        if (icon) {
            button.Content(icon);
        } else {
            button.Content(winrt::box_value(Wide(control.label)));
            if (!std::isnan(button.Width())) {  // a text is wider than Apple's icon buttons: it may grow, never shrink
                button.MinWidth(button.Width());
                button.Width(std::numeric_limits<double>::quiet_NaN());
            }
        }
        if (control.active) TintControl(button, control, static_cast<bool>(icon));
        if (control.disabled) button.IsEnabled(false);
        // A text that only shows something: no hover, no press, the pointer goes through to the bar (so no tooltip).
        if (control.clickable) controls::ToolTipService::SetToolTip(button, winrt::box_value(Wide(control.label)));
        else button.IsHitTestVisible(false);
        Name(button, uirender::ElementName(control.elementId, "control-" + std::to_string(control.id)), control.label);
        UiEventSink sink = shared.sink;
        std::string elementId = control.elementId;
        int id = control.id;
        button.Click([sink, elementId, id](IInspectable const&, xaml::RoutedEventArgs const&) noexcept {
            try {
                if (sink) sink(UiEvent{elementId, id, true, {}});
            } catch (...) {
                // A handler runs inside Apple Music's XAML frame: nothing may escape it.
            }
        });

        std::uint32_t index = 0;
        std::optional<std::uint32_t> anchor;
        auto after = insertion->after >= 0 ? context->probeObjects[insertion->after].get().try_as<xaml::UIElement>() : nullptr;
        if (after && parent.Children().IndexOf(after, index)) anchor = index;
        // Beside one of Apple's buttons (the volume), right after it; the others keep their order at the end.
        bool beside = !control.beside.empty() && anchor;
        parent.Children().InsertAt(beside ? *anchor + 1 : StableIndex(context->placed, parent, anchor, control.elementId, control.id),
                                   button);
        placed.button = button;
        placed.label = control.label;
        placed.hasIcon = static_cast<bool>(icon);
        placed.active = control.active;
        placed.disabled = control.disabled;
        placed.elementId = control.elementId;
        placed.id = control.id;
        placed.icon = control.icon;
        placed.clickable = control.clickable;
        placed.folded = false;  // a new button shows; FoldPlaybarButtons folds it again if the bar lacks room
        shared.journal.Write("Interface: button \"" + control.label + "\" of " + control.elementId + " placed in \"" +
                             control.placement + "\" (provisional landmarks)");
    }
    for (auto it = context->placed.begin(); it != context->placed.end();) {
        if (present.count(it->first)) {
            ++it;
            continue;
        }
        RemoveFromParent(it->second.button);
        it = context->placed.erase(it);
    }
    ReleaseSidebarHost(*context);
    context->appEntries = std::move(appEntries);
    context->contextEntries = std::move(contextEntries);
    FollowMenus(shared, context);
    FollowPlaybarMark(shared, context, surfaces);
    FollowPlaybarHover(shared, context);
    QueueSongRoom(shared.sink, context);  // a button gone may leave a single one folded
}

namespace {

constexpr double kSongClearance = 24;  // between the song's display and the first toggle (the volume)
constexpr double kSongMinWidth = 200;  // the transport guard leaves the song at least this: artwork and a few letters

// A layout that sizes the song itself (bottom-bar's width) ran it under the transport in a narrow bar, the star on
// the shuffle button: the song gets a maximum width that ends it a clear space short of the transport, and loses it
// as the room comes back. Only a real overlap starts or narrows it: in a layout where the transport follows the song,
// each narrowing would draw the transport after it, down to the minimum.
// True when the song's size changes, which lays it out again and queues the next pass.
bool KeepSongClearOfTransport(WindowContext& context, const xaml::FrameworkElement& song, const xaml::UIElement& parent) {
    xaml::FrameworkElement transport = context.transport.get();
    double width = song.Width();
    double current = song.MaxWidth();
    bool ours = context.songMaxWidth && std::abs(current - context.songLimit) < 0.5;  // not rewritten since
    bool held = ours && context.songLimitedFor == width;                                // a new width starts again
    double limit = std::numeric_limits<double>::infinity();
    if (!std::isnan(width) && transport && transport.ActualWidth() > 0 && song.ActualWidth() > 0) {
        double songLeft = song.TransformToVisual(parent).TransformPoint({0, 0}).X;
        double transportLeft = transport.TransformToVisual(parent).TransformPoint({0, 0}).X;
        double room = std::max(kSongMinWidth, transportLeft - kSongClearance - songLeft);
        bool overlaps = songLeft + song.ActualWidth() > transportLeft;
        if (transportLeft > songLeft && room < width) {
            if (overlaps) limit = room;
            else if (held) limit = std::max(current, room);  // widens with the room, never narrows without an overlap
        }
    }
    bool binding = song.ActualWidth() >= current - 0.5;  // the song stands at its maximum now
    if (std::isinf(limit)) {
        if (!context.songMaxWidth) return false;
        if (ours) song.MaxWidth(*context.songMaxWidth);  // a value someone else wrote since stays
        context.songMaxWidth.reset();
        return ours && binding;
    }
    if (!ours) context.songMaxWidth = current;
    context.songLimitedFor = width;
    context.songLimit = limit;
    if (std::abs(current - limit) < 0.5) return false;
    song.MaxWidth(limit);
    return limit < song.ActualWidth() - 0.5 || binding;
}

Edges EdgesOf(const xaml::Thickness& margin) {
    Edges edges;
    edges.left = margin.Left;
    edges.top = margin.Top;
    edges.right = margin.Right;
    edges.bottom = margin.Bottom;
    return edges;
}

xaml::Thickness ThicknessOf(const Edges& edges) {
    return {edges.left, edges.top, edges.right, edges.bottom};
}

// A layout that spans the song over the toggles' column (floating-panels) leaves its end to Ampify: Apple's columns
// left the song a third of the bar, with an empty stretch before the volume, and the title cut in a narrow window. The
// song ends a clear space short of the toggles, whatever their number. A layout that sizes the song itself (a width,
// or another alignment) keeps it; a single column gets the layout's margin back (SongMargin tells the two apart).
// True when it gave the song a new margin, which the next layout pass lays out.
bool FitSongRoom(WindowContext& context) {
    xaml::FrameworkElement song = context.songColumn.get();
    xaml::FrameworkElement toggles = context.songToggles.get();
    auto parent = song ? media::VisualTreeHelper::GetParent(song).try_as<xaml::UIElement>() : nullptr;
    if (!song || !toggles || !parent) return false;
    bool ours = controls::Grid::GetColumnSpan(song) >= 2 && std::isnan(song.Width()) &&
                song.HorizontalAlignment() == xaml::HorizontalAlignment::Stretch;
    Edges margin = EdgesOf(song.Margin());
    if (!ours) {
        context.songFitting = false;
        std::optional<Edges> back = context.songMargin.Release(margin);
        if (back) song.Margin(ThicknessOf(*back));
        return KeepSongClearOfTransport(context, song, parent) || back.has_value();
    }
    if (KeepSongClearOfTransport(context, song, parent)) return true;  // gives Apple's maximum back
    Edges base = context.songMargin.Base(margin);
    context.songFitting = true;
    if (song.ActualWidth() <= 0 || toggles.ActualWidth() <= 0) return false;
    double togglesLeft = toggles.TransformToVisual(parent).TransformPoint({0, 0}).X;
    // The slot's end, not the song's width plus its margin: a margin just written is not laid out yet, and adding it to
    // the old width doubled it for a frame (the display flashed narrow at each theme change).
    winrt::Windows::Foundation::Rect slot = primitives::LayoutInformation::GetLayoutSlot(song);
    double slotRight = slot.X + slot.Width;
    double right = std::max(base.right, slotRight - (togglesLeft - kSongClearance));
    if (std::abs(right - margin.right) < 0.5) return false;
    Edges fitted = margin;
    fitted.right = right;
    song.Margin(ThicknessOf(fitted));
    context.songMargin.Wrote(fitted);
    return true;
}

constexpr double kPlaybarClearance = 16;       // Ampify's buttons stay that far from what stands left of them
constexpr double kDisplayRoom = 340;           // a display narrower cuts a title short (Apple keeps ~390 when narrow)
constexpr double kUnfoldMargin = 8;            // room past that before a folded button comes back: no back and forth
constexpr char kMoreGlyph[] = "\xEE\x9C\x92";  // U+E712, "More" in Segoe Fluent Icons

double LeftOf(const xaml::FrameworkElement& element) {
    return element.TransformToVisual(nullptr).TransformPoint({0, 0}).X;
}

// The toggles' spare room, negative when they lack some: their slot in the bar less their width (a bar too narrow cuts
// them at its edge), their distance less kPlaybarClearance to what stands left of them (the transport, and Apple's
// display unless Ampify fits the song itself: FitSongRoom keeps that one clear), and, while the display takes what
// the toggles leave it (no width from the theme), its width less kDisplayRoom.
std::optional<double> PlaybarRoom(const WindowContext& context, const xaml::FrameworkElement& toggles) {
    if (toggles.ActualWidth() <= 0) return std::nullopt;
    xaml::Thickness margin = toggles.Margin();
    double room = std::numeric_limits<double>::infinity();
    if (toggles.HorizontalAlignment() != xaml::HorizontalAlignment::Stretch) {  // stretched, they fill their slot
        room = primitives::LayoutInformation::GetLayoutSlot(toggles).Width -
               (toggles.ActualWidth() + margin.Left + margin.Right);
    }
    double togglesLeft = LeftOf(toggles);
    auto measure = [&](const xaml::FrameworkElement& element) {
        if (!element || element.ActualWidth() <= 0 || element.Visibility() != xaml::Visibility::Visible) return;
        double left = LeftOf(element);
        if (left >= togglesLeft) return;  // a layout put it right of the toggles: not in their way
        room = std::min(room, togglesLeft - (left + element.ActualWidth()) - kPlaybarClearance);
    };
    measure(context.transport.get());
    if (!context.songFitting) measure(context.songDisplay.get());
    xaml::FrameworkElement display = context.songDisplay.get();
    xaml::FrameworkElement song = context.songColumn.get();
    if (display && song && display.ActualWidth() > 0 && std::isnan(display.Width()) && std::isnan(song.Width())) {
        double wanted = std::min({kDisplayRoom, display.MaxWidth(), song.MaxWidth()});
        room = std::min(room, display.ActualWidth() - wanted);
    }
    if (std::isinf(room)) return std::nullopt;
    return room;
}

// Ampify's clickable buttons among the toggles, in their order (PlacedBefore).
std::vector<PlacedControl*> PlaybarButtons(WindowContext& context, const controls::Panel& toggles) {
    std::vector<PlacedControl*> buttons;
    for (auto& [key, placed] : context.placed) {
        std::uint32_t index = 0;
        if (!placed.button || !placed.clickable || placed.elementId.empty()) continue;
        if (toggles.Children().IndexOf(placed.button, index)) buttons.push_back(&placed);
    }
    std::sort(buttons.begin(), buttons.end(),
              [](const PlacedControl* a, const PlacedControl* b) { return PlacedBefore(*a, b->elementId, b->id); });
    return buttons;
}

// The entries of the "More" menu: the folded buttons in their order, each doing what its button does.
void FillPlaybarMenu(const controls::MenuFlyout& menu, WindowContext& context, const UiEventSink& sink) {
    menu.Items().Clear();
    auto toggles = context.songToggles.get().try_as<controls::Panel>();
    if (!toggles) return;
    for (PlacedControl* placed : PlaybarButtons(context, toggles)) {
        if (!placed->folded) continue;
        controls::ToggleMenuFlyoutItem item;
        item.Text(Wide(placed->label));
        item.IsChecked(placed->active);
        item.IsEnabled(!placed->disabled);
        if (auto icon = MakeIconElement(placed->icon, 16, L"TextFillColorPrimaryBrush").try_as<controls::IconElement>()) {
            item.Icon(icon);
        }
        std::string elementId = placed->elementId;
        int id = placed->id;
        item.Click([sink, elementId, id](IInspectable const&, xaml::RoutedEventArgs const&) noexcept {
            try {
                if (sink) sink(UiEvent{elementId, id, true, {}});
            } catch (...) {
                // A handler runs inside Apple Music's XAML frame: nothing may escape it.
            }
        });
        menu.Items().Append(item);
    }
}

// The "More" button, sized and styled like Ampify's button it follows (which took Apple's neighbour's look).
controls::Button MakePlaybarMore(const controls::Button& model, const UiEventSink& sink, const WeakContext& weakContext) {
    controls::Button more;
    if (model.Style()) more.Style(model.Style());
    more.Background(model.Background());
    more.BorderThickness(model.BorderThickness());
    more.Width(model.Width());
    more.Height(model.Height());
    more.MinWidth(model.MinWidth());
    more.MinHeight(model.MinHeight());
    more.Padding(model.Padding());
    more.Margin(model.Margin());
    more.VerticalAlignment(model.VerticalAlignment());
    more.Content(MakeIconElement(kMoreGlyph, 16, L"TextFillColorPrimaryBrush"));
    std::string label = i18n::Text("ui.more");
    controls::ToolTipService::SetToolTip(more, winrt::box_value(Wide(label)));
    Name(more, uirender::ElementName("ampify", "playbar-more"), label);
    controls::MenuFlyout menu;
    // The menu comes as the sender: captured, it would hold itself through its own handler.
    menu.Opening([sink, weakContext](IInspectable const& sender, IInspectable const&) noexcept {
        try {
            if (auto live = weakContext.lock()) FillPlaybarMenu(sender.as<controls::MenuFlyout>(), *live, sink);
        } catch (...) {
            // A handler runs inside Apple Music's XAML frame: nothing may escape it.
        }
    });
    more.Flyout(menu);
    return more;
}

// In a narrow window, Ampify's buttons go one by one into a "More" button, the last one first,
// and come back as the room does, so the bar keeps room for the song and the transport. Apple's own buttons stay. One
// step per call: the layout that follows calls again (SizeChanged). Folding a single button would gain nothing.
void FoldPlaybarButtons(const UiEventSink& sink, const std::shared_ptr<WindowContext>& context) {
    xaml::FrameworkElement togglesElement = context->songToggles.get();
    auto toggles = togglesElement.try_as<controls::Panel>();
    if (!toggles) return;
    std::vector<PlacedControl*> buttons = PlaybarButtons(*context, toggles);
    auto shown = std::find_if(buttons.rbegin(), buttons.rend(), [](const PlacedControl* p) { return !p->folded; });
    auto folded = std::find_if(buttons.begin(), buttons.end(), [](const PlacedControl* p) { return p->folded; });
    auto countFolded = [&buttons]() {
        return static_cast<std::size_t>(
            std::count_if(buttons.begin(), buttons.end(), [](const PlacedControl* p) { return p->folded; }));
    };
    std::size_t foldedCount = countFolded();
    std::optional<double> room = PlaybarRoom(*context, togglesElement);
    auto fold = [](PlacedControl& placed) {
        placed.width = placed.button.ActualWidth() + placed.button.Margin().Left + placed.button.Margin().Right;
        placed.folded = true;
        placed.button.Visibility(xaml::Visibility::Collapsed);
    };
    auto unfold = [](PlacedControl& placed) {
        placed.folded = false;
        placed.button.Visibility(xaml::Visibility::Visible);
    };
    if (room && *room < 0 && shown != buttons.rend()) {
        if (foldedCount == 0) {
            if (buttons.size() < 2) return;
            fold(**shown);  // the "More" button takes the first one's place: a second one goes with it
            ++shown;
        }
        fold(**shown);
    } else if (foldedCount > 0 && room) {
        // The first folded button comes back, needing its width; the last two come back together, in place of the
        // "More" button, which needs about the same.
        double needed = (*folded)->width;
        if (*room >= needed + kUnfoldMargin) {
            unfold(**folded);
            if (foldedCount == 2) unfold(**std::next(folded));
        }
    }
    if (countFolded() == 0) {
        if (context->playbarMore) context->playbarMore.Visibility(xaml::Visibility::Collapsed);
        return;
    }
    if (!context->playbarMore) context->playbarMore = MakePlaybarMore(buttons.front()->button, sink, context);
    std::uint32_t index = 0;
    if (!toggles.Children().IndexOf(context->playbarMore, index)) {
        RemoveFromParent(context->playbarMore);
        toggles.Children().Append(context->playbarMore);
    }
    context->playbarMore.Visibility(xaml::Visibility::Visible);
}

// The row of toggles a little lower while it runs under the window's buttons (caption_clearance.hpp). Through its
// Translation, which leaves its margin and its transform to the theme, and with a short glide.
void KeepTogglesClearOfCaption(WindowContext& context) {
    xaml::FrameworkElement toggles = context.songToggles.get();
    if (!toggles || toggles.ActualWidth() <= 0 || !toggles.XamlRoot()) return;
    winrt::Windows::Foundation::Point origin = toggles.TransformToVisual(nullptr).TransformPoint({0, 0});
    winrt::Windows::Foundation::Numerics::float3 shift = toggles.Translation();
    double top = origin.Y - shift.y;  // where the row stands without Ampify's drop
    double drop = TogglesDrop(origin.X + toggles.ActualWidth(), top, toggles.XamlRoot().Size().Width);
    if (std::abs(drop - context.togglesDrop) < 0.5) return;
    if (!toggles.TranslationTransition()) {
        xaml::Vector3Transition glide;
        glide.Duration(std::chrono::milliseconds(150));
        toggles.TranslationTransition(glide);
    }
    shift.y += static_cast<float>(drop - context.togglesDrop);
    toggles.Translation(shift);
    context.togglesDrop = drop;
}

// Fits the song and folds the buttons once the layout pass is over, a single time for all the sizes it changed: within
// the pass, the widths read are not yet those of the step just taken, and folding on them went back and forth until
// WinUI ended Apple Music for a layout cycle. Each step lays out again, which queues the next one.
void QueueSongRoom(const UiEventSink& sink, const std::shared_ptr<WindowContext>& context) noexcept {
    try {
        if (context->songRoomQueued || !context->dispatcher) return;
        WeakContext weakContext = context;
        context->songRoomQueued =
            context->dispatcher.TryEnqueue(dispatching::DispatcherQueuePriority::Low, [weakContext, sink]() noexcept {
                try {
                    auto live = weakContext.lock();
                    if (!live) return;
                    live->songRoomQueued = false;
                    // A new margin is laid out first: the widths read now would be those before it.
                    if (FitSongRoom(*live)) return;
                    FoldPlaybarButtons(sink, live);
                    KeepTogglesClearOfCaption(*live);
                } catch (...) {
                    // A handler runs inside Apple Music's XAML frame: nothing may escape it.
                }
            });
    } catch (...) {
        // Nothing may escape into Apple Music's XAML frame.
    }
}

}  // namespace

void FollowSongRoom(UiService::Shared& shared, const std::shared_ptr<WindowContext>& context) {
    const std::vector<ProbeNode>& nodes = context->probe;
    std::optional<int> display = uirender::FindByName(nodes, "LCD");
    std::optional<int> toggles = uirender::FindByName(nodes, "NowPlayingToggleButtons");
    if (!display || !toggles) return;
    auto displayElement = context->probeObjects[*display].get().try_as<xaml::FrameworkElement>();
    auto togglesElement = context->probeObjects[*toggles].get().try_as<xaml::FrameworkElement>();
    auto song = displayElement ? media::VisualTreeHelper::GetParent(displayElement).try_as<xaml::FrameworkElement>()
                               : nullptr;
    if (!song || !togglesElement || context->songColumn.get() == song) return;
    // The drop lives in the row's own Translation: it starts again only with another row.
    if (context->songToggles.get() != togglesElement) context->togglesDrop = 0;
    context->songColumn = winrt::make_weak(song);
    context->songToggles = winrt::make_weak(togglesElement);
    context->songMargin = {};
    context->songFitting = false;
    context->songMaxWidth.reset();
    WeakContext weakContext = context;
    std::optional<int> transport = uirender::FindByName(nodes, "TransportControl");
    context->songDisplay = winrt::make_weak(displayElement);
    if (transport) {
        if (auto element = context->probeObjects[*transport].get().try_as<xaml::FrameworkElement>()) {
            context->transport = winrt::make_weak(element);
        }
    }
    UiEventSink sink = shared.sink;
    auto update = [weakContext, sink]() noexcept {
        if (auto live = weakContext.lock()) QueueSongRoom(sink, live);
    };
    auto sized = [update](IInspectable const&, xaml::SizeChangedEventArgs const&) noexcept { update(); };
    song.SizeChanged(sized);
    togglesElement.SizeChanged(sized);
    // The window growing moves the toggles without resizing them: the bar's own size says so.
    if (auto bar = media::VisualTreeHelper::GetParent(togglesElement).try_as<xaml::FrameworkElement>()) {
        bar.SizeChanged(sized);
    }
    for (const xaml::DependencyProperty& property : {controls::Grid::ColumnSpanProperty(),
                                                     xaml::FrameworkElement::WidthProperty(),
                                                     xaml::FrameworkElement::HorizontalAlignmentProperty()}) {
        song.RegisterPropertyChangedCallback(
            property, [update](xaml::DependencyObject const&, xaml::DependencyProperty const&) noexcept { update(); });
    }
    FitSongRoom(*context);
    shared.Once("song-room", "Interface: the playback bar's song follows the room the toggles leave it");
}

}  // namespace ampify::win::ui_service_detail

#endif  // AMPIFY_WITH_XAML
