// The extensions' entries in Apple Music's sidebar (its footer, or its header for the Marketplace), and the
// logo of the folded sidebar.

#if defined(AMPIFY_WITH_XAML)

#include "ui_service_detail.hpp"

#include "../core/kit_layout.hpp"
#include "../core/sidebar_entry.hpp"
#include "../core/sidebar_hold.hpp"

#include <limits>

namespace ampify::win::ui_service_detail {

namespace {

// Sidebar entries. Apple Music fills its menu and footer through item sources of its own view models: an item Ampify
// added to its footer source crashed it at once (1.1792), and a NavigationViewItem anywhere in the pane would report
// its taps to Apple Music's navigation. So the entries are flat buttons laid out like the sidebar's items, in a panel
// that takes the place of the NavigationView's PaneFooter, with Apple Music's own footer (a separator) kept at its end.
controls::NavigationView FindNavigation(const WindowContext& context) {
    for (size_t i = 0; i < context.probe.size(); i++) {
        if (!context.probe[i].Is(uirender::kProbeNavigationView)) continue;
        if (auto navigation = context.probeObjects[i].get().try_as<controls::NavigationView>()) return navigation;
    }
    return nullptr;
}

// Takes the pane's watches off (FollowSidebarPane), when the entries leave the list.
void StopFollowingSidebarPane(WindowContext& context) {
    if (auto navigation = context.sidebarNavigation.get()) {
        if (context.sidebarPaneToken) {
            navigation.UnregisterPropertyChangedCallback(controls::NavigationView::IsPaneOpenProperty(),
                                                         context.sidebarPaneToken);
        }
        if (context.sidebarModeToken) {
            navigation.UnregisterPropertyChangedCallback(controls::NavigationView::DisplayModeProperty(),
                                                         context.sidebarModeToken);
        }
    }
    if (auto scroll = context.sidebarScroll.get(); scroll && context.sidebarScrollSize) {
        scroll.SizeChanged(context.sidebarScrollSize);
    }
    context.sidebarScroll = winrt::weak_ref<controls::ScrollViewer>{nullptr};
    context.sidebarScrollSize = {};
    context.sidebarEntryCut = 0;
    context.sidebarNavigation = winrt::weak_ref<controls::NavigationView>{nullptr};
    context.sidebarAppleIcon = winrt::weak_ref<xaml::FrameworkElement>{nullptr};
    context.sidebarPaneToken = 0;
    context.sidebarModeToken = 0;
}

controls::StackPanel EnsureSidebarHost(WindowContext& context, const controls::NavigationView& navigation) {
    xaml::UIElement footer = navigation.PaneFooter();
    if (context.sidebarHost && footer == context.sidebarHost) return context.sidebarHost;
    controls::StackPanel host;
    Name(host, "AMPIFY_SidebarHost");
    navigation.PaneFooter(nullptr);  // frees Apple Music's footer, a separator, which becomes the first child of the panel
    if (footer) host.Children().Append(footer);
    navigation.PaneFooter(host);
    context.sidebarHost = host;
    context.sidebarOriginal = footer;
    return host;
}

}  // namespace

// Puts Apple Music's own footer back once no element has an entry left.
void ReleaseSidebarHost(WindowContext& context) {
    if (!context.sidebarHost || context.sidebarHost.Children().Size() > (context.sidebarOriginal ? 1u : 0u)) return;
    controls::NavigationView navigation = FindNavigation(context);
    if (navigation && navigation.PaneFooter() == context.sidebarHost) {
        navigation.PaneFooter(nullptr);
        context.sidebarHost.Children().Clear();
        navigation.PaneFooter(context.sidebarOriginal);
    }
    // An entry in the header still follows the pane, to come back to the list as soon as it folds.
    const bool inHeader = std::any_of(context.placed.begin(), context.placed.end(), [](const auto& item) {
        return item.second.button && item.second.inHeader;
    });
    if (!inHeader) StopFollowingSidebarPane(context);
    context.sidebarHost = nullptr;
    context.sidebarOriginal = nullptr;
}

namespace {

// The icon of a sidebar entry, in the text brush like Apple Music's own icons (the Marketplace gives Ampify's logo as
// an SVG), or in the accent while the entry is on.
xaml::FrameworkElement SidebarIcon(const UiControl& control, bool active) {
    const wchar_t* key = active ? L"AccentTextFillColorPrimaryBrush" : L"TextFillColorPrimaryBrush";
    xaml::FrameworkElement icon = nullptr;
    if (!control.icon.empty() && !uirender::IsGlyphIcon(control.icon)) icon = MakeIconElement(control.icon, 16, key);
    if (!icon) {
        controls::FontIcon glyph;
        glyph.FontSize(16);
        glyph.Glyph(uirender::IsGlyphIcon(control.icon) ? Wide(control.icon) : winrt::hstring(L"\xE790"));  // Color
        if (active) {
            if (auto accent = ThemeResource(key).try_as<media::Brush>()) glyph.Foreground(accent);
        }
        icon = glyph;
    }
    icon.VerticalAlignment(xaml::VerticalAlignment::Center);
    return icon;
}

// The inside of a sidebar entry: the icon and the label, and a bar at the left edge like Apple Music's own selected item.
// An entry that is on takes the accent colour. The icon is drawn again with the colour, so a state change rebuilds it.
xaml::UIElement SidebarContent(const UiControl& control, bool active) {
    const wchar_t* key = active ? L"AccentTextFillColorPrimaryBrush" : L"TextFillColorPrimaryBrush";
    const media::Brush accent = active ? ThemeResource(key).try_as<media::Brush>() : nullptr;
    controls::StackPanel row;
    row.Orientation(controls::Orientation::Horizontal);
    row.Spacing(16);
    xaml::FrameworkElement icon = SidebarIcon(control, active);
    controls::TextBlock label;
    label.Text(Wide(control.label));
    label.VerticalAlignment(xaml::VerticalAlignment::Center);
    if (accent) label.Foreground(accent);
    row.Children().Append(icon);
    row.Children().Append(label);

    controls::Grid content;
    content.Children().Append(row);
    if (active) {
        controls::Border bar;
        bar.Width(3);
        bar.Height(16);
        bar.CornerRadius(xaml::CornerRadius{1.5, 1.5, 1.5, 1.5});
        bar.HorizontalAlignment(xaml::HorizontalAlignment::Left);
        bar.VerticalAlignment(xaml::VerticalAlignment::Center);
        bar.Margin(xaml::Thickness{-12, 0, 0, 0});  // on the button's own edge, in the padding
        bar.IsHitTestVisible(false);
        if (auto fill = ThemeResource(L"AccentFillColorDefaultBrush").try_as<media::Brush>()) bar.Background(fill);
        content.Children().Append(bar);
    }
    return content;
}

// A flat button with the height, margins and corners of Apple Music's sidebar items, and the subtle hover of Fluent.
// Its content stretches to its height, so the icon is centred on the button folded and open alike: centred in the
// open row instead (as tall as its label), it was rounded a pixel lower than in the folded column.
controls::Button MakeSidebarButton(bool active) {
    const std::wstring brushes =
        L"<StaticResource x:Key='ButtonBackground' ResourceKey='SubtleFillColorTransparentBrush'/>"
        L"<StaticResource x:Key='ButtonBackgroundPointerOver' ResourceKey='SubtleFillColorSecondaryBrush'/>"
        L"<StaticResource x:Key='ButtonBackgroundPressed' ResourceKey='SubtleFillColorTertiaryBrush'/>"
        L"<StaticResource x:Key='ButtonBorderBrush' ResourceKey='SubtleFillColorTransparentBrush'/>"
        L"<StaticResource x:Key='ButtonBorderBrushPointerOver' ResourceKey='SubtleFillColorTransparentBrush'/>"
        L"<StaticResource x:Key='ButtonBorderBrushPressed' ResourceKey='SubtleFillColorTransparentBrush'/>"
        L"<StaticResource x:Key='ButtonForegroundPointerOver' ResourceKey='TextFillColorPrimaryBrush'/>"
        L"<StaticResource x:Key='ButtonForegroundPressed' ResourceKey='TextFillColorSecondaryBrush'/>";
    const std::wstring text =
        L"<Button xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation' "
        L"xmlns:x='http://schemas.microsoft.com/winfx/2006/xaml' HorizontalAlignment='Stretch' "
        L"HorizontalContentAlignment='Left' VerticalContentAlignment='Stretch' MinWidth='0' Height='36' "
        L"Margin='4,2,4,2' Padding='12,0,12,0' CornerRadius='4' BorderThickness='0'><Button.Resources><ResourceDictionary><ResourceDictionary.ThemeDictionaries>"
        L"<ResourceDictionary x:Key='Default'>" + brushes + L"</ResourceDictionary>"
        L"<ResourceDictionary x:Key='Light'>" + brushes + L"</ResourceDictionary>"
        L"</ResourceDictionary.ThemeDictionaries></ResourceDictionary></Button.Resources></Button>";
    controls::Button button = xaml::Markup::XamlReader::Load(text).as<controls::Button>();
    automation::AutomationProperties::SetItemStatus(button, active ? L"on" : L"");
    return button;
}

constexpr double kMoreButtonWidth = 32;  // Apple's "···" before its first layout
constexpr double kHeaderButtonHeight = 28;  // the height of "···"
constexpr double kHeaderSpacing = 2;

// The sidebar's header (Apple's logo, its name and "···"), where Ampify's own entry stands with ampify.entry = header.
// Nothing while the sidebar is folded: Apple hides the header then, and the entry stays in the list.
controls::Grid EntryHeader(const UiService::Shared& shared, const controls::NavigationView& navigation,
                           const UiControl& control) {
    if (!IsFirstParty(control.elementId) || !shared.entryInHeader || !navigation.IsPaneOpen()) return nullptr;
    return navigation.PaneHeader().try_as<controls::Grid>();
}

// The room the hover sidebar's pin takes just left of "···" (sidebar_hover.cpp). It shows whenever the pane is open,
// the only time the entry stands in the header, so its visibility is not read: it may change after this update.
double PinRoom(const controls::Grid& header) {
    for (const xaml::UIElement& child : header.Children()) {
        auto pin = child.try_as<xaml::FrameworkElement>();
        if (pin && pin.Name() == L"AMPIFY_SidebarPin") return pin.Width() + kHeaderSpacing;
    }
    return 0;
}

// Just left of "···" (and of the hover sidebar's pin) and level with it. Read again on each update: a theme's @media
// moves "···" as the window changes size, or writes its margin after the entry was placed.
void KeepBesideMore(const controls::Grid& header, const controls::Button& button) {
    xaml::Thickness margin{0, 0, kMoreButtonWidth + kHeaderSpacing, 0};
    for (const xaml::UIElement& child : header.Children()) {
        auto more = child.try_as<xaml::FrameworkElement>();
        if (!more || more.Name() != L"MoreButton") continue;
        xaml::Thickness moreMargin = more.Margin();
        double width = more.ActualWidth() > 0 ? more.ActualWidth() : kMoreButtonWidth;
        double right = ampify::LeftOfMoreMargin(moreMargin.Right, width, kHeaderSpacing, PinRoom(header));
        margin = xaml::Thickness{0, moreMargin.Top, right, moreMargin.Bottom};
        controls::Grid::SetColumn(button, controls::Grid::GetColumn(more));
        controls::Grid::SetRow(button, controls::Grid::GetRow(more));
    }
    if (button.Margin() != margin) button.Margin(margin);
}

// The entry's icon as a button the size of "···", just left of it in the same cell, its label as the tooltip.
controls::Button MakeHeaderButton(const controls::Grid& header, const UiControl& control, bool active) {
    controls::Button button = MakeSidebarButton(active);  // for its subtle brushes
    button.Content(SidebarIcon(control, active));
    button.HorizontalAlignment(xaml::HorizontalAlignment::Right);
    button.HorizontalContentAlignment(xaml::HorizontalAlignment::Center);
    button.VerticalAlignment(xaml::VerticalAlignment::Center);
    button.Width(kMoreButtonWidth);
    button.Height(kHeaderButtonHeight);
    button.Padding(xaml::Thickness{});
    KeepBesideMore(header, button);
    controls::ToolTipService::SetToolTip(button, winrt::box_value(Wide(control.label)));
    return button;
}

// An entry is on when its element asked for it (Ampify.ui.setActive), or while the page of that element is on screen.
// Ampify's own page (Settings > Ampify) lights the Marketplace's entry, the one that stands for Ampify.
bool SidebarEntryIsOn(const WindowContext& context, const UiControl& control) {
    if (control.active) return true;
    if (!context.route || context.pageClosed) return false;
    bool ampifyPage = context.route->second == kAmpifyPagePath && IsFirstParty(control.elementId);
    return ampifyPage || context.route->first == control.elementId;
}

// Folded: the icon alone, centred; the label is the tooltip. Open: the row of SidebarContent.
xaml::FrameworkElement SidebarCompactContent(const UiControl& control, bool active) {
    xaml::FrameworkElement icon = SidebarIcon(control, active);
    icon.HorizontalAlignment(xaml::HorizontalAlignment::Center);
    return icon;
}

const double kAutoWidth = std::numeric_limits<double>::quiet_NaN();  // open: the row fills the sidebar
constexpr double kOpenPadding = 12;     // the open row's side padding, as in the markup
constexpr std::size_t kItemSearchLimit = 64;  // the icon sits a few levels into the item's template

// The open button's padding is the markup's; folded, the icon gets the whole width (the column is narrow). Both move
// the icon by `shift`, onto the axis of Apple's own icons.
void LayoutOpenEntry(PlacedControl& placed, bool active, double shift) {
    placed.button.Width(kAutoWidth);
    placed.button.HorizontalAlignment(xaml::HorizontalAlignment::Stretch);
    placed.button.HorizontalContentAlignment(xaml::HorizontalAlignment::Left);
    placed.button.Padding(xaml::Thickness{kOpenPadding + shift, 0, kOpenPadding, 0});
    placed.button.Content(SidebarContent(placed.entry, active));
    controls::ToolTipService::SetToolTip(placed.button, nullptr);
}

void LayoutFoldedEntry(PlacedControl& placed, double width, bool active, double shift) {
    placed.button.Width(width);
    placed.button.HorizontalAlignment(xaml::HorizontalAlignment::Center);
    placed.button.HorizontalContentAlignment(xaml::HorizontalAlignment::Center);
    placed.button.Padding(xaml::Thickness{});
    xaml::FrameworkElement icon = SidebarCompactContent(placed.entry, active);
    icon.Margin(xaml::Thickness{shift, 0, -shift, 0});
    placed.button.Content(icon);
    controls::ToolTipService::SetToolTip(placed.button, winrt::box_value(Wide(placed.entry.label)));
}

// Lays an entry out for the pane's state. Writes only what the button does not show yet, so a width a theme wrote on
// it is put back. Margin, height and colours stay the theme's (names.md). Centred, so the entry sits in the column
// whichever way the sidebar flows.
void LayoutSidebarEntry(PlacedControl& placed, const controls::NavigationView& navigation, bool active, double shift) {
    const bool paneOpen = navigation.IsPaneOpen();
    const xaml::Thickness margin = placed.button.Margin();
    const double width =
        paneOpen ? kAutoWidth : ampify::FoldedEntryWidth(navigation.CompactPaneLength(), margin.Left, margin.Right);
    const ampify::EntryLayout shown{placed.paneOpen, placed.active, placed.button.Width(), placed.iconShift};
    const ampify::EntryLayout wanted{paneOpen, active, width, shift};
    bool restain = std::exchange(placed.restain, false);
    if (placed.laidOut && !ampify::EntryLayoutChanged(shown, wanted) && !restain) return;
    placed.laidOut = true;
    placed.paneOpen = paneOpen;
    placed.active = active;
    placed.iconShift = shift;
    if (paneOpen) {
        LayoutOpenEntry(placed, active, shift);
    } else {
        LayoutFoldedEntry(placed, width, active, shift);
    }
    automation::AutomationProperties::SetItemStatus(placed.button, active ? L"on" : L"");
}

xaml::FrameworkElement FindItemIcon(const xaml::DependencyObject& item) {
    std::vector<xaml::DependencyObject> queue{item};
    for (std::size_t i = 0; i < queue.size() && queue.size() < kItemSearchLimit; i++) {
        auto element = queue[i].try_as<xaml::FrameworkElement>();
        if (element && element.Name() == L"IconBox" && element.ActualWidth() > 0) return element;
        int count = media::VisualTreeHelper::GetChildrenCount(queue[i]);
        for (int child = 0; child < count; child++) queue.push_back(media::VisualTreeHelper::GetChild(queue[i], child));
    }
    return nullptr;
}

// The icon of Apple's first laid-out item in its sidebar list (MenuItemsHost, which the discovery lists but does not
// enter), found again only when Apple rebuilt the list.
xaml::FrameworkElement AppleItemIcon(WindowContext& context) {
    if (auto icon = context.sidebarAppleIcon.get(); icon && icon.IsLoaded() && icon.ActualWidth() > 0) return icon;
    std::optional<int> list = uirender::FindByName(context.probe, "MenuItemsHost");
    xaml::DependencyObject repeater =
        list && *list >= 0 && *list < static_cast<int>(context.probeObjects.size()) ? context.probeObjects[*list].get()
                                                                                    : nullptr;
    int count = repeater ? media::VisualTreeHelper::GetChildrenCount(repeater) : 0;
    for (int i = 0; i < count; i++) {
        auto item = media::VisualTreeHelper::GetChild(repeater, i).try_as<controls::NavigationViewItem>();
        xaml::FrameworkElement icon = item ? FindItemIcon(item) : nullptr;
        if (!icon) continue;
        context.sidebarAppleIcon = winrt::make_weak(icon);
        return icon;
    }
    return nullptr;
}

// The x of an Apple icon's middle in `host`, from its item's arranged offset: a theme's hover that slides the item
// (Glass's hover-grow) moves its render transform, not its offset, so the axis holds still under the pointer.
std::optional<double> AppleIconAxis(const xaml::FrameworkElement& icon, const xaml::UIElement& host) {
    xaml::DependencyObject at = icon;
    while (at && !at.try_as<controls::NavigationViewItem>()) at = media::VisualTreeHelper::GetParent(at);
    auto item = at ? at.try_as<controls::NavigationViewItem>() : nullptr;
    auto list = item ? media::VisualTreeHelper::GetParent(item).try_as<xaml::UIElement>() : nullptr;
    if (!list) return std::nullopt;
    const float inItem = icon.TransformToVisual(item).TransformPoint({static_cast<float>(icon.ActualWidth() / 2), 0}).X;
    const float listX = list.TransformToVisual(host).TransformPoint({0, 0}).X;
    return listX + item.ActualOffset().x + inItem;
}

// The x of a folded entry's icon in its host, before any shift: the middle of the button where it was arranged. Not
// from its slot: a host narrower than the button (Glass) makes the button overflow its slot, off the slot's middle.
double FoldedEntryAxis(const controls::Button& button) {
    return button.ActualOffset().x + button.ActualWidth() / 2;
}

// The x of an open entry's icon in its host, before any shift: the first child of its row (SidebarContent), less the
// shift its padding already holds.
std::optional<double> OpenEntryAxis(const PlacedControl& placed) {
    auto content = placed.button.Content().try_as<controls::Panel>();
    auto row = content && content.Children().Size() ? content.Children().GetAt(0).try_as<controls::Panel>() : nullptr;
    auto icon = row && row.Children().Size() ? row.Children().GetAt(0).try_as<xaml::FrameworkElement>() : nullptr;
    if (!icon || icon.ActualWidth() <= 0) return std::nullopt;
    const float inButton =
        icon.TransformToVisual(placed.button).TransformPoint({static_cast<float>(icon.ActualWidth() / 2), 0}).X;
    return placed.button.ActualOffset().x + inButton - placed.iconShift;
}

// The shift for the pane's state: the open row's icon does not sit where the folded one is centred, so each state has
// its own, measured while an entry is laid out in that state.
double EntryShift(const WindowContext& context, const controls::NavigationView& navigation) {
    return navigation.IsPaneOpen() ? context.sidebarOpenIconShift : context.sidebarIconShift;
}

// Measured on an entry laid out for the pane's current state and on screen, where both axes are current.
void MeasureIconShift(WindowContext& context, const PlacedControl& placed, const controls::NavigationView& navigation) {
    const bool open = navigation.IsPaneOpen();
    if (!placed.laidOut || placed.paneOpen != open || placed.button.ActualWidth() <= 0) return;
    if (!context.sidebarHost || placed.button.Parent() != context.sidebarHost) return;
    xaml::FrameworkElement icon = AppleItemIcon(context);
    std::optional<double> apple = icon ? AppleIconAxis(icon, context.sidebarHost) : std::nullopt;
    std::optional<double> entry = open ? OpenEntryAxis(placed) : std::optional<double>{FoldedEntryAxis(placed.button)};
    if (!apple || !entry) return;
    const xaml::XamlRoot root = placed.button.XamlRoot();
    const double scale = root ? root.RasterizationScale() : 1.0;
    (open ? context.sidebarOpenIconShift : context.sidebarIconShift) = ampify::EntryIconShift(*apple, *entry, scale);
}

constexpr double kEntryHeight = 36;  // MakeSidebarButton's, when a theme has not written another
constexpr double kMaxEntryCut = 8;   // an entry never gets lower than 28

controls::ScrollViewer AppleItemsScroll(WindowContext& context) {
    if (auto scroll = context.sidebarScroll.get()) return scroll;
    std::optional<int> node = uirender::FindByName(context.probe, "MenuItemsScrollViewer");
    if (!node || *node >= static_cast<int>(context.probeObjects.size())) return nullptr;
    return context.probeObjects[*node].get().try_as<controls::ScrollViewer>();
}

std::vector<PlacedControl*> ListEntries(WindowContext& context) {
    std::vector<PlacedControl*> entries;
    for (auto& item : context.placed) {
        PlacedControl& placed = item.second;
        if (placed.button && !placed.inHeader && placed.button.Parent() == context.sidebarHost) entries.push_back(&placed);
    }
    return entries;
}

// Folded, the entries sit in the footer, which Apple Music's list yields to: a low window cut its last icon (Playlists)
// in half. The entries give back what the list lacks, through MaxHeight, which a theme's Height never meets.
void FitFoldedList(WindowContext& context) {
    controls::NavigationView navigation = context.sidebarNavigation.get();
    if (!navigation || !context.sidebarHost) return;
    controls::ScrollViewer scroll = navigation.IsPaneOpen() ? nullptr : AppleItemsScroll(context);
    std::vector<PlacedControl*> entries = ListEntries(context);
    double each = 0;
    if (scroll) {
        // The content's own height: the extent stretches to the viewport, and would never show a spare.
        auto content = scroll.Content().try_as<xaml::UIElement>();
        const double room = scroll.ViewportHeight() - (content ? content.DesiredSize().Height : 0);
        each = ampify::EntryCutEach(context.sidebarEntryCut * entries.size(), std::max(0.0, -room), std::max(0.0, room),
                                    static_cast<int>(entries.size()), kMaxEntryCut);
    }
    context.sidebarEntryCut = each;
    for (PlacedControl* placed : entries) {
        const double height = placed->button.Height();
        const double full = std::isfinite(height) ? height : kEntryHeight;
        const double wanted = each > 0 ? full - each : std::numeric_limits<double>::infinity();
        if (placed->button.MaxHeight() != wanted) placed->button.MaxHeight(wanted);
    }
}

// The pane opened, folded or changed mode: every entry of the list follows it at once, not at the next walk.
void RefreshSidebarEntries(WindowContext& context) {
    controls::NavigationView navigation = context.sidebarNavigation.get();
    if (!navigation || !context.sidebarHost) return;
    FitFoldedList(context);
    // context.placed also holds the buttons of the top bar and the playback bar: only the list's own entries follow.
    for (auto& item : context.placed) {
        PlacedControl& placed = item.second;
        if (!placed.button || placed.inHeader || placed.button.Parent() != context.sidebarHost) continue;
        LayoutSidebarEntry(placed, navigation, SidebarEntryIsOn(context, placed.entry), EntryShift(context, navigation));
    }
}

// Ampify's own entry moves between the list and the header as the pane opens or folds (ampify.entry = header). Done in
// the pane's own callback, before the next frame: left to the next walk of the tree, the entry stood in the list of the
// open pane for a moment, then showed in the header late.
void MoveHeaderEntries(UiService::Shared& shared, const std::shared_ptr<WindowContext>& context) {
    if (!shared.entryInHeader) return;
    for (auto& item : context->placed) {
        PlacedControl& placed = item.second;
        if (!placed.button || placed.entry.placement != "sidebar" || !IsFirstParty(placed.entry.elementId)) continue;
        const UiControl control = placed.entry;  // PlaceSidebarItem writes placed.entry again
        PlaceSidebarItem(shared, context, placed, control);
    }
}

// Watches the pane's state of this NavigationView. The callbacks hold the context weakly and lock it when they run.
void FollowSidebarPane(const WeakShared& weakShared, const std::shared_ptr<WindowContext>& context,
                       const controls::NavigationView& navigation) {
    StopFollowingSidebarPane(*context);
    WeakContext weakContext = context;
    auto changed = [weakShared, weakContext](xaml::DependencyObject const&, xaml::DependencyProperty const&) noexcept {
        try {
            auto live = weakContext.lock();
            if (!live) return;
            if (SharedPtr shared = weakShared.lock()) MoveHeaderEntries(*shared, live);
            RefreshSidebarEntries(*live);
        } catch (...) {
            // A handler runs inside Apple Music's XAML frame: nothing may escape it.
        }
    };
    context->sidebarNavigation = winrt::make_weak(navigation);
    if (auto scroll = AppleItemsScroll(*context)) {
        context->sidebarScroll = winrt::make_weak(scroll);
        context->sidebarScrollSize = scroll.SizeChanged([weakContext](IInspectable const&, xaml::SizeChangedEventArgs const&) noexcept {
            try {
                if (auto live = weakContext.lock()) FitFoldedList(*live);
            } catch (...) {
                // A handler runs inside Apple Music's XAML frame: nothing may escape it.
            }
        });
    }
    context->sidebarPaneToken =
        navigation.RegisterPropertyChangedCallback(controls::NavigationView::IsPaneOpenProperty(), changed);
    context->sidebarModeToken =
        navigation.RegisterPropertyChangedCallback(controls::NavigationView::DisplayModeProperty(), changed);
}

}  // namespace

namespace {

// An entry that stays where it is: a header entry changes its icon, a list entry follows the pane (LayoutSidebarEntry).
void UpdatePlacedEntry(WindowContext& context, PlacedControl& placed, const UiControl& control,
                       const controls::NavigationView& navigation, const controls::Grid& header, bool active) {
    if (!header) {
        placed.entry = control;
        MeasureIconShift(context, placed, navigation);
        LayoutSidebarEntry(placed, navigation, active, EntryShift(context, navigation));
        FitFoldedList(context);
        return;
    }
    KeepBesideMore(header, placed.button);
    if (placed.active == active && !std::exchange(placed.restain, false)) return;
    placed.active = active;
    placed.button.Content(SidebarIcon(control, active));
    automation::AutomationProperties::SetItemStatus(placed.button, active ? L"on" : L"");
}

// Once a list entry is laid out for a new pane state (its size changes as the pane opens or folds), the shift is
// measured again and the entry moved at once: waiting for the next walk left the first opens a pixel off.
void RemeasureOnResize(const std::shared_ptr<WindowContext>& context, const controls::Button& button) {
    WeakContext weakContext = context;
    button.SizeChanged([weakContext](IInspectable const& sender, xaml::SizeChangedEventArgs const&) noexcept {
        try {
            auto live = weakContext.lock();
            controls::NavigationView navigation = live ? live->sidebarNavigation.get() : nullptr;
            if (!navigation) return;
            for (auto& item : live->placed) {
                PlacedControl& placed = item.second;
                if (!placed.button || placed.button != sender || placed.inHeader) continue;
                MeasureIconShift(*live, placed, navigation);
                LayoutSidebarEntry(placed, navigation, placed.active, EntryShift(*live, navigation));
            }
        } catch (...) {
            // A handler runs inside Apple Music's XAML frame: nothing may escape it.
        }
    });
}

// A click on the entry goes to the element that added it, as an event of the control.
void WireSidebarClick(const controls::Button& button, const UiService::Shared& shared, const UiControl& control) {
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
}

}  // namespace

void PlaceSidebarItem(UiService::Shared& shared, const std::shared_ptr<WindowContext>& context, PlacedControl& placed,
                      const UiControl& control) {
    controls::NavigationView navigation = FindNavigation(*context);
    if (!navigation) {
        if (!placed.reported) {
            placed.reported = true;
            shared.journal.Write("Interface: sidebar not found for the entry \"" + control.label + "\" of " +
                                 control.elementId);
        }
        return;
    }
    controls::Grid header = EntryHeader(shared, navigation, control);
    const bool inHeader = static_cast<bool>(header);
    controls::StackPanel host = inHeader ? nullptr : EnsureSidebarHost(*context, navigation);
    controls::UIElementCollection place = inHeader ? header.Children() : host.Children();
    const bool active = SidebarEntryIsOn(*context, control);
    // Followed in the header too: the pane folding takes the entry back to the list at once.
    if (context->sidebarNavigation.get() != navigation) FollowSidebarPane(shared.weak_from_this(), context, navigation);
    placed.entry = control;
    std::uint32_t index = 0;
    if (placed.button && placed.inHeader == inHeader && place.IndexOf(placed.button, index)) {
        UpdatePlacedEntry(*context, placed, control, navigation, header, active);  // in place: its state may change
        return;
    }

    RemoveFromParent(placed.button);  // from the list to the header or back, as the sidebar folds or opens
    controls::Button button = inHeader ? MakeHeaderButton(header, control, active) : MakeSidebarButton(active);
    placed.active = active;
    placed.inHeader = inHeader;
    placed.laidOut = false;
    if (!inHeader) {
        placed.button = button;
        LayoutSidebarEntry(placed, navigation, active, EntryShift(*context, navigation));  // before the entry shows
        RemeasureOnResize(context, button);
    }
    Name(button, uirender::ElementName(control.elementId, "control-" + std::to_string(control.id)), control.label);
    WireSidebarClick(button, shared, control);
    // After Apple Music's separator, so the entries sit with the account, below the line that ends the list, in the
    // order of the elements (an entry back from the header takes its place again).
    if (inHeader) {
        place.Append(button);
    } else {
        place.InsertAt(StableIndex(context->placed, host, std::nullopt, control.elementId, control.id), button);
    }
    placed.button = button;
    placed.elementId = control.elementId;
    placed.id = control.id;
    shared.journal.Write("Interface: sidebar entry \"" + control.label + "\" of " + control.elementId + " placed" +
                         (inHeader ? " in the sidebar's header" : ""));
}

namespace {

constexpr double kFoldedPaneWidth = 100;  // the folded column is 48 wide, the open sidebar 290

// Folded, Apple Music moves the sidebar's header right when the back arrow shows, for the arrow and the logo to sit side
// by side as in the open sidebar: the column cut the logo off but for 3 pixels, a sliver at its edge. The logo goes
// while the column is folded and the arrow shows, and comes back when either changes. An arrow the top bar holds
// (opacity 0) leaves the header in place (topbar_search.cpp), and the logo with it.
void ShowFoldedLogo(const WindowContext& context) {
    xaml::FrameworkElement back = context.foldBack.get();
    xaml::FrameworkElement logo = context.foldLogo.get();
    xaml::FrameworkElement pane = context.foldPane.get();
    if (!back || !logo || !pane) return;
    bool arrow = back.Visibility() == xaml::Visibility::Visible && back.Opacity() > 0;
    bool pushedOut = arrow && pane.ActualWidth() < kFoldedPaneWidth;
    double opacity = pushedOut ? 0 : 1;
    if (logo.Opacity() != opacity) logo.Opacity(opacity);
}

}  // namespace

void FollowFoldedLogo(UiService::Shared& shared, const std::shared_ptr<WindowContext>& context) {
    const std::vector<ProbeNode>& nodes = context->probe;
    std::optional<int> back = uirender::FindByName(nodes, "NavigationViewBackButton");
    std::optional<int> header = uirender::FindByName(nodes, "PaneHeaderContentBorder");
    std::optional<int> pane = uirender::FindByName(nodes, "PaneContentGrid");
    if (!back || !header || !pane) return;
    std::optional<int> logo;
    for (int node = *header + 1; node < static_cast<int>(nodes.size()); node++) {
        if (nodes[node].className == "Microsoft.UI.Xaml.Controls.Image" && uirender::IsUnder(nodes, node, *header)) {
            logo = node;
            break;
        }
    }
    if (!logo) return;
    auto element = [&](int node) { return context->probeObjects[node].get().try_as<xaml::FrameworkElement>(); };
    xaml::FrameworkElement backElement = element(*back);
    xaml::FrameworkElement paneElement = element(*pane);
    xaml::FrameworkElement logoElement = element(*logo);
    if (!backElement || !paneElement || !logoElement || context->foldBack.get() == backElement) return;
    context->foldBack = winrt::make_weak(backElement);
    context->foldLogo = winrt::make_weak(logoElement);
    context->foldPane = winrt::make_weak(paneElement);
    WeakContext weakContext = context;
    auto update = [weakContext]() noexcept {
        try {
            if (auto live = weakContext.lock()) ShowFoldedLogo(*live);
        } catch (...) {
            // A handler runs inside Apple Music's XAML frame: nothing may escape it.
        }
    };
    for (const xaml::DependencyProperty& property :
         {xaml::UIElement::VisibilityProperty(), xaml::UIElement::OpacityProperty()}) {
        backElement.RegisterPropertyChangedCallback(
            property, [update](xaml::DependencyObject const&, xaml::DependencyProperty const&) noexcept { update(); });
    }
    paneElement.SizeChanged([update](IInspectable const&, xaml::SizeChangedEventArgs const&) noexcept { update(); });
    ShowFoldedLogo(*context);
    shared.Once("folded-logo", "Interface: the folded sidebar's logo follows its back arrow");
}

}  // namespace ampify::win::ui_service_detail

#endif  // AMPIFY_WITH_XAML
