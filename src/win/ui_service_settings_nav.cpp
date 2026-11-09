// The tabs, the search and the folds of Ampify's settings page (SettingsNav): one tab shows one pane, the search looks
// through all of them, and each element's own settings stay folded under its name until opened.

#if defined(AMPIFY_WITH_XAML)

#include "ui_service_detail.hpp"

#include "ui_motion.hpp"

namespace ampify::win::ui_service_detail {

namespace {

constexpr double kIndicatorWidth = 100;  // the indicator's own width, stretched by its scale to the tab's
constexpr double kIndicatorHeight = 3;
constexpr double kIndicatorInset = 10;   // the indicator is shorter than its tab, centred under its text
constexpr double kIdleTabOpacity = 0.65;
constexpr double kPaneSpacing = 24;

xaml::Visibility Show(bool shown) {
    return shown ? xaml::Visibility::Visible : xaml::Visibility::Collapsed;
}

media::SolidColorBrush Clear() {
    return media::SolidColorBrush(winrt::Windows::UI::Color{0, 0, 0, 0});
}

bool Matches(const settings_panel::SettingText& text, const std::string& query) {
    if (text.label.empty() && text.description.empty()) return false;
    return !settings_panel::MatchingSettings({text}, query).empty();
}

bool AnyMatches(const std::vector<settings_panel::SettingText>& texts, const std::string& query) {
    return !settings_panel::MatchingSettings(texts, query).empty();
}

// The Expander is WinUI's own (its slide and chevron come with it, and themes colour it through its Expander*
// brushes); inside a card it drops its own card look and lines up with the rows.
void FlattenFold(const controls::Expander& fold) {
    auto resources = fold.Resources();
    for (const wchar_t* key : {L"ExpanderHeaderBackground", L"ExpanderContentBackground", L"ExpanderHeaderBorderBrush",
                               L"ExpanderContentBorderBrush"}) {
        resources.Insert(winrt::box_value(key), Clear());
    }
    resources.Insert(winrt::box_value(L"ExpanderHeaderBorderThickness"), winrt::box_value(xaml::Thickness{0, 0, 0, 0}));
    resources.Insert(winrt::box_value(L"ExpanderContentDownBorderThickness"), winrt::box_value(xaml::Thickness{0, 0, 0, 0}));
    resources.Insert(winrt::box_value(L"ExpanderHeaderPadding"), winrt::box_value(xaml::Thickness{0, 0, 0, 0}));
    resources.Insert(winrt::box_value(L"ExpanderContentPadding"), winrt::box_value(xaml::Thickness{0, 4, 0, 8}));
}

std::string CountText(std::size_t count) {
    return count == 1 ? i18n::Text("settings.countOne")
                      : i18n::Text("settings.countMany", {{"count", std::to_string(count)}});
}

// The fold's header: the element's name and kind, its number of settings on the right; or only that number when the
// row above already names the element (an extension).
xaml::FrameworkElement FoldHeader(UiService::Shared& shared, const uirender::ModEntry& mod, bool named) {
    controls::Grid header;
    header.ColumnSpacing(16);
    controls::ColumnDefinition text;
    text.Width(xaml::GridLength{1.0, xaml::GridUnitType::Star});
    controls::ColumnDefinition count;
    count.Width(xaml::GridLength{1.0, xaml::GridUnitType::Auto});
    header.ColumnDefinitions().Append(text);
    header.ColumnDefinitions().Append(count);
    controls::StackPanel titles;
    titles.VerticalAlignment(xaml::VerticalAlignment::Center);
    titles.Padding(xaml::Thickness{0, 8, 0, 8});
    if (named) {
        titles.Children().Append(MakeText(Wide(mod.NameIn(i18n::Language())).c_str(), "body", false, &shared.probe));
        titles.Children().Append(MakeText(
            Wide(i18n::Text(mod.kind == ElementKind::Snippet ? "mods.kindSnippet" : "mods.kindTheme")).c_str(), "caption",
            true, &shared.probe));
        controls::TextBlock number = MakeText(Wide(CountText(mod.settings.size())).c_str(), "caption", true, &shared.probe);
        number.VerticalAlignment(xaml::VerticalAlignment::Center);
        controls::Grid::SetColumn(number, 1);
        header.Children().Append(number);
    } else {
        titles.Children().Append(MakeText(Wide(CountText(mod.settings.size())).c_str(), "caption", true, &shared.probe));
    }
    header.Children().InsertAt(0, titles);
    return header;
}

void PlaceIndicator(SettingsNav& nav, std::size_t tab, bool animate) {
    if (tab >= nav.panes.size() || !nav.tabRow || !nav.indicator) return;
    controls::Button button = nav.panes[tab].tab;
    double width = button.ActualWidth();
    if (width <= 0) return;  // not laid out yet: SizeChanged places it
    auto at = button.TransformToVisual(nav.tabRow).TransformPoint({0, 0});
    double length = std::max(width - 2 * kIndicatorInset, 8.0);
    motion::Glide(nav.indicator, static_cast<float>(at.X + (width - length) / 2),
                  static_cast<float>(length / kIndicatorWidth), animate && nav.placed);
    nav.placed = true;
}

void SelectTab(UiService::Shared& shared, SettingsNav& nav, std::size_t tab) {
    if (tab >= nav.panes.size()) return;
    shared.settingsTab = tab;
    if (!shared.settingsQuery.empty() && nav.search) {
        nav.search.Text(L"");  // TextChanged empties the query and shows the tab
        return;
    }
    ShowSettingsState(shared, nav, true);
}

controls::Button TabButton(UiService::Shared& shared, const std::weak_ptr<SettingsNav>& weak, std::size_t index,
                           const std::string& title, controls::TextBlock& label) {
    controls::Button tab;
    tab.Background(Clear());
    tab.BorderThickness(xaml::Thickness{0, 0, 0, 0});
    tab.Padding(xaml::Thickness{12, 8, 12, 10});
    label = MakeText(Wide(title).c_str(), "body", false, &shared.probe);
    tab.Content(label);
    Name(tab, "AMPIFY_SettingsTab_" + std::to_string(index), title);
    UiService::Shared* owner = &shared;
    tab.Click([owner, weak, index](IInspectable const&, xaml::RoutedEventArgs const&) noexcept {
        try {
            if (auto nav = weak.lock()) SelectTab(*owner, *nav, index);
        } catch (...) {
            // A handler runs inside Apple Music's XAML frame: nothing may escape it.
        }
    });
    return tab;
}

// Left and Right move between the tabs, as in Windows' own tab rows.
void FollowArrowKeys(UiService::Shared& shared, const controls::StackPanel& buttons, const std::weak_ptr<SettingsNav>& weak) {
    UiService::Shared* owner = &shared;
    buttons.KeyDown([owner, weak](IInspectable const&, xaml::Input::KeyRoutedEventArgs const& args) noexcept {
        try {
            auto nav = weak.lock();
            if (!nav || nav->panes.empty()) return;
            using winrt::Windows::System::VirtualKey;
            std::size_t count = nav->panes.size();
            std::size_t next = owner->settingsTab;
            if (args.Key() == VirtualKey::Right) next = (next + 1) % count;
            else if (args.Key() == VirtualKey::Left) next = (next + count - 1) % count;
            else return;
            args.Handled(true);
            SelectTab(*owner, *nav, next);
            nav->panes[next].tab.Focus(xaml::FocusState::Keyboard);
        } catch (...) {
            // A handler runs inside Apple Music's XAML frame: nothing may escape it.
        }
    });
}

controls::TextBox SearchField(UiService::Shared& shared, const std::weak_ptr<SettingsNav>& weak) {
    controls::TextBox search;
    if (shared.probe.textBox) search.Style(shared.probe.textBox);
    search.PlaceholderText(Wide(i18n::Text("settings.search")));
    search.HorizontalAlignment(xaml::HorizontalAlignment::Stretch);
    Name(search, "AMPIFY_SettingsSearch", i18n::Text("settings.search"));
    search.Text(Wide(shared.settingsQuery));
    UiService::Shared* owner = &shared;
    search.TextChanged([owner, weak](IInspectable const& sender, controls::TextChangedEventArgs const&) noexcept {
        try {
            std::string query = Utf8(sender.as<controls::TextBox>().Text());
            if (query == owner->settingsQuery) return;
            owner->settingsQuery = query;
            if (auto nav = weak.lock()) ShowSettingsState(*owner, *nav, true);
        } catch (...) {
            // A handler runs inside Apple Music's XAML frame: nothing may escape it.
        }
    });
    search.KeyDown([](IInspectable const& sender, xaml::Input::KeyRoutedEventArgs const& args) noexcept {
        try {
            if (args.Key() != winrt::Windows::System::VirtualKey::Escape) return;
            auto box = sender.as<controls::TextBox>();
            if (box.Text().empty()) return;
            box.Text(L"");
            args.Handled(true);
        } catch (...) {
            // A handler runs inside Apple Music's XAML frame: nothing may escape it.
        }
    });
    return search;
}

// What one row shows: everything while browsing (a row of folds only when it has some); while searching, the row when
// its own text matches, else only its folds that match, opened.
bool ShowRow(UiService::Shared& shared, SettingsNav::Row& row, const std::string& query) {
    bool searching = !query.empty();
    bool own = searching ? Matches(row.text, query) : !row.text.label.empty();
    bool any = own;
    for (SettingsFold& fold : row.folds) {
        bool open = shared.openFolds.contains(fold.key);
        bool shown = !searching || own || AnyMatches(fold.texts, query);
        if (searching && !own && shown) open = true;
        fold.expander.Visibility(Show(shown));
        if (fold.expander.IsExpanded() != open) fold.expander.IsExpanded(open);
        any = any || shown;
    }
    row.unit.Visibility(Show(any));
    return any;
}

}  // namespace

controls::StackPanel AddSettingsPane(UiService::Shared&, SettingsNav& nav, const std::string& tabKey) {
    SettingsNav::Pane pane;
    pane.panel = controls::StackPanel();
    pane.panel.Spacing(kPaneSpacing);
    const std::string title = i18n::Text(tabKey);
    controls::TextBlock heading = MakeText(Wide(title).c_str(), "subtitle", false, nullptr);
    automation::AutomationProperties::SetHeadingLevel(heading, automation::Peers::AutomationHeadingLevel::Level2);
    pane.heading = heading;
    pane.panel.Children().Append(heading);
    nav.panes.push_back(pane);
    return pane.panel;
}

// A heading (none for a pane's first, unnamed group), a line on what it holds, then one card whose rows are split by
// lines, as Apple groups its own settings.
controls::StackPanel AddSettingsGroup(UiService::Shared& shared, SettingsNav& nav, const std::string& name,
                                      const std::string& titleKey, const std::string& subtitleKey) {
    controls::StackPanel whole;
    whole.Spacing(4);
    Name(whole, "AMPIFY_Settings_" + name, titleKey.empty() ? name : i18n::Text(titleKey));
    if (!titleKey.empty()) {
        controls::TextBlock heading = MakeText(Wide(i18n::Text(titleKey)).c_str(), "bodyStrong", false, &shared.probe);
        automation::AutomationProperties::SetHeadingLevel(heading, automation::Peers::AutomationHeadingLevel::Level3);
        whole.Children().Append(heading);
    }
    if (!subtitleKey.empty()) {
        whole.Children().Append(MakeText(Wide(i18n::Text(subtitleKey)).c_str(), "caption", true, &shared.probe));
    }
    controls::Border card = MakeCardBorder(shared.probe);
    card.Margin(xaml::Thickness{0, titleKey.empty() && subtitleKey.empty() ? 0.0 : 6.0, 0, 0});
    controls::StackPanel rows;
    rows.Spacing(12);
    card.Child(rows);
    whole.Children().Append(card);
    nav.panes.back().panel.Children().Append(whole);
    nav.groups.push_back({whole, nav.panes.size() - 1, {}});
    return rows;
}

std::size_t AddSettingsRow(SettingsNav& nav, const controls::StackPanel& rows, const xaml::FrameworkElement& row,
                           settings_panel::SettingText text) {
    controls::StackPanel unit;
    unit.Spacing(12);
    xaml::FrameworkElement divider{nullptr};
    if (rows.Children().Size() > 0) {
        divider = MakeDivider();
        unit.Children().Append(divider);
    }
    unit.Children().Append(row);
    rows.Children().Append(unit);
    nav.rows.push_back({unit, divider, std::move(text), {}});
    nav.groups.back().rows.push_back(nav.rows.size() - 1);
    return nav.rows.size() - 1;
}

controls::Expander AddSettingsFold(UiService::Shared& shared, const std::shared_ptr<SettingsNav>& nav, std::size_t row,
                                   const uirender::ModEntry& mod, bool named) {
    controls::Expander fold;
    FlattenFold(fold);
    fold.HorizontalAlignment(xaml::HorizontalAlignment::Stretch);
    fold.HorizontalContentAlignment(xaml::HorizontalAlignment::Stretch);
    std::string key = std::string(ToString(mod.kind)) + ":" + mod.id;
    Name(fold, "AMPIFY_SettingsFold_" + mod.id, mod.NameIn(i18n::Language()));
    fold.Header(FoldHeader(shared, mod, named));
    fold.Content(ModSettings(shared, mod));
    fold.IsExpanded(shared.openFolds.contains(key));
    UiService::Shared* owner = &shared;
    std::weak_ptr<SettingsNav> weak = nav;
    fold.Expanding([owner, weak, key](controls::Expander const&, controls::ExpanderExpandingEventArgs const&) noexcept {
        try {
            if (auto live = weak.lock(); live && !live->searching) owner->openFolds.insert(key);
        } catch (...) {
            // A handler runs inside Apple Music's XAML frame: nothing may escape it.
        }
    });
    fold.Collapsed([owner, weak, key](controls::Expander const&, controls::ExpanderCollapsedEventArgs const&) noexcept {
        try {
            if (auto live = weak.lock(); live && !live->searching) owner->openFolds.erase(key);
        } catch (...) {
            // A handler runs inside Apple Music's XAML frame: nothing may escape it.
        }
    });
    std::vector<settings_panel::SettingText> texts{{mod.NameIn(i18n::Language()), mod.DescriptionIn(i18n::Language())}};
    for (settings_panel::SettingText& text : ElementSettingTexts(mod)) texts.push_back(std::move(text));
    if (row < nav->rows.size()) nav->rows[row].folds.push_back({fold, std::move(key), std::move(texts)});
    return fold;
}

// The search over the whole page, then one tab per pane with the accent's line under the one shown.
xaml::FrameworkElement SettingsNavBar(UiService::Shared& shared, const std::shared_ptr<SettingsNav>& nav) {
    std::weak_ptr<SettingsNav> weak = nav;
    controls::StackPanel bar;
    bar.Spacing(12);
    nav->search = SearchField(shared, weak);
    bar.Children().Append(nav->search);

    controls::Grid tabs;
    controls::StackPanel buttons;
    buttons.Orientation(controls::Orientation::Horizontal);
    buttons.Spacing(2);
    Name(buttons, "AMPIFY_SettingsTabs");
    for (std::size_t index = 0; index < nav->panes.size(); index++) {
        SettingsNav::Pane& pane = nav->panes[index];
        std::string title = Utf8(pane.heading.as<controls::TextBlock>().Text());
        pane.tab = TabButton(shared, weak, index, title, pane.label);
        buttons.Children().Append(pane.tab);
    }
    FollowArrowKeys(shared, buttons, weak);
    tabs.Children().Append(buttons);

    xaml::FrameworkElement line = MakeDivider();
    line.VerticalAlignment(xaml::VerticalAlignment::Bottom);
    tabs.Children().Append(line);
    // The accent through {ThemeResource}: themes and the accent chosen over them replace that resource, and the line
    // follows them, in light and dark mode alike.
    nav->indicator = LoadKitXaml(L"<Border " + std::wstring(KitXmlns()) +
                                 L" Background='{ThemeResource AccentFillColorDefaultBrush}'/>")
                         .try_as<controls::Border>();
    if (!nav->indicator) nav->indicator = controls::Border();
    nav->indicator.Width(kIndicatorWidth);
    nav->indicator.Height(kIndicatorHeight);
    nav->indicator.CornerRadius(xaml::CornerRadius{1.5, 1.5, 1.5, 1.5});
    nav->indicator.HorizontalAlignment(xaml::HorizontalAlignment::Left);
    nav->indicator.VerticalAlignment(xaml::VerticalAlignment::Bottom);
    nav->indicator.IsHitTestVisible(false);
    tabs.Children().Append(nav->indicator);
    nav->tabRow = tabs;

    UiService::Shared* owner = &shared;
    tabs.SizeChanged([owner, weak](IInspectable const&, xaml::SizeChangedEventArgs const&) noexcept {
        try {
            if (auto live = weak.lock()) PlaceIndicator(*live, owner->settingsTab, false);
        } catch (...) {
            // A handler runs inside Apple Music's XAML frame: nothing may escape it.
        }
    });
    bar.Children().Append(tabs);
    return bar;
}

xaml::FrameworkElement SettingsPanes(UiService::Shared& shared, const std::shared_ptr<SettingsNav>& nav) {
    nav->host = controls::StackPanel();
    nav->host.Spacing(kPaneSpacing);
    for (const SettingsNav::Pane& pane : nav->panes) nav->host.Children().Append(pane.panel);
    nav->none = MakeText(Wide(i18n::Text("settings.noMatch")).c_str(), "body", true, &shared.probe);
    nav->host.Children().Append(nav->none);
    if (shared.settingsTab >= nav->panes.size()) shared.settingsTab = 0;
    ShowSettingsState(shared, *nav, false);
    return nav->host;
}

// Browsing: the chosen tab's pane, its rows and folds as left. Searching: every pane with a match, under its name, and
// in it the groups and rows that match, the first line of each card hidden so no card starts with a line.
void ShowSettingsState(UiService::Shared& shared, SettingsNav& nav, bool animate) {
    const std::string& query = shared.settingsQuery;
    bool searching = !query.empty();
    nav.searching = true;
    std::vector<bool> paneShown(nav.panes.size(), false);
    bool anything = false;
    for (const SettingsNav::Group& group : nav.groups) {
        bool groupShown = false;
        bool first = true;
        for (std::size_t index : group.rows) {
            SettingsNav::Row& row = nav.rows[index];
            bool shown = ShowRow(shared, row, query);
            if (row.divider) row.divider.Visibility(Show(shown && !first));
            if (shown) first = false;
            groupShown = groupShown || shown;
        }
        group.whole.Visibility(Show(groupShown));
        if (groupShown) paneShown[group.pane] = true;
    }
    nav.searching = false;

    for (std::size_t index = 0; index < nav.panes.size(); index++) {
        SettingsNav::Pane& pane = nav.panes[index];
        bool shown = searching ? paneShown[index] : index == shared.settingsTab;
        pane.panel.Visibility(Show(shown));
        pane.heading.Visibility(Show(searching));
        anything = anything || shown;
        if (pane.label) motion::FadeTo(pane.label, !searching && index == shared.settingsTab ? 1.0 : kIdleTabOpacity);
    }
    if (nav.none) nav.none.Visibility(Show(searching && !anything));
    if (nav.indicator) motion::FadeTo(nav.indicator, searching ? 0.0 : 1.0);
    PlaceIndicator(nav, shared.settingsTab, animate);

    // Only a change of what is shown moves in: a new tab, the search starting or ending. Typing on does not.
    std::size_t shownPane = searching ? nav.panes.size() : shared.settingsTab;
    bool changed = shownPane != nav.shownPane || searching != nav.wasSearching;
    nav.shownPane = shownPane;
    nav.wasSearching = searching;
    if (animate && changed && nav.host) motion::Enter(nav.host);
}

}  // namespace ampify::win::ui_service_detail

#endif  // AMPIFY_WITH_XAML
