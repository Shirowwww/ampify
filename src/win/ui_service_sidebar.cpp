// The extensions' entries in Apple Music's sidebar (its footer, or its header for the Marketplace), and the
// logo of the folded sidebar.

#if defined(AMPIFY_WITH_XAML)

#include "ui_service_detail.hpp"

#include "../core/sidebar_entry.hpp"

#include <limits>

namespace ampify::win::ui_service_detail {

namespace {

// ---- Buttons of the top bar and the play bar ----------------------------------------------------------------------

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
    context.sidebarNavigation = winrt::weak_ref<controls::NavigationView>{nullptr};
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
    StopFollowingSidebarPane(context);
    context.sidebarHost = nullptr;
    context.sidebarOriginal = nullptr;
}

namespace {

// The inside of a sidebar entry: the icon and the label, and a bar at the left edge like Apple Music's own selected item.
// An entry that is on takes the accent colour. The icon is drawn again with the colour, so a state change rebuilds it.
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
        L"HorizontalContentAlignment='Left' MinWidth='0' Height='36' Margin='4,2,4,2' Padding='12,0,12,0' CornerRadius='4' "
        L"BorderThickness='0'><Button.Resources><ResourceDictionary><ResourceDictionary.ThemeDictionaries>"
        L"<ResourceDictionary x:Key='Default'>" + brushes + L"</ResourceDictionary>"
        L"<ResourceDictionary x:Key='Light'>" + brushes + L"</ResourceDictionary>"
        L"</ResourceDictionary.ThemeDictionaries></ResourceDictionary></Button.Resources></Button>";
    controls::Button button = xaml::Markup::XamlReader::Load(text).as<controls::Button>();
    automation::AutomationProperties::SetItemStatus(button, active ? L"on" : L"");
    return button;
}

constexpr char kMarketplaceId[] = "marketplace";
constexpr double kMoreButtonWidth = 32;  // Apple's "···" before its first layout
constexpr double kHeaderButtonHeight = 28;  // the height of "···"
constexpr double kHeaderSpacing = 2;

// The sidebar's header (Apple's logo, its name and "···"), where Ampify's own entry stands with ampify.entry = header.
// Nothing while the sidebar is folded: Apple hides the header then, and the entry stays in the list.
controls::Grid EntryHeader(const UiService::Shared& shared, const controls::NavigationView& navigation,
                           const UiControl& control) {
    if (control.elementId != kMarketplaceId || !shared.entryInHeader || !navigation.IsPaneOpen()) return nullptr;
    return navigation.PaneHeader().try_as<controls::Grid>();
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
    double right = kMoreButtonWidth + kHeaderSpacing;
    for (const xaml::UIElement& child : header.Children()) {
        auto more = child.try_as<xaml::FrameworkElement>();
        if (!more || more.Name() != L"MoreButton") continue;
        right = (more.ActualWidth() > 0 ? more.ActualWidth() : kMoreButtonWidth) + more.Margin().Right + kHeaderSpacing;
        controls::Grid::SetColumn(button, controls::Grid::GetColumn(more));
        controls::Grid::SetRow(button, controls::Grid::GetRow(more));
    }
    button.Margin(xaml::Thickness{0, 0, right, 0});
    controls::ToolTipService::SetToolTip(button, winrt::box_value(Wide(control.label)));
    return button;
}

// An entry is on when its element asked for it (Ampify.ui.setActive), or while the page of that element is on screen.
bool SidebarEntryIsOn(const WindowContext& context, const UiControl& control) {
    return control.active || (context.route && !context.pageClosed && context.route->first == control.elementId);
}

// Folded: the icon alone, centred; the label is the tooltip. Open: the row of SidebarContent.
xaml::FrameworkElement SidebarCompactContent(const UiControl& control, bool active) {
    xaml::FrameworkElement icon = SidebarIcon(control, active);
    icon.HorizontalAlignment(xaml::HorizontalAlignment::Center);
    return icon;
}

const double kAutoWidth = std::numeric_limits<double>::quiet_NaN();  // open: the row fills the sidebar

// The open button's padding is the markup's; folded, the icon gets the whole width (the column is narrow).
void LayoutOpenEntry(PlacedControl& placed, bool active) {
    placed.button.Width(kAutoWidth);
    placed.button.HorizontalAlignment(xaml::HorizontalAlignment::Stretch);
    placed.button.HorizontalContentAlignment(xaml::HorizontalAlignment::Left);
    placed.button.Padding(xaml::Thickness{12, 0, 12, 0});
    placed.button.Content(SidebarContent(placed.entry, active));
    controls::ToolTipService::SetToolTip(placed.button, nullptr);
}

void LayoutFoldedEntry(PlacedControl& placed, double width, bool active) {
    placed.button.Width(width);
    placed.button.HorizontalAlignment(xaml::HorizontalAlignment::Center);
    placed.button.HorizontalContentAlignment(xaml::HorizontalAlignment::Center);
    placed.button.Padding(xaml::Thickness{});
    placed.button.Content(SidebarCompactContent(placed.entry, active));
    controls::ToolTipService::SetToolTip(placed.button, winrt::box_value(Wide(placed.entry.label)));
}

// Lays an entry out for the pane's state. Writes only what the button does not show yet, so a width a theme wrote on
// it is put back. Margin, height and colours stay the theme's (names.md). Centred, so the entry sits in the column
// whichever way the sidebar flows.
void LayoutSidebarEntry(PlacedControl& placed, const controls::NavigationView& navigation, bool active) {
    const bool paneOpen = navigation.IsPaneOpen();
    const xaml::Thickness margin = placed.button.Margin();
    const double width =
        paneOpen ? kAutoWidth : ampify::FoldedEntryWidth(navigation.CompactPaneLength(), margin.Left, margin.Right);
    const ampify::EntryLayout shown{placed.paneOpen, placed.active, placed.button.Width()};
    if (placed.laidOut && !ampify::EntryLayoutChanged(shown, ampify::EntryLayout{paneOpen, active, width})) return;
    placed.laidOut = true;
    placed.paneOpen = paneOpen;
    placed.active = active;
    if (paneOpen) {
        LayoutOpenEntry(placed, active);
    } else {
        LayoutFoldedEntry(placed, width, active);
    }
    automation::AutomationProperties::SetItemStatus(placed.button, active ? L"on" : L"");
}

// The pane opened, folded or changed mode: every entry of the list follows it at once, not at the next walk.
void RefreshSidebarEntries(WindowContext& context) {
    controls::NavigationView navigation = context.sidebarNavigation.get();
    if (!navigation) return;
    for (auto& item : context.placed) {
        PlacedControl& placed = item.second;
        if (!placed.button || placed.inHeader || !placed.button.Parent()) continue;
        LayoutSidebarEntry(placed, navigation, SidebarEntryIsOn(context, placed.entry));
    }
}

// Watches the pane's state of this NavigationView. The callbacks hold the context weakly and lock it when they run.
void FollowSidebarPane(const std::shared_ptr<WindowContext>& context, const controls::NavigationView& navigation) {
    StopFollowingSidebarPane(*context);
    WeakContext weakContext = context;
    auto changed = [weakContext](xaml::DependencyObject const&, xaml::DependencyProperty const&) noexcept {
        try {
            if (auto live = weakContext.lock()) RefreshSidebarEntries(*live);
        } catch (...) {
        }
    };
    context->sidebarNavigation = winrt::make_weak(navigation);
    context->sidebarPaneToken =
        navigation.RegisterPropertyChangedCallback(controls::NavigationView::IsPaneOpenProperty(), changed);
    context->sidebarModeToken =
        navigation.RegisterPropertyChangedCallback(controls::NavigationView::DisplayModeProperty(), changed);
}

}  // namespace

namespace {

// An entry that stays where it is: a header entry changes its icon, a list entry follows the pane (LayoutSidebarEntry).
void UpdatePlacedEntry(PlacedControl& placed, const UiControl& control, const controls::NavigationView& navigation,
                       bool inHeader, bool active) {
    if (!inHeader) {
        placed.entry = control;
        LayoutSidebarEntry(placed, navigation, active);
        return;
    }
    if (placed.active == active) return;
    placed.active = active;
    placed.button.Content(SidebarIcon(control, active));
    automation::AutomationProperties::SetItemStatus(placed.button, active ? L"on" : L"");
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
    if (!inHeader && context->sidebarNavigation.get() != navigation) FollowSidebarPane(context, navigation);
    std::uint32_t index = 0;
    if (placed.button && placed.inHeader == inHeader && place.IndexOf(placed.button, index)) {
        UpdatePlacedEntry(placed, control, navigation, inHeader, active);  // still in place: only its state may change
        return;
    }

    RemoveFromParent(placed.button);  // from the list to the header or back, as the sidebar folds or opens
    controls::Button button = inHeader ? MakeHeaderButton(header, control, active) : MakeSidebarButton(active);
    placed.active = active;
    placed.inHeader = inHeader;
    placed.laidOut = false;
    if (!inHeader) {
        placed.button = button;
        placed.entry = control;
        LayoutSidebarEntry(placed, navigation, active);  // the pane's state, before the entry shows
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
// while the column is folded and the arrow shows, and comes back when either changes.
void ShowFoldedLogo(const WindowContext& context) {
    xaml::FrameworkElement back = context.foldBack.get();
    xaml::FrameworkElement logo = context.foldLogo.get();
    xaml::FrameworkElement pane = context.foldPane.get();
    if (!back || !logo || !pane) return;
    bool pushedOut = back.Visibility() == xaml::Visibility::Visible && pane.ActualWidth() < kFoldedPaneWidth;
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
        }
    };
    backElement.RegisterPropertyChangedCallback(
        xaml::UIElement::VisibilityProperty(),
        [update](xaml::DependencyObject const&, xaml::DependencyProperty const&) noexcept { update(); });
    paneElement.SizeChanged([update](IInspectable const&, xaml::SizeChangedEventArgs const&) noexcept { update(); });
    ShowFoldedLogo(*context);
    shared.Once("folded-logo", "Interface: the folded sidebar's logo follows its back arrow");
}

}  // namespace ampify::win::ui_service_detail

#endif  // AMPIFY_WITH_XAML
