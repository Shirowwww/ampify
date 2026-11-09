// Ampify's settings page (Settings > Ampify): its sections, the theme and accent choices, the Apple Music
// tweaks, and the Ampify row added at the top of Apple Music's settings.

#if defined(AMPIFY_WITH_XAML)

#include "ui_service_detail.hpp"

#include "../core/kit_layout.hpp"
#include "ui_motion.hpp"

#include <winrt/Microsoft.UI.Xaml.Shapes.h>

namespace ampify::win::ui_service_detail {

namespace {

constexpr std::size_t kPageLimit = 4000;

constexpr double kAmpifyPageWidth = 1000;  // as wide as Apple's settings rows
constexpr double kAccentFieldWidth = 110;  // "#RRGGBB" with room to type
constexpr auto kFieldFade = std::chrono::milliseconds(150);
constexpr auto kRingFade = std::chrono::milliseconds(200);   // the accent ring, from one swatch to the next
constexpr auto kRingSettle = std::chrono::milliseconds(40);  // a frame or two more before the accent is written

constexpr char kAmpifyPageOwner[] = "ampify";
constexpr char kAtmosLeveler[] = "atmos-leveler";
static_assert(IsBuiltInExtension(kAtmosLeveler), "AddSoundRows writes the row of an extension built into the kernel");

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

// Label and help on the left, the control on the right.
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

// Where the settings of the active theme and snippets go: a row of the page's navigation that holds their folds.
struct ThemeFolds {
    controls::StackPanel host;
    std::size_t row = 0;
};

// Folds the settings of the theme active now, then those of the snippets switched on, each under its name; the row
// stays hidden when none declares any. A search under way looks through the new folds at once.
void FillThemeFolds(UiService::Shared& shared, const std::shared_ptr<SettingsNav>& nav, const ThemeFolds& folds) {
    if (!nav || folds.row >= nav->rows.size()) return;
    folds.host.Children().Clear();
    nav->rows[folds.row].folds.clear();
    std::vector<uirender::ModEntry> elements;
    if (std::optional<uirender::ModEntry> theme = uirender::ActiveThemeSettings(shared.kit)) elements.push_back(*theme);
    for (uirender::ModEntry& snippet : uirender::EnabledSnippetSettings(shared.kit)) elements.push_back(std::move(snippet));
    for (const uirender::ModEntry& element : elements) {
        if (folds.host.Children().Size() > 0) folds.host.Children().Append(MakeDivider());
        folds.host.Children().Append(AddSettingsFold(shared, nav, folds.row, element, true));
    }
    ShowSettingsState(shared, *nav, false);
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
    box.Width(kAccentFieldWidth);
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
            // A handler runs inside Apple Music's XAML frame: nothing may escape it.
        }
    });
    box.KeyDown([commit](IInspectable const& sender, xaml::Input::KeyRoutedEventArgs const& args) noexcept {
        try {
            if (args.Key() == winrt::Windows::System::VirtualKey::Enter) commit(sender.as<controls::TextBox>());
        } catch (...) {
            // A handler runs inside Apple Music's XAML frame: nothing may escape it.
        }
    });
    return box;
}

media::SolidColorBrush HexBrush(std::string_view hex) {
    auto byte = [&](std::size_t at) {
        unsigned value = 0;
        std::from_chars(hex.data() + at, hex.data() + at + 2, value, 16);
        return static_cast<uint8_t>(value);
    };
    return media::SolidColorBrush(winrt::Windows::UI::Color{255, byte(0), byte(2), byte(4)});
}

// What a swatch shows: the theme's own accent as a ring of the text colour, the artwork's as a warm-to-cool blend, a
// preset as its colour, "custom" as a plus.
xaml::FrameworkElement SwatchFace(const std::string& value, bool custom) {
    constexpr double kDot = 18;
    if (custom) {
        controls::FontIcon plus;
        plus.Glyph(L"");  // Add, in Segoe Fluent Icons
        plus.FontSize(12);
        return plus;
    }
    winrt::Microsoft::UI::Xaml::Shapes::Ellipse dot;
    dot.Width(kDot);
    dot.Height(kDot);
    if (value.empty()) {
        if (auto ring = ThemeResource(L"TextFillColorSecondaryBrush").try_as<media::Brush>()) dot.Stroke(ring);
        dot.StrokeThickness(2);
    } else if (value == uirender::kArtworkAccent) {
        media::LinearGradientBrush blend;
        blend.StartPoint({0, 0});
        blend.EndPoint({1, 1});
        media::GradientStop warm;
        warm.Color(winrt::Windows::UI::Color{255, 0xE7, 0xA6, 0x4B});
        media::GradientStop cool;
        cool.Color(winrt::Windows::UI::Color{255, 0x4B, 0x6B, 0xE7});
        cool.Offset(1);
        blend.GradientStops().Append(warm);
        blend.GradientStops().Append(cool);
        dot.Fill(blend);
    } else {
        dot.Fill(HexBrush(value));
    }
    return dot;
}

// One round button of the accent row, with the ring that marks the choice (faded in and out, motion::FadeTo).
controls::Button Swatch(const std::string& value, bool custom, const std::string& name,
                        winrt::Microsoft::UI::Xaml::Shapes::Ellipse& ring) {
    constexpr double kSize = 28;
    controls::Grid face;
    ring.Width(kSize - 2);
    ring.Height(kSize - 2);
    if (auto brush = ThemeResource(L"TextFillColorPrimaryBrush").try_as<media::Brush>()) ring.Stroke(brush);
    ring.StrokeThickness(2);
    ring.Opacity(0);
    face.Children().Append(ring);
    xaml::FrameworkElement inner = SwatchFace(value, custom);
    inner.HorizontalAlignment(xaml::HorizontalAlignment::Center);
    inner.VerticalAlignment(xaml::VerticalAlignment::Center);
    face.Children().Append(inner);
    controls::Button swatch;
    swatch.Width(kSize);
    swatch.Height(kSize);
    swatch.MinWidth(0);
    swatch.MinHeight(0);
    swatch.Padding(xaml::Thickness{0, 0, 0, 0});
    swatch.CornerRadius(xaml::CornerRadius{kSize / 2, kSize / 2, kSize / 2, kSize / 2});
    swatch.Background(media::SolidColorBrush(winrt::Windows::UI::Color{0, 0, 0, 0}));
    swatch.BorderThickness(xaml::Thickness{0, 0, 0, 0});
    swatch.Content(face);
    controls::ToolTipService::SetToolTip(swatch, winrt::box_value(Wide(name)));
    Name(swatch, "AMPIFY_Accent_" + (custom ? std::string("custom") : value.empty() ? std::string("theme") : value), name);
    return swatch;
}

// The accent over the theme's, chosen in one click: the theme's own, a preset, the artwork's, or a colour typed in
// hex (the plus shows the field). The ring moves to the swatch chosen.
xaml::FrameworkElement AccentChoice(UiService::Shared& shared) {
    std::string chosen = uirender::ChosenAccent(shared.kit);
    std::vector<std::pair<std::string, std::string>> choices{{"", i18n::Text("accent.theme")}};  // "": the theme's
    for (const uirender::AccentPreset& preset : uirender::AccentPresets()) {
        choices.emplace_back(preset.value, i18n::Text(preset.textKey));
    }
    choices.emplace_back(uirender::kArtworkAccent, i18n::Text("accent.artwork"));
    auto known = std::find_if(choices.begin(), choices.end(), [&](const auto& choice) { return choice.first == chosen; });
    const std::size_t customIndex = choices.size();
    std::size_t selected = known == choices.end() ? customIndex : static_cast<std::size_t>(known - choices.begin());

    // The field keeps its place while hidden, so the plus fades it in without moving the row or its text.
    controls::TextBox custom = AccentField(shared, known == choices.end() ? chosen : "");
    xaml::ScalarTransition fade;
    fade.Duration(kFieldFade);
    custom.OpacityTransition(fade);
    auto showCustom = [custom](bool shown) {
        custom.Opacity(shown ? 1.0 : 0.0);
        custom.IsEnabled(shown);  // out of the Tab order while hidden, and out of screen readers' tree
        automation::AutomationProperties::SetAccessibilityView(
            custom, shown ? automation::Peers::AccessibilityView::Content : automation::Peers::AccessibilityView::Raw);
    };
    showCustom(selected == customIndex);

    controls::StackPanel swatches;
    swatches.Orientation(controls::Orientation::Horizontal);
    swatches.Spacing(4);
    Name(swatches, "AMPIFY_AccentChoice", i18n::Text("accent.title"));
    // The ring for the eyes, the item status for screen readers. The swatches are found through their row when it is
    // marked, so no swatch holds the others (a cycle would keep every page built so far).
    auto mark = [](const controls::Panel& row, std::size_t chosenIndex) {
        for (std::uint32_t other = 0; other < row.Children().Size(); other++) {
            auto button = row.Children().GetAt(other).as<controls::Button>();
            bool chosen = other == chosenIndex;
            motion::FadeTo(button.Content().as<controls::Grid>().Children().GetAt(0), chosen ? 1.0 : 0.0, kRingFade);
            automation::AutomationProperties::SetItemStatus(button, chosen ? Wide(i18n::Text("accent.chosen")) : winrt::hstring{});
        }
    };
    UiService::Shared* owner = &shared;
    // A new accent makes the window read its theme resources again, which holds the interface thread for most of a
    // second: written at the click, the ring's fade waited behind it and jumped in a frame. The ring moves first, and
    // the accent is written once it has (the last click wins).
    dispatching::DispatcherQueueTimer commit = dispatching::DispatcherQueue::GetForCurrentThread().CreateTimer();
    commit.Interval(kRingFade + kRingSettle);
    commit.IsRepeating(false);
    auto pending = std::make_shared<std::string>();
    commit.Tick([owner, pending](auto const&, auto const&) noexcept {
        try {
            if (*pending != uirender::ChosenAccent(owner->kit)) CommitAccent(*owner, *pending);
        } catch (...) {
            // A handler runs inside Apple Music's XAML frame: nothing may escape it.
        }
    });
    for (std::size_t index = 0; index <= customIndex; index++) {
        bool isCustom = index == customIndex;
        std::string value = isCustom ? std::string() : choices[index].first;
        std::string name = isCustom ? i18n::Text("accent.custom") : choices[index].second;
        winrt::Microsoft::UI::Xaml::Shapes::Ellipse ring;
        controls::Button swatch = Swatch(value, isCustom, name, ring);
        swatch.Click([mark, index, isCustom, value, custom, showCustom, commit, pending](
                         IInspectable const& sender, xaml::RoutedEventArgs const&) noexcept {
            try {
                if (auto row = sender.as<controls::Button>().Parent().try_as<controls::Panel>()) mark(row, index);
                showCustom(isCustom);
                commit.Stop();
                if (isCustom) {
                    custom.Focus(xaml::FocusState::Programmatic);
                } else {
                    *pending = value;
                    commit.Start();
                }
            } catch (...) {
                // A handler runs inside Apple Music's XAML frame: nothing may escape it.
            }
        });
        swatches.Children().Append(swatch);
    }
    mark(swatches, selected);
    controls::StackPanel control;
    control.Orientation(controls::Orientation::Horizontal);
    control.Spacing(8);
    control.Children().Append(custom);
    control.Children().Append(swatches);
    return LabelledRow(shared, i18n::Text("accent.title"), i18n::Text("accent.help"), control);
}

// One choice of the card as a menu row, written to settings.txt when it changes (uirender::SetAppleTweak).
xaml::FrameworkElement TweakRow(UiService::Shared& shared, const uirender::TweakChoice& choice) {
    UiService::Shared* owner = &shared;
    controls::ComboBox combo;
    if (shared.probe.comboBox) combo.Style(shared.probe.comboBox);
    combo.Width(kSettingControlWidth);
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
            // A handler runs inside Apple Music's XAML frame: nothing may escape it.
        }
    });
    return LabelledRow(shared, i18n::Text(choice.labelKey), i18n::Text(choice.helpKey), combo);
}

// The theme in use, chosen from the installed ones. A choice is written to settings.txt, which the theme service
// watches: the theme changes at once, and its settings replace the last one's in `folds`.
xaml::FrameworkElement ThemeChoice(UiService::Shared& shared, const std::shared_ptr<SettingsNav>& nav,
                                   const std::shared_ptr<ThemeFolds>& folds) {
    std::vector<uirender::ModEntry> themes = uirender::ListThemes(shared.kit);
    std::string active = uirender::ActiveThemeId(shared.kit);
    {
        std::lock_guard<std::mutex> lock(shared.mutex);
        shared.cardTheme = active;
    }
    controls::ComboBox combo;
    if (shared.probe.comboBox) combo.Style(shared.probe.comboBox);
    combo.Width(kSettingControlWidth);
    Name(combo, "AMPIFY_ThemeChoice", i18n::Text("mods.theme"));
    auto ids = std::make_shared<std::vector<std::string>>(1, "");  // "" is the choice "None"
    combo.Items().Append(winrt::box_value(Wide(i18n::Text("mods.themeNone"))));
    for (const uirender::ModEntry& theme : themes) {
        ids->push_back(theme.id);
        combo.Items().Append(winrt::box_value(Wide(theme.NameIn(i18n::Language()))));
    }
    auto chosen = std::find(ids->begin(), ids->end(), active);
    combo.SelectedIndex(chosen == ids->end() ? 0 : static_cast<int32_t>(chosen - ids->begin()));
    UiService::Shared* owner = &shared;
    std::weak_ptr<SettingsNav> weak = nav;
    combo.SelectionChanged([owner, ids, weak, folds](IInspectable const& sender,
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
            FillThemeFolds(*owner, weak.lock(), *folds);
        } catch (...) {
            // A handler runs inside Apple Music's XAML frame: nothing may escape it.
        }
    });
    return LabelledRow(shared, i18n::Text("mods.theme"), i18n::Text("mods.themeHelp"), combo);
}

settings_panel::SettingText TextOf(const std::string& labelKey, const std::string& helpKey) {
    return {i18n::Text(labelKey), helpKey.empty() ? std::string() : i18n::Text(helpKey)};
}

void AddTweakRow(UiService::Shared& shared, SettingsNav& nav, const controls::StackPanel& rows,
                 const uirender::TweakChoice& choice) {
    AddSettingsRow(nav, rows, TweakRow(shared, choice), TextOf(choice.labelKey, choice.helpKey));
}

// What Ampify changes in one part of Apple Music's own behaviour (uirender::AppleTweakChoices): one choice per row.
// The kernel reads settings.txt again within a second (UiService::Run), so a choice applies at once.
void AddAppleTweakRows(UiService::Shared& shared, SettingsNav& nav, const controls::StackPanel& rows,
                       uirender::TweakGroup group) {
    for (const uirender::TweakChoice& choice : uirender::AppleTweakChoices(shared.kit, group)) {
        AddTweakRow(shared, nav, rows, choice);
    }
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
                        // A handler runs inside Apple Music's XAML frame: nothing may escape it.
                    }
                });
            });
        } catch (...) {
            // A handler runs inside Apple Music's XAML frame: nothing may escape it.
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

// One row per installed extension, its settings folded under it.
void AddExtensionRows(UiService::Shared& shared, const std::shared_ptr<SettingsNav>& nav, const controls::StackPanel& rows) {
    std::vector<uirender::ModEntry> mods = uirender::ListMods(shared.kit);
    if (mods.empty()) {
        AddSettingsRow(*nav, rows, MakeText(Wide(i18n::Text("mods.none")).c_str(), "caption", true, &shared.probe),
                       TextOf("mods.none", ""));
    }
    for (const uirender::ModEntry& mod : mods) {
        controls::StackPanel item;
        item.Spacing(4);
        item.Children().Append(ModRow(shared, mod));
        std::size_t row = AddSettingsRow(*nav, rows, item,
                                         {mod.NameIn(i18n::Language()), mod.DescriptionIn(i18n::Language())});
        if (!mod.settings.empty()) item.Children().Append(AddSettingsFold(shared, nav, row, mod, false));
    }
}

// The Atmos leveler is built into the kernel, so no manifest brings its row: it is written here, in words a beginner
// reads, and switched like an extension (uirender::SwitchMod).
void AddSoundRows(UiService::Shared& shared, SettingsNav& nav, const controls::StackPanel& rows) {
    uirender::ModEntry leveler;
    leveler.id = kAtmosLeveler;
    leveler.name = i18n::Text("sound.leveler");
    leveler.description = i18n::Text("sound.levelerHelp");
    leveler.enabled = uirender::IsModOn(shared.kit, kAtmosLeveler);
    AddSettingsRow(nav, rows, ModRow(shared, leveler), {leveler.name, leveler.description});
}

// The look: theme, accent and glass, then the folded settings of the theme and the snippets switched on.
void AddLookPane(UiService::Shared& shared, const std::shared_ptr<SettingsNav>& nav) {
    AddSettingsPane(shared, *nav, "mods.look");
    controls::StackPanel rows = AddSettingsGroup(shared, *nav, "Look", "", "");
    auto folds = std::make_shared<ThemeFolds>();
    folds->host = controls::StackPanel();
    folds->host.Spacing(4);
    AddSettingsRow(*nav, rows, ThemeChoice(shared, nav, folds), TextOf("mods.theme", "mods.themeHelp"));
    AddSettingsRow(*nav, rows, AccentChoice(shared), TextOf("accent.title", "accent.help"));
    // Like the accent, the glass level holds for every theme and snippet, and the engine follows it within a tick.
    AddTweakRow(shared, *nav, rows, uirender::GlassChoice(shared.kit));
    controls::StackPanel elements = AddSettingsGroup(shared, *nav, "Elements", "section.elementSettings", "");
    folds->row = AddSettingsRow(*nav, elements, folds->host, {});
    FillThemeFolds(shared, nav, *folds);
}

xaml::FrameworkElement FolderRow(UiService::Shared& shared) {
    controls::Button open = MakeButton(shared, "mods.openFolder", "AMPIFY_ModsOpenFolder", false);
    std::filesystem::path kit = shared.kit;
    open.Click([kit](IInspectable const&, xaml::RoutedEventArgs const&) noexcept {
        try {
            OpenKitFolder(kit);
        } catch (...) {
            // A handler runs inside Apple Music's XAML frame: nothing may escape it.
        }
    });
    return LabelledRow(shared, i18n::Text("mods.folder"), i18n::Text("mods.folderHelp"), open);
}

}  // namespace

// Ampify's settings page: a line on what Ampify is and the way to the Marketplace, then one section per subject.
controls::StackPanel BuildAmpifyPage(UiService::Shared& shared) {
    shared.settingsCards.clear();  // the page shown until now is replaced, its cards with it
    controls::StackPanel page;
    page.Spacing(20);
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
    const UiEventSink* sink = &shared.sink;
    market.Click([model, journal, sink](IInspectable const&, xaml::RoutedEventArgs const&) noexcept {
        try {
            model->Navigate("marketplace", "/marketplace");
            // As the sidebar's entry does (Ampify.ui.navigate): the page mounts anew and reads the kit again, or it
            // opened empty after this page.
            if (*sink) (*sink)(UiEvent{"marketplace", 0, false, "/marketplace", dispatch::kPageShow});
            journal->Write("Settings: Marketplace opened from the Ampify page");
        } catch (...) {
            // A handler runs inside Apple Music's XAML frame: nothing may escape it.
        }
    });
    controls::Grid::SetColumn(market, 1);
    intro.Children().Append(market);
    page.Children().Append(intro);

    // One tab per subject, what people change most first: the look, the window, the player, the sound and the
    // extensions, then what works in the background and Ampify's own upkeep. The search looks through all of them.
    auto nav = std::make_shared<SettingsNav>();
    shared.settingsNav = nav;
    AddLookPane(shared, nav);
    AddSettingsPane(shared, *nav, "section.window");
    AddAppleTweakRows(shared, *nav, AddSettingsGroup(shared, *nav, "Window", "", ""), uirender::TweakGroup::Window);
    AddSettingsPane(shared, *nav, "section.playerTab");
    AddAppleTweakRows(shared, *nav, AddSettingsGroup(shared, *nav, "Player", "", ""), uirender::TweakGroup::Player);
    AddSettingsPane(shared, *nav, "section.soundExtensions");
    AddSoundRows(shared, *nav, AddSettingsGroup(shared, *nav, "Sound", "sound.title", ""));
    AddExtensionRows(shared, nav, AddSettingsGroup(shared, *nav, "Extensions", "mods.extensions", "mods.extensionsHelp"));
    AddSettingsPane(shared, *nav, "section.advanced");
    AddAppleTweakRows(shared, *nav,
                      AddSettingsGroup(shared, *nav, "Reliability", "section.reliability", "section.reliabilityHelp"),
                      uirender::TweakGroup::Reliability);
    controls::StackPanel helpRows = AddSettingsGroup(shared, *nav, "Help", "mods.help", "");
    AddTweakRow(shared, *nav, helpRows, uirender::KitUpdateChoice(shared.kit));
    AddSettingsRow(*nav, helpRows, FolderRow(shared), TextOf("mods.folder", "mods.folderHelp"));
    AddSettingsRow(*nav, helpRows, ReportRow(shared), TextOf("mods.report", "mods.reportHelp"));

    page.Children().Append(SettingsNavBar(shared, nav));
    page.Children().Append(SettingsPanes(shared, nav));
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
                // The old page is gone: its handler went with it.
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
                    // A handler runs inside Apple Music's XAML frame: nothing may escape it.
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

// Ampify's logo, the puzzle piece of the sidebar entry (apps/marketplace/main.js).
constexpr char kAmpifyIcon[] =
    "<svg viewBox=\"0 0 64 64\"><path d=\"M12.3 13L19 13C21.5 13 22.25 11.25 20.75 9.25C17.75 5 20.5 .75 26 .75"
    "C31.5 .75 34.25 5 31.25 9.25C29.75 11.25 30.5 13 33 13L39.7 13A11.3 11.3 0 0 1 51 24.3L51 31C51 33.5 52.75 34.25"
    " 54.75 32.75C59 29.75 63.25 32.5 63.25 38C63.25 43.5 59 46.25 54.75 43.25C52.75 41.75 51 42.5 51 45L51 51.7"
    "A11.3 11.3 0 0 1 39.7 63L12.3 63A11.3 11.3 0 0 1 1 51.7L1 24.3A11.3 11.3 0 0 1 12.3 13Z\"/></svg>";

// The index of the node named `name` (the title of one of Apple's rows, such as "PlaybackNavigationViewItem").
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
            // A handler runs inside Apple Music's XAML frame: nothing may escape it.
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
