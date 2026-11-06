// Ampify's settings page (Settings > Ampify): its sections, the theme and accent choices, the Apple Music
// tweaks, and the Ampify row added at the top of Apple Music's settings.

#if defined(AMPIFY_WITH_XAML)

#include "ui_service_detail.hpp"

#include "../core/kit_layout.hpp"

namespace ampify::win::ui_service_detail {

namespace {

constexpr std::size_t kPageLimit = 4000;

constexpr double kAmpifyPageWidth = 1000;  // as wide as Apple's settings rows

constexpr char kAmpifyPageOwner[] = "ampify";
constexpr char kAtmosLeveler[] = "atmos-leveler";
static_assert(IsBuiltInExtension(kAtmosLeveler), "SoundRows writes the row of an extension built into the kernel");

// ---- The Mods card in Settings > Playback ------------------------------------------------------------------------

// The page's own tree, depth first, parents before children (4000 nodes at most).
void CollectPage(const xaml::DependencyObject& page, std::vector<ProbeNode>& nodes,
                 std::vector<xaml::DependencyObject>& objects) {
    std::vector<std::pair<xaml::DependencyObject, int>> stack{{page, -1}};
    while (!stack.empty() && nodes.size() < kPageLimit) {
        auto [node, parent] = stack.back();
        stack.pop_back();
        int index = static_cast<int>(nodes.size());
        nodes.push_back(Describe(node, parent));
        objects.push_back(node);
        int count = media::VisualTreeHelper::GetChildrenCount(node);
        for (int child = count - 1; child >= 0; child--) stack.push_back({media::VisualTreeHelper::GetChild(node, child), index});
    }
}

// A row of the card: a label and its help on the left, a control on the right.
controls::Grid LabelledRow(UiService::Shared& shared, const std::string& label, const std::string& help,
                           const xaml::FrameworkElement& control) {
    controls::Grid row;
    row.ColumnSpacing(16);
    controls::ColumnDefinition text;
    text.Width(xaml::GridLength{1.0, xaml::GridUnitType::Star});
    controls::ColumnDefinition right;
    right.Width(xaml::GridLength{1.0, xaml::GridUnitType::Auto});
    row.ColumnDefinitions().Append(text);
    row.ColumnDefinitions().Append(right);
    controls::StackPanel titles;
    titles.VerticalAlignment(xaml::VerticalAlignment::Center);
    titles.Children().Append(MakeText(Wide(label).c_str(), "body", false, &shared.probe));
    titles.Children().Append(MakeText(Wide(help).c_str(), "caption", true, &shared.probe));
    row.Children().Append(titles);
    control.VerticalAlignment(xaml::VerticalAlignment::Center);
    controls::Grid::SetColumn(control, 1);
    row.Children().Append(control);
    return row;
}

// Shows the settings of the theme active now under the theme list, then those of the snippets switched on, or nothing
// when none declares any.
void FillThemeSettings(UiService::Shared& shared, const controls::StackPanel& host) {
    host.Children().Clear();
    if (std::optional<uirender::ModEntry> theme = uirender::ActiveThemeSettings(shared.kit)) {
        host.Children().Append(MakeDivider());
        host.Children().Append(ThemeSettingsSection(shared, *theme));
    }
    for (const uirender::ModEntry& snippet : uirender::EnabledSnippetSettings(shared.kit)) {
        host.Children().Append(MakeDivider());
        host.Children().Append(ThemeSettingsSection(shared, snippet));
    }
}

// Writes theme.accent ("" for the theme's own), which the theme engine follows like theme.scheme.
bool CommitAccent(UiService::Shared& shared, const std::string& accent) {
    std::string error;
    if (!uirender::ChooseAccent(shared.kit, accent, error)) {
        shared.journal.Write("Settings: could not change theme.accent: " + error);
        return false;
    }
    std::string written = uirender::ChosenAccent(shared.kit);
    shared.journal.Write("Settings: theme.accent = " + (written.empty() ? std::string("the theme's") : written) +
                         " from the Ampify page");
    return true;
}

// The hex field of a custom accent, written when it is left or on Enter. A colour that does not read puts the last
// one back.
controls::TextBox AccentField(UiService::Shared& shared, const std::string& value) {
    controls::TextBox box;
    if (shared.probe.textBox) box.Style(shared.probe.textBox);
    box.MinWidth(110);
    box.MaxLength(7);
    box.PlaceholderText(L"#RRGGBB");
    box.Text(Wide(value.empty() ? value : "#" + value));
    Name(box, "AMPIFY_AccentCustom", i18n::Text("accent.custom"));
    UiService::Shared* owner = &shared;
    auto commit = [owner](const controls::TextBox& field) {
        // Compared with settings.txt, not the field's last text: a preset may have been chosen since.
        std::string current = uirender::ChosenAccent(owner->kit);
        std::string typed = Utf8(field.Text());
        if (typed.empty() || typed == "#" + current) return;
        if (!CommitAccent(*owner, typed)) field.Text(Wide(current.empty() ? current : "#" + current));
    };
    box.LostFocus([commit](IInspectable const& sender, xaml::RoutedEventArgs const&) noexcept {
        try {
            commit(sender.as<controls::TextBox>());
        } catch (...) {
        }
    });
    box.KeyDown([commit](IInspectable const& sender, xaml::Input::KeyRoutedEventArgs const& args) noexcept {
        try {
            if (args.Key() == winrt::Windows::System::VirtualKey::Enter) commit(sender.as<controls::TextBox>());
        } catch (...) {
        }
    });
    return box;
}

// The accent over the theme's: the theme's own, a preset, or a colour typed in hex ("Custom…" shows the field).
xaml::FrameworkElement AccentChoice(UiService::Shared& shared) {
    std::string chosen = uirender::ChosenAccent(shared.kit);
    controls::ComboBox combo;
    if (shared.probe.comboBox) combo.Style(shared.probe.comboBox);
    combo.MinWidth(220);
    Name(combo, "AMPIFY_AccentChoice", i18n::Text("accent.title"));
    auto values = std::make_shared<std::vector<std::string>>(1, "");  // "" is the theme's own accent
    combo.Items().Append(winrt::box_value(Wide(i18n::Text("accent.theme"))));
    for (const uirender::AccentPreset& preset : uirender::AccentPresets()) {
        values->push_back(preset.value);
        combo.Items().Append(winrt::box_value(Wide(i18n::Text(preset.textKey))));
    }
    values->push_back(uirender::kArtworkAccent);
    combo.Items().Append(winrt::box_value(Wide(i18n::Text("accent.artwork"))));
    combo.Items().Append(winrt::box_value(Wide(i18n::Text("accent.custom"))));
    auto known = std::find(values->begin(), values->end(), chosen);
    int32_t customIndex = static_cast<int32_t>(values->size());
    combo.SelectedIndex(known == values->end() ? customIndex : static_cast<int32_t>(known - values->begin()));

    controls::TextBox custom = AccentField(shared, known == values->end() ? chosen : "");
    custom.Visibility(known == values->end() ? xaml::Visibility::Visible : xaml::Visibility::Collapsed);
    UiService::Shared* owner = &shared;
    combo.SelectionChanged([owner, values, customIndex, custom](IInspectable const& sender,
                                                                controls::SelectionChangedEventArgs const&) noexcept {
        try {
            int32_t index = sender.as<controls::ComboBox>().SelectedIndex();
            if (index < 0 || index > customIndex) return;
            bool typed = index == customIndex;
            custom.Visibility(typed ? xaml::Visibility::Visible : xaml::Visibility::Collapsed);
            if (typed) {
                custom.Focus(xaml::FocusState::Programmatic);
            } else if ((*values)[index] != uirender::ChosenAccent(owner->kit)) {
                CommitAccent(*owner, (*values)[index]);
            }
        } catch (...) {
        }
    });
    controls::StackPanel control;
    control.Orientation(controls::Orientation::Horizontal);
    control.Spacing(8);
    control.Children().Append(custom);
    control.Children().Append(combo);
    return LabelledRow(shared, i18n::Text("accent.title"), i18n::Text("accent.help"), control);
}

// One choice of the card as a menu row, written to settings.txt when it changes (uirender::SetAppleTweak).
xaml::FrameworkElement TweakRow(UiService::Shared& shared, const uirender::TweakChoice& choice) {
    UiService::Shared* owner = &shared;
    controls::ComboBox combo;
    if (shared.probe.comboBox) combo.Style(shared.probe.comboBox);
    combo.MinWidth(220);
    auto values = std::make_shared<std::vector<std::string>>();
    for (const auto& [value, textKey] : choice.options) {
        values->push_back(value);
        combo.Items().Append(winrt::box_value(Wide(i18n::Text(textKey))));
    }
    auto chosen = std::find(values->begin(), values->end(), choice.value);
    combo.SelectedIndex(chosen == values->end() ? 0 : static_cast<int32_t>(chosen - values->begin()));
    Name(combo, "AMPIFY_Tweak_" + choice.key, i18n::Text(choice.labelKey));
    combo.SelectionChanged([owner, values, key = choice.key](IInspectable const& sender,
                                                             controls::SelectionChangedEventArgs const&) noexcept {
        try {
            int32_t index = sender.as<controls::ComboBox>().SelectedIndex();
            if (index < 0 || index >= static_cast<int32_t>(values->size())) return;
            std::string error;
            if (uirender::SetAppleTweak(owner->kit, key, (*values)[index], error)) {
                owner->journal.Write("Settings: " + key + " = " + (*values)[index] + " from the Ampify page");
            } else {
                owner->journal.Write("Settings: could not change " + key + ": " + error);
            }
        } catch (...) {
        }
    });
    return LabelledRow(shared, i18n::Text(choice.labelKey), i18n::Text(choice.helpKey), combo);
}

// The theme in use, chosen from the installed ones, then its settings. A choice is written to settings.txt, which the
// theme service watches: the theme changes at once, and its settings replace the last one's.
xaml::FrameworkElement ThemeChoice(UiService::Shared& shared) {
    controls::StackPanel section;
    section.Spacing(10);
    std::vector<uirender::ModEntry> themes = uirender::ListThemes(shared.kit);
    std::string active = uirender::ActiveThemeId(shared.kit);
    {
        std::lock_guard<std::mutex> lock(shared.mutex);
        shared.cardTheme = active;
    }
    controls::ComboBox combo;
    if (shared.probe.comboBox) combo.Style(shared.probe.comboBox);
    combo.MinWidth(220);
    Name(combo, "AMPIFY_ThemeChoice", i18n::Text("mods.theme"));
    auto ids = std::make_shared<std::vector<std::string>>(1, "");  // "" is the choice "None"
    combo.Items().Append(winrt::box_value(Wide(i18n::Text("mods.themeNone"))));
    for (const uirender::ModEntry& theme : themes) {
        ids->push_back(theme.id);
        combo.Items().Append(winrt::box_value(Wide(theme.NameIn(i18n::Language()))));
    }
    auto chosen = std::find(ids->begin(), ids->end(), active);
    combo.SelectedIndex(chosen == ids->end() ? 0 : static_cast<int32_t>(chosen - ids->begin()));
    section.Children().Append(LabelledRow(shared, i18n::Text("mods.theme"), i18n::Text("mods.themeHelp"), combo));
    section.Children().Append(AccentChoice(shared));
    // Like the accent, the glass level holds for every theme and snippet, and the engine follows it within a tick.
    section.Children().Append(TweakRow(shared, uirender::GlassChoice(shared.kit)));

    controls::StackPanel settingsHost;
    settingsHost.Spacing(10);
    FillThemeSettings(shared, settingsHost);
    section.Children().Append(settingsHost);

    UiService::Shared* owner = &shared;
    combo.SelectionChanged([owner, ids, settingsHost](IInspectable const& sender,
                                                      controls::SelectionChangedEventArgs const&) noexcept {
        try {
            int32_t index = sender.as<controls::ComboBox>().SelectedIndex();
            if (index < 0 || index >= static_cast<int32_t>(ids->size())) return;
            const std::string& id = (*ids)[index];
            if (id == uirender::ActiveThemeId(owner->kit)) return;
            std::string error;
            {
                std::lock_guard<std::mutex> lock(owner->mutex);
                owner->cardTheme = id;  // the card already shows it: no rebuild
            }
            if (!uirender::ChooseTheme(owner->kit, id, error)) {
                owner->journal.Write("Settings: could not choose the theme \"" + id + "\": " + error);
                return;
            }
            owner->journal.Write("Settings: theme \"" + (id.empty() ? std::string("none") : id) +
                                 "\" chosen from the Ampify page");
            FillThemeSettings(*owner, settingsHost);
        } catch (...) {
        }
    });
    return section;
}

// Adds a row to the rows of a section's card, under a line when it is not the first.
void AddRow(const controls::StackPanel& rows, const xaml::UIElement& row) {
    if (rows.Children().Size() > 0) rows.Children().Append(MakeDivider());
    rows.Children().Append(row);
}

// One choice of the card as a menu row, written to settings.txt when it changes (uirender::SetAppleTweak).
void AddTweakRow(UiService::Shared& shared, const controls::StackPanel& rows, const uirender::TweakChoice& choice) {
    AddRow(rows, TweakRow(shared, choice));
}

// What Ampify changes in Apple Music's own behaviour (uirender::AppleTweakChoices): one choice per row. The kernel
// reads settings.txt again within a second (UiService::Run), so a choice applies at once.
void AddAppleTweakRows(UiService::Shared& shared, const controls::StackPanel& rows) {
    for (const uirender::TweakChoice& choice : uirender::AppleTweakChoices(shared.kit)) AddTweakRow(shared, rows, choice);
}

// A zip of the state and the logs with names, folders and music taken out (`ampify report`), opened in Explorer once
// written. The button says when it is done; the button is moved back to the interface thread before it is released.
xaml::FrameworkElement ReportRow(UiService::Shared& shared) {
    controls::Button report;
    if (shared.probe.button) report.Style(shared.probe.button);
    report.Content(winrt::box_value(Wide(i18n::Text("mods.report.run"))));
    report.VerticalAlignment(xaml::VerticalAlignment::Center);
    Name(report, "AMPIFY_ModsReport", i18n::Text("mods.report.run"));
    Journal* journal = &shared.journal;
    report.Click([journal](IInspectable const& sender, xaml::RoutedEventArgs const&) noexcept {
        try {
            controls::Button button = sender.as<controls::Button>();
            button.IsEnabled(false);
            button.Content(winrt::box_value(Wide(i18n::Text("mods.report.running"))));
            dispatching::DispatcherQueue queue = dispatching::DispatcherQueue::GetForCurrentThread();
            auto held = std::make_shared<controls::Button>(button);
            RunAmpifyInBackground({"report", "--dir"}, [queue, held, journal](int code) mutable {
                journal->Write("Settings: report exported from the Ampify page (code " + std::to_string(code) + ")");
                queue.TryEnqueue([held = std::move(held), code]() noexcept {
                    try {
                        held->IsEnabled(true);
                        held->Content(winrt::box_value(Wide(i18n::Text(code == 0 ? "mods.report.done" : "mods.report.failed"))));
                    } catch (...) {
                    }
                });
            });
        } catch (...) {
        }
    });
    return LabelledRow(shared, i18n::Text("mods.report"), i18n::Text("mods.reportHelp"), report);
}

// A button with Apple's look, or the accent's for the main action of a page.
controls::Button MakeButton(UiService::Shared& shared, const std::string& textKey, const std::string& name, bool primary) {
    controls::Button button;
    if (shared.probe.button) button.Style(shared.probe.button);
    if (primary) {
        if (controls::ControlTemplate accent = PrimaryButtonTemplate()) button.Template(accent);
    }
    button.Content(winrt::box_value(Wide(i18n::Text(textKey))));
    button.VerticalAlignment(xaml::VerticalAlignment::Center);
    Name(button, name, i18n::Text(textKey));
    return button;
}

// A section of the Ampify page, as Apple groups its own settings: a heading, a line on what it holds, then one card
// whose rows are split by lines.
controls::StackPanel Section(UiService::Shared& shared, const std::string& name, const std::string& titleKey,
                             const std::string& subtitleKey, const controls::StackPanel& rows) {
    controls::StackPanel section;
    section.Spacing(4);
    const std::string title = i18n::Text(titleKey);
    Name(section, "AMPIFY_Settings_" + name, title);
    controls::TextBlock heading = MakeText(Wide(title).c_str(), "subtitle", false, nullptr);
    automation::AutomationProperties::SetHeadingLevel(heading, automation::Peers::AutomationHeadingLevel::Level2);
    section.Children().Append(heading);
    if (!subtitleKey.empty()) {
        section.Children().Append(MakeText(Wide(i18n::Text(subtitleKey)).c_str(), "caption", true, &shared.probe));
    }
    controls::Border card = MakeCardBorder(shared.probe);
    card.Margin(xaml::Thickness{0, 8, 0, 0});
    rows.Spacing(12);
    card.Child(rows);
    section.Children().Append(card);
    return section;
}

// One row per installed extension, its settings under it.
controls::StackPanel ExtensionRows(UiService::Shared& shared) {
    controls::StackPanel rows;
    std::vector<uirender::ModEntry> mods = uirender::ListMods(shared.kit);
    if (mods.empty()) rows.Children().Append(MakeText(Wide(i18n::Text("mods.none")).c_str(), "caption", true, &shared.probe));
    for (const uirender::ModEntry& mod : mods) {
        controls::StackPanel item;
        item.Spacing(10);
        item.Children().Append(ModRow(shared, mod));
        if (!mod.settings.empty()) item.Children().Append(ModSettings(shared, mod));
        AddRow(rows, item);
    }
    return rows;
}

// The Atmos leveler is built into the kernel, so no manifest brings its row: it is written here, in words a beginner
// reads, and switched like an extension (uirender::SwitchMod).
controls::StackPanel SoundRows(UiService::Shared& shared) {
    uirender::ModEntry leveler;
    leveler.id = kAtmosLeveler;
    leveler.name = i18n::Text("sound.leveler");
    leveler.description = i18n::Text("sound.levelerHelp");
    leveler.enabled = uirender::IsModOn(shared.kit, kAtmosLeveler);
    controls::StackPanel rows;
    AddRow(rows, ModRow(shared, leveler));
    return rows;
}

xaml::FrameworkElement FolderRow(UiService::Shared& shared) {
    controls::Button open = MakeButton(shared, "mods.openFolder", "AMPIFY_ModsOpenFolder", false);
    std::filesystem::path kit = shared.kit;
    open.Click([kit](IInspectable const&, xaml::RoutedEventArgs const&) noexcept {
        try {
            OpenKitFolder(kit);
        } catch (...) {
        }
    });
    return LabelledRow(shared, i18n::Text("mods.folder"), i18n::Text("mods.folderHelp"), open);
}

}  // namespace

// Ampify's settings page: a line on what Ampify is and the way to the Marketplace, then one section per subject.
controls::StackPanel BuildAmpifyPage(UiService::Shared& shared) {
    shared.settingsCards.clear();  // the page shown until now is replaced, its cards with it
    controls::StackPanel page;
    page.Spacing(28);
    page.MaxWidth(kAmpifyPageWidth);
    page.HorizontalAlignment(xaml::HorizontalAlignment::Left);
    page.Padding(xaml::Thickness{0, 0, 0, 24});
    Name(page, "AMPIFY_SettingsPage", "Ampify");

    controls::Grid intro;
    intro.ColumnSpacing(16);
    controls::ColumnDefinition text;
    text.Width(xaml::GridLength{1.0, xaml::GridUnitType::Star});
    controls::ColumnDefinition action;
    action.Width(xaml::GridLength{1.0, xaml::GridUnitType::Auto});
    intro.ColumnDefinitions().Append(text);
    intro.ColumnDefinitions().Append(action);
    controls::TextBlock line = MakeText(Wide(i18n::Text("mods.subtitle")).c_str(), "body", true, nullptr);
    line.VerticalAlignment(xaml::VerticalAlignment::Center);
    intro.Children().Append(line);
    controls::Button market = MakeButton(shared, "mods.openMarketplace", "AMPIFY_ModsOpenMarketplace", true);
    UiModel* model = &shared.model;
    Journal* journal = &shared.journal;
    market.Click([model, journal](IInspectable const&, xaml::RoutedEventArgs const&) noexcept {
        try {
            model->Navigate("marketplace", "/marketplace");
            journal->Write("Settings: Marketplace opened from the Ampify page");
        } catch (...) {
        }
    });
    controls::Grid::SetColumn(market, 1);
    intro.Children().Append(market);
    page.Children().Append(intro);

    page.Children().Append(Section(shared, "Extensions", "mods.extensions", "mods.extensionsHelp", ExtensionRows(shared)));
    page.Children().Append(Section(shared, "Sound", "sound.title", "", SoundRows(shared)));
    controls::StackPanel lookRows;
    lookRows.Children().Append(ThemeChoice(shared));
    page.Children().Append(Section(shared, "Look", "mods.look", "", lookRows));
    controls::StackPanel appleRows;
    AddAppleTweakRows(shared, appleRows);
    page.Children().Append(Section(shared, "AppleMusic", "tweaks.title", "tweaks.subtitle", appleRows));
    controls::StackPanel helpRows;
    AddTweakRow(shared, helpRows, uirender::KitUpdateChoice(shared.kit));
    AddTweakRow(shared, helpRows, uirender::EntryChoice(shared.kit));
    AddRow(helpRows, FolderRow(shared));
    AddRow(helpRows, ReportRow(shared));
    page.Children().Append(Section(shared, "Help", "mods.help", "", helpRows));
    return page;
}

namespace {

// Copies the look of Apple's controls from Settings > Playback (switches, menus, buttons, texts) once that page was
// seen, so the Ampify page reads like Apple's settings. Nothing is added to Apple's page. False until its rows are
// laid out, a few layout passes after Loaded.
bool CaptureSettingsStyle(UiService::Shared& shared, const xaml::FrameworkElement& page) {
    StyleProbe probe;
    CaptureStyleProbe(page, probe, "PlaybackSettingsPage");
    if (probe.Found() == 0) return false;
    shared.probe = probe;
    shared.Once("settings-style", "Settings: the look of Apple's settings copied from the Playback page (" +
                                      std::to_string(probe.Found()) + " native style value(s))");
    return true;
}

}  // namespace

void WatchSettingsPage(UiService::Shared& shared, const std::shared_ptr<WindowContext>& context) {
    {
        std::lock_guard<std::mutex> lock(shared.mutex);
        if (shared.cardStale) context->ampifyPageStale = true;
        shared.cardStale = false;
    }
    xaml::FrameworkElement page = context->foundSettings.get();
    xaml::FrameworkElement current = context->settingsPage.get();
    if (page != current) {
        if (current) {
            try {
                current.LayoutUpdated(context->settingsLayout);
            } catch (...) {
            }
        }
        context->settingsPage = page ? winrt::make_weak(page) : winrt::weak_ref<xaml::FrameworkElement>{};
        context->settingsMounted = false;
        if (page) {
            WeakShared weakShared = shared.weak_from_this();
            WeakContext weakContext = context;
            std::uint64_t revision = context->rootRevision.load();
            // LayoutUpdated until the page is mounted: its rows appear a few layout passes after Loaded.
            context->settingsLayout = page.LayoutUpdated([weakShared, weakContext, revision](IInspectable const&,
                                                                                            IInspectable const&) noexcept {
                try {
                    auto live = weakContext.lock();
                    if (live && !live->settingsMounted && live->ticket.OnLayout(NowMs())) {
                        Wake(weakShared, weakContext, revision, false);
                    }
                } catch (...) {
                }
            });
            context->ticket.ArmLayout(NowMs());
            context->settingsSeenMs = NowMs();
            shared.journal.Write("Settings: PlaybackSettingsPage seen, waiting for it to load");
        }
    }
    if (page && !context->settingsMounted && page.IsLoaded()) {
        if (CaptureSettingsStyle(shared, page)) {
            context->settingsMounted = true;
            context->ticket.StopLayout();
        }
    }
}

namespace {

// ---- The Ampify row at the top of Settings -----------------------------------------------------------------------

// Ampify's logo, the puzzle piece of the sidebar entry (apps/marketplace/main.js).
constexpr char kAmpifyIcon[] =
    "<svg viewBox=\"0 0 64 64\"><path d=\"M12.3 13L19 13C21.5 13 22.25 11.25 20.75 9.25C17.75 5 20.5 .75 26 .75"
    "C31.5 .75 34.25 5 31.25 9.25C29.75 11.25 30.5 13 33 13L39.7 13A11.3 11.3 0 0 1 51 24.3L51 31C51 33.5 52.75 34.25"
    " 54.75 32.75C59 29.75 63.25 32.5 63.25 38C63.25 43.5 59 46.25 54.75 43.25C52.75 41.75 51 42.5 51 45L51 51.7"
    "A11.3 11.3 0 0 1 39.7 63L12.3 63A11.3 11.3 0 0 1 1 51.7L1 24.3A11.3 11.3 0 0 1 12.3 13Z\"/></svg>";

// The index of the TextBlock named `name` (the title of one of Apple's rows, such as "PlaybackNavigationViewItem").
std::optional<int> FindNamed(const std::vector<ProbeNode>& nodes, std::string_view name) {
    for (std::size_t index = 0; index < nodes.size(); index++) {
        if (nodes[index].name == name) return static_cast<int>(index);
    }
    return std::nullopt;
}

// The Button of Apple's row whose title is the node at `title`.
controls::Button RowOf(const std::vector<ProbeNode>& nodes, const std::vector<xaml::DependencyObject>& objects,
                       std::optional<int> title) {
    if (!title) return nullptr;
    for (int parent = nodes[*title].parent; parent >= 0; parent = nodes[parent].parent) {
        if (auto button = objects[parent].try_as<controls::Button>()) return button;
    }
    return nullptr;
}

// A TextBlock that reads like `model`, Apple's title or description of a row.
controls::TextBlock TextLike(const controls::TextBlock& model, const std::string& text) {
    controls::TextBlock block;
    block.Text(Wide(text));
    block.TextWrapping(xaml::TextWrapping::WrapWholeWords);
    if (!model) return block;
    block.FontFamily(model.FontFamily());
    block.FontSize(model.FontSize());
    block.FontWeight(model.FontWeight());
    block.Foreground(model.Foreground());
    block.Opacity(model.Opacity());  // Apple dims its descriptions by opacity (0.6), not by their brush
    block.CharacterSpacing(model.CharacterSpacing());
    block.Margin(model.Margin());
    return block;
}

// A row like Playback's (Apple's style, so the chevron, the hover and the press come with it). A click opens Ampify's
// own settings page over Settings, as Apple's rows open theirs; Apple's back arrow or the page's own closes it.
controls::Button BuildSettingsEntry(UiService::Shared& shared, const controls::Button& playback,
                                    const controls::TextBlock& titleModel, const controls::TextBlock& textModel) {
    controls::Grid content;
    controls::ColumnDefinition iconColumn;
    iconColumn.Width(xaml::GridLength{1.0, xaml::GridUnitType::Auto});
    controls::ColumnDefinition textColumn;
    textColumn.Width(xaml::GridLength{1.0, xaml::GridUnitType::Star});
    content.ColumnDefinitions().Append(iconColumn);
    content.ColumnDefinitions().Append(textColumn);
    xaml::FrameworkElement icon = MakeIconElement(kAmpifyIcon, 16, L"TextFillColorPrimaryBrush");
    if (icon) {
        // Lines the icon and the title up with Apple's rows (icon centred 16 px in, title 48 px in).
        icon.Margin(xaml::Thickness{8, 0, 24, 0});
        icon.VerticalAlignment(xaml::VerticalAlignment::Center);
        content.Children().Append(icon);
    }
    controls::StackPanel texts;
    texts.Padding(xaml::Thickness{0, 12, 0, 12});
    texts.Children().Append(TextLike(titleModel, "Ampify"));
    texts.Children().Append(TextLike(textModel, i18n::Text("settings.entryHelp")));
    controls::Grid::SetColumn(texts, 1);
    content.Children().Append(texts);

    controls::Button entry;
    entry.Style(playback.Style());
    entry.Margin(playback.Margin());
    entry.Padding(playback.Padding());
    entry.HorizontalAlignment(playback.HorizontalAlignment());
    entry.HorizontalContentAlignment(playback.HorizontalContentAlignment());
    entry.VerticalContentAlignment(playback.VerticalContentAlignment());
    entry.Background(playback.Background());
    entry.BorderBrush(playback.BorderBrush());
    entry.BorderThickness(playback.BorderThickness());
    entry.CornerRadius(playback.CornerRadius());
    entry.Content(content);
    Name(entry, "AMPIFY_SettingsEntry", "Ampify");
    automation::AutomationProperties::SetHelpText(entry, Wide(i18n::Text("settings.entryHelp")));

    UiModel* model = &shared.model;
    Journal* journal = &shared.journal;
    entry.Click([model, journal](IInspectable const&, xaml::RoutedEventArgs const&) noexcept {
        try {
            model->Navigate(kAmpifyPageOwner, kAmpifyPagePath);
            journal->Write("Settings: Ampify page opened from the Ampify row");
        } catch (...) {
        }
    });
    return entry;
}

}  // namespace

// Puts the Ampify row after Apple's last row (Advanced) of the Settings page, once per page.
void WatchSettingsEntry(UiService::Shared& shared, const std::shared_ptr<WindowContext>& context) {
    xaml::FrameworkElement page = context->foundMainSettings.get();
    if (!page || !page.IsLoaded()) return;
    if (context->entryPage.get() == page && context->settingsEntry && context->settingsEntry.Parent()) return;

    std::vector<ProbeNode> nodes;
    std::vector<xaml::DependencyObject> objects;
    CollectPage(page, nodes, objects);
    std::optional<int> playbackTitle = FindNamed(nodes, "PlaybackNavigationViewItem");
    controls::Button playback = RowOf(nodes, objects, playbackTitle);
    if (!playback) return;  // its rows appear a few layout passes after Loaded: the next refresh tries again
    auto panel = media::VisualTreeHelper::GetParent(playback).try_as<controls::Panel>();
    if (!panel) {
        shared.Once("settings-entry-panel", "Settings: Apple's rows are not in a panel, Ampify row not added");
        return;
    }

    controls::TextBlock titleModel = objects[*playbackTitle].try_as<controls::TextBlock>();
    controls::TextBlock textModel{nullptr};
    if (auto texts = media::VisualTreeHelper::GetParent(titleModel).try_as<controls::Panel>(); texts && texts.Children().Size() > 1) {
        textModel = texts.Children().GetAt(1).try_as<controls::TextBlock>();
    }
    RemoveFromParent(context->settingsEntry);
    context->settingsEntry = BuildSettingsEntry(shared, playback, titleModel, textModel);

    auto children = panel.Children();
    std::uint32_t at = children.Size();
    controls::Button last = RowOf(nodes, objects, FindNamed(nodes, "AdvancedNavigationViewItem"));
    std::uint32_t index = 0;
    if (last && children.IndexOf(last, index)) at = index + 1;
    children.InsertAt(at, context->settingsEntry);
    context->entryPage = winrt::make_weak(page);
    shared.journal.Write("Settings: row \"Ampify\" added to the Settings page");
}

}  // namespace ampify::win::ui_service_detail

#endif  // AMPIFY_WITH_XAML
