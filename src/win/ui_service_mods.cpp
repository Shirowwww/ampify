// The rows of the installed extensions and themes (what the Marketplace installs) on the settings page, and the
// controls of their own settings (fields, sliders, switches, choices), grouped, searchable and resettable.

#if defined(AMPIFY_WITH_XAML)

#include "ui_service_detail.hpp"

#include <shellapi.h>

namespace ampify::win::ui_service_detail {

namespace {

int g_modsUpdating = 0;                 // interface thread only: > 0 while the Mods card sets its toggles itself

}  // namespace

void OpenKitFolder(const std::filesystem::path& kit) {
    ShellExecuteW(nullptr, L"open", kit.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

xaml::FrameworkElement ModRow(UiService::Shared& shared, const uirender::ModEntry& mod) {
    controls::Grid row;
    row.ColumnSpacing(16);
    controls::ColumnDefinition text;
    text.Width(xaml::GridLength{1.0, xaml::GridUnitType::Star});
    controls::ColumnDefinition toggleColumn;
    toggleColumn.Width(xaml::GridLength{1.0, xaml::GridUnitType::Auto});
    row.ColumnDefinitions().Append(text);
    row.ColumnDefinitions().Append(toggleColumn);

    controls::StackPanel labels;
    labels.VerticalAlignment(xaml::VerticalAlignment::Center);
    // The manifest's translation for Windows' language, else its English name and description.
    const std::string& shownName = mod.NameIn(i18n::Language());
    const std::string& shownDescription = mod.DescriptionIn(i18n::Language());
    labels.Children().Append(MakeText(Wide(shownName).c_str(), "body", false, &shared.probe));
    if (!shownDescription.empty()) labels.Children().Append(MakeText(Wide(shownDescription).c_str(), "caption", true, &shared.probe));
    row.Children().Append(labels);

    controls::ToggleSwitch toggle;
    if (shared.probe.toggle) toggle.Style(shared.probe.toggle);
    toggle.MinWidth(0);
    // The states in Ampify's language (Apple Music's own words in French), not in whatever Windows would pick.
    toggle.OnContent(winrt::box_value(Wide(i18n::Text("ui.on"))));
    toggle.OffContent(winrt::box_value(Wide(i18n::Text("ui.off"))));
    toggle.VerticalAlignment(xaml::VerticalAlignment::Center);
    toggle.HorizontalAlignment(xaml::HorizontalAlignment::Right);
    Name(toggle, "AMPIFY_ModsToggle_" + mod.id, shownName);
    automation::AutomationProperties::SetHelpText(toggle, Wide(i18n::Text("mods.toggleHelp")));
    g_modsUpdating++;
    toggle.IsOn(mod.enabled);
    g_modsUpdating--;
    controls::Grid::SetColumn(toggle, 1);
    row.Children().Append(toggle);

    Journal* journal = &shared.journal;
    std::filesystem::path kit = shared.kit;
    std::string id = mod.id;
    toggle.Toggled([journal, kit, id](IInspectable const& sender, xaml::RoutedEventArgs const&) noexcept {
        try {
            if (g_modsUpdating > 0) return;
            auto control = sender.as<controls::ToggleSwitch>();
            bool on = control.IsOn();
            std::string error;
            if (uirender::SwitchMod(kit, id, on, error)) {
                journal->Write("Settings: extension \"" + id + "\" " + (on ? "turned on" : "turned off") +
                               " from the Ampify page (" + (on ? "start " : "stop ") + id + " requested)");
            } else {
                journal->Write("Settings: could not change extension \"" + id + "\": " + error);
                g_modsUpdating++;
                control.IsOn(!on);
                g_modsUpdating--;
            }
        } catch (...) {
            g_modsUpdating = 0;
        }
    });
    return row;
}

namespace {

// Whose setting a control writes: an extension's, or the active theme's.
struct SettingOwner {
    std::string id;
    bool theme = false;
    bool snippet = false;
};

// Writes one declared setting from the card. An extension restarts with it (SetModSetting); the theme service sees
// settings.txt change and applies the theme and the snippets again (SetThemeSetting, SetSnippetSetting).
void CommitSetting(UiService::Shared& shared, const SettingOwner& owner, const std::string& key, const std::string& value) {
    std::string error;
    bool written = owner.snippet ? uirender::SetSnippetSetting(shared.kit, owner.id, key, value, error)
                   : owner.theme ? uirender::SetThemeSetting(shared.kit, owner.id, key, value, error)
                                 : uirender::SetModSetting(shared.kit, owner.id, key, value, error);
    if (written) {
        shared.journal.Write("Settings: \"" + owner.id + "\" " + key + " = " + value + " from the Ampify page" +
                             (owner.snippet ? std::string(" (snippet)")
                              : owner.theme ? std::string(" (theme)")
                                            : " (restart " + owner.id + " requested)"));
    } else {
        shared.journal.Write("Settings: could not change \"" + owner.id + "\" " + key + ": " + error);
    }
}

// A number typed in a field: the text when it reads as one within the bounds, else nothing. A comma counts as the
// decimal point, as French people type it.
std::optional<std::string> NumberIn(std::string text, const SettingSpec& spec) {
    std::replace(text.begin(), text.end(), ',', '.');
    double number = 0;
    const char* end = text.data() + text.size();
    auto [stop, error] = std::from_chars(text.data(), end, number);
    if (text.empty() || error != std::errc() || stop != end || !std::isfinite(number)) return std::nullopt;
    if ((spec.min && number < *spec.min) || (spec.max && number > *spec.max)) return std::nullopt;
    return SettingNumberText(number);
}

// A text or number field, written when it is left or on Enter. A number that does not read puts the last value back.
controls::TextBox SettingField(UiService::Shared& shared, const SettingOwner& target, const SettingSpec& spec,
                               const std::string& value) {
    controls::TextBox box;
    if (shared.probe.textBox) box.Style(shared.probe.textBox);
    box.MinWidth(spec.type == SettingSpec::Type::Number ? 96 : 200);
    box.MaxLength(200);  // the length docs/extensions/manifest.md allows a text default
    // settings.txt keeps a point; a French or German reader sees a comma, like the labels' "0,3 à 0,9".
    auto shown = [number = spec.type == SettingSpec::Type::Number](std::string text) {
        if (number && i18n::UsesDecimalComma()) std::replace(text.begin(), text.end(), '.', ',');
        return Wide(text);
    };
    box.Text(shown(value));
    auto last = std::make_shared<std::string>(value);
    UiService::Shared* owner = &shared;
    auto commit = [owner, target, spec, last, shown](const controls::TextBox& field) {
        std::string typed = Utf8(field.Text());
        std::optional<std::string> next = spec.type == SettingSpec::Type::Number ? NumberIn(typed, spec) : typed;
        if (!next) {
            field.Text(shown(*last));
            return;
        }
        if (*next == *last) return;
        *last = *next;
        CommitSetting(*owner, target, spec.key, *next);
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

// A number with both bounds: a slider, so the range shows, with its value written beside it. settings.txt is written
// once the slider rests, not at each step of a drag: a theme applies again on every write.
xaml::FrameworkElement SettingSlider(UiService::Shared& shared, const SettingOwner& target, const SettingSpec& spec,
                                     const std::string& value) {
    constexpr auto kRestDelay = std::chrono::milliseconds(350);
    constexpr double kSliderWidth = 200;
    const double low = *spec.min;
    const double high = *spec.max;
    const bool whole = std::floor(low) == low && std::floor(high) == high;
    std::optional<std::string> stored = NumberIn(value, spec);
    std::optional<std::string> fallback = NumberIn(spec.defaultValue, spec);
    std::string start = SettingNumberText(low);
    if (stored) {
        start = *stored;
    } else if (fallback) {
        start = *fallback;
    }

    controls::Slider slider;
    if (shared.probe.slider) slider.Style(shared.probe.slider);
    slider.Minimum(low);
    slider.Maximum(high);
    slider.StepFrequency(whole ? 1.0 : (high - low) / 100);
    slider.Width(kSliderWidth);
    slider.Value(std::strtod(start.c_str(), nullptr));
    slider.VerticalAlignment(xaml::VerticalAlignment::Center);

    // A point in settings.txt, a comma for a French or German reader, as in SettingField.
    auto shown = [](double number) {
        std::string text = SettingNumberText(number);
        if (i18n::UsesDecimalComma()) std::replace(text.begin(), text.end(), '.', ',');
        return Wide(text);
    };
    controls::TextBlock reading = MakeText(shown(slider.Value()).c_str(), "body", false, &shared.probe);
    reading.MinWidth(32);
    reading.TextAlignment(xaml::TextAlignment::Right);
    reading.VerticalAlignment(xaml::VerticalAlignment::Center);

    controls::StackPanel box;
    box.Orientation(controls::Orientation::Horizontal);
    box.Spacing(12);
    box.Children().Append(reading);
    box.Children().Append(slider);

    dispatching::DispatcherQueueTimer rest = dispatching::DispatcherQueue::GetForCurrentThread().CreateTimer();
    rest.Interval(kRestDelay);
    rest.IsRepeating(false);
    auto last = std::make_shared<std::string>(start);
    UiService::Shared* owner = &shared;
    winrt::weak_ref<controls::Slider> weakSlider = winrt::make_weak(slider);
    rest.Tick([owner, target, key = spec.key, last, weakSlider](auto const&, auto const&) noexcept {
        try {
            controls::Slider live = weakSlider.get();
            if (!live) return;
            std::string next = SettingNumberText(live.Value());
            if (next == *last) return;
            *last = next;
            CommitSetting(*owner, target, key, next);
        } catch (...) {
        }
    });
    slider.ValueChanged([rest, reading, shown](IInspectable const&,
                                               primitives::RangeBaseValueChangedEventArgs const& args) noexcept {
        try {
            reading.Text(shown(args.NewValue()));
            rest.Stop();
            rest.Start();
        } catch (...) {
        }
    });
    return box;
}

// The text of `key` in one list of the manifest's translation (`local`, null when there is none), else `fallback`.
const std::string& Localized(const ManifestText* local, std::map<std::string, std::string> ManifestText::*list,
                             const std::string& key, const std::string& fallback) {
    if (!local) return fallback;
    auto found = (local->*list).find(key);
    return found == (local->*list).end() ? fallback : found->second;
}

// `local` is the manifest's text in the interface's language, or null: a choice shows its options' translated texts
// and still stores their values.
xaml::FrameworkElement SettingControl(UiService::Shared& shared, const SettingOwner& target, const SettingSpec& spec,
                                      const std::string& value, const ManifestText* local) {
    UiService::Shared* owner = &shared;
    if (spec.type == SettingSpec::Type::Toggle) {
        controls::ToggleSwitch toggle;
        if (shared.probe.toggle) toggle.Style(shared.probe.toggle);
        toggle.MinWidth(0);
        toggle.OnContent(winrt::box_value(Wide(i18n::Text("ui.on"))));
        toggle.OffContent(winrt::box_value(Wide(i18n::Text("ui.off"))));
        toggle.IsOn(value == "true" || value == "1" || value == "on" || value == "yes");
        toggle.Toggled([owner, target, key = spec.key](IInspectable const& sender, xaml::RoutedEventArgs const&) noexcept {
            try {
                CommitSetting(*owner, target, key, sender.as<controls::ToggleSwitch>().IsOn() ? "true" : "false");
            } catch (...) {
            }
        });
        return toggle;
    }
    if (spec.type == SettingSpec::Type::Choice) {
        controls::ComboBox combo;
        if (shared.probe.comboBox) combo.Style(shared.probe.comboBox);
        combo.MinWidth(200);
        for (const std::string& option : spec.options) {
            combo.Items().Append(winrt::box_value(Wide(Localized(local, &ManifestText::options, option, option))));
        }
        auto chosen = std::find(spec.options.begin(), spec.options.end(), value);
        if (chosen == spec.options.end()) chosen = std::find(spec.options.begin(), spec.options.end(), spec.defaultValue);
        combo.SelectedIndex(static_cast<int32_t>(chosen - spec.options.begin()));
        combo.SelectionChanged([owner, target, key = spec.key, options = spec.options](
                                   IInspectable const& sender, controls::SelectionChangedEventArgs const&) noexcept {
            try {
                int32_t index = sender.as<controls::ComboBox>().SelectedIndex();
                if (index >= 0 && index < static_cast<int32_t>(options.size())) CommitSetting(*owner, target, key, options[index]);
            } catch (...) {
            }
        });
        return combo;
    }
    if (spec.type == SettingSpec::Type::Number && spec.min && spec.max && *spec.min < *spec.max) {
        return SettingSlider(shared, target, spec, value);
    }
    return SettingField(shared, target, spec, value);
}

// The manifest's translation in the interface's language, or null when there is none.
const ManifestText* LocalTextOf(const uirender::ModEntry& mod) {
    auto language = mod.translations.find(i18n::Language());
    return language == mod.translations.end() ? nullptr : &language->second;
}

// The label and description of a setting in the language of the card, each English when no translation has it.
settings_panel::SettingText ShownText(const uirender::ModEntry& mod, const SettingSpec& spec) {
    const ManifestText* local = LocalTextOf(mod);
    return {Localized(local, &ManifestText::settings, spec.key, spec.label),
            Localized(local, &ManifestText::settingDescriptions, spec.key, spec.description)};
}

inline xaml::Visibility Show(bool shown) {
    return shown ? xaml::Visibility::Visible : xaml::Visibility::Collapsed;
}

// What the search and the card's refresh work on: each row with its text and group, and each group's heading (null
// for the settings that have no group).
struct SettingRow {
    xaml::FrameworkElement element{nullptr};
    settings_panel::SettingText text;
    size_t group = 0;
};
struct SearchTargets {
    std::vector<SettingRow> rows;
    std::vector<xaml::FrameworkElement> headings;  // by group
    xaml::FrameworkElement none{nullptr};          // the line shown when nothing matches
};

// Shows the rows that match `query`, and the headings of the groups that still have one.
void ApplySearch(SearchTargets& targets, const std::string& query) {
    std::vector<settings_panel::SettingText> texts;
    for (const SettingRow& row : targets.rows) texts.push_back(row.text);
    std::vector<size_t> matches = settings_panel::MatchingSettings(texts, query);
    std::vector<bool> rowShown(targets.rows.size(), false);
    std::vector<bool> groupShown(targets.headings.size(), false);
    for (size_t index : matches) {
        rowShown[index] = true;
        groupShown[targets.rows[index].group] = true;
    }
    for (size_t index = 0; index < targets.rows.size(); index++) targets.rows[index].element.Visibility(Show(rowShown[index]));
    for (size_t index = 0; index < targets.headings.size(); index++) {
        if (targets.headings[index]) targets.headings[index].Visibility(Show(groupShown[index]));
    }
    if (targets.none) targets.none.Visibility(Show(matches.empty() && !query.empty()));
}

// The "Reset" of one setting: the manifest default is written, then the card is built again from settings.txt. It is
// off while the setting already holds its default, so a click does not restart an extension for nothing.
controls::Button ResetButton(UiService::Shared& shared, const SettingOwner& target, const SettingSpec& spec,
                             const std::string& label, bool changed, const std::function<void()>& refresh) {
    controls::Button button;
    if (shared.probe.button) button.Style(shared.probe.button);
    button.Content(winrt::box_value(Wide(i18n::Text("settings.reset"))));
    button.VerticalAlignment(xaml::VerticalAlignment::Center);
    button.IsEnabled(changed);
    automation::AutomationProperties::SetName(button, Wide(i18n::Text("settings.reset") + " " + label));
    UiService::Shared* owner = &shared;
    button.Click([owner, target, key = spec.key, value = spec.defaultValue, refresh](IInspectable const&,
                                                                                    xaml::RoutedEventArgs const&) noexcept {
        try {
            CommitSetting(*owner, target, key, value);
            refresh();
        } catch (...) {
        }
    });
    return button;
}

// One setting: its label and description on the left, its control and Reset on the right.
xaml::FrameworkElement SettingRowOf(UiService::Shared& shared, const uirender::ModEntry& mod, size_t index,
                                    const std::function<void()>& refresh) {
    const SettingSpec& spec = mod.settings[index];
    settings_panel::SettingText shown = ShownText(mod, spec);
    SettingOwner target{mod.id, mod.theme, mod.snippet};
    controls::Grid row;
    row.ColumnSpacing(16);
    controls::ColumnDefinition text;
    text.Width(xaml::GridLength{1.0, xaml::GridUnitType::Star});
    controls::ColumnDefinition control;
    control.Width(xaml::GridLength{1.0, xaml::GridUnitType::Auto});
    row.ColumnDefinitions().Append(text);
    row.ColumnDefinitions().Append(control);

    controls::StackPanel labels;
    labels.VerticalAlignment(xaml::VerticalAlignment::Center);
    labels.Children().Append(MakeText(Wide(shown.label).c_str(), "body", false, &shared.probe));
    if (!shown.description.empty()) {
        labels.Children().Append(MakeText(Wide(shown.description).c_str(), "caption", true, &shared.probe));
    }
    row.Children().Append(labels);

    xaml::FrameworkElement input = SettingControl(shared, target, spec, mod.values[index], LocalTextOf(mod));
    input.VerticalAlignment(xaml::VerticalAlignment::Center);
    input.HorizontalAlignment(xaml::HorizontalAlignment::Right);
    const char* prefix = mod.snippet ? "AMPIFY_SnippetSetting_" : mod.theme ? "AMPIFY_ThemeSetting_" : "AMPIFY_ModsSetting_";
    Name(input, std::string(prefix) + mod.id + "_" + spec.key, shown.label);

    controls::StackPanel inputRow;
    inputRow.Orientation(controls::Orientation::Horizontal);
    inputRow.Spacing(8);
    inputRow.Children().Append(input);
    inputRow.Children().Append(ResetButton(shared, target, spec, shown.label, mod.values[index] != spec.defaultValue, refresh));
    controls::Grid::SetColumn(inputRow, 1);
    row.Children().Append(inputRow);
    return row;
}

// The heading of a group, in the language of the card.
controls::TextBlock GroupHeading(UiService::Shared& shared, const uirender::ModEntry& mod, const std::string& group) {
    const std::string& shown = Localized(LocalTextOf(mod), &ManifestText::groups, group, group);
    controls::TextBlock heading = MakeText(Wide(shown).c_str(), "bodyStrong", false, &shared.probe);
    heading.Margin(xaml::Thickness{0, 8, 0, 0});
    return heading;
}

// The rows and headings of the card, group by group, in the order of the settings' groups. `targets` keeps the rows
// for the search.
std::vector<xaml::FrameworkElement> GroupedRows(UiService::Shared& shared, const uirender::ModEntry& mod,
                                                SearchTargets& targets, const std::function<void()>& refresh) {
    std::vector<xaml::FrameworkElement> body;
    for (const settings_panel::SettingGroup& group : settings_panel::GroupSettings(mod.settings)) {
        size_t groupIndex = targets.headings.size();
        xaml::FrameworkElement heading{nullptr};
        if (!group.name.empty()) {
            heading = GroupHeading(shared, mod, group.name);
            body.push_back(heading);
        }
        targets.headings.push_back(heading);
        for (size_t index : group.settings) {
            if (index >= mod.values.size()) continue;
            xaml::FrameworkElement row = SettingRowOf(shared, mod, index, refresh);
            body.push_back(row);
            targets.rows.push_back({row, ShownText(mod, mod.settings[index]), groupIndex});
        }
    }
    return body;
}

// A small button of the toolbar; its action runs on the interface thread.
controls::Button ToolButton(UiService::Shared& shared, const char* textKey, std::function<void()> action) {
    controls::Button button;
    if (shared.probe.button) button.Style(shared.probe.button);
    button.Content(winrt::box_value(Wide(i18n::Text(textKey))));
    button.Click([action](IInspectable const&, xaml::RoutedEventArgs const&) noexcept {
        try {
            action();
        } catch (...) {
        }
    });
    return button;
}

// Every setting back to its manifest default, in one write, then the card again.
void ResetAll(UiService::Shared& shared, const uirender::ModEntry& mod, const std::function<void()>& refresh) {
    std::string error;
    if (uirender::ChangeElementValues(shared.kit, SettingKindOf(mod), mod.id, settings_panel::DefaultValues(mod.settings), error)) {
        shared.journal.Write(i18n::Text("settings.resetDone", {{"id", mod.id}}));
    } else {
        shared.journal.Write("Settings: could not reset \"" + mod.id + "\": " + error);
    }
    refresh();
}

// A card with fewer settings than this has no search box: there is little to find.
constexpr size_t kSearchFromSettings = 6;

// The search box. `query` keeps what was typed, so a card built again after a change shows the same search.
controls::TextBox SearchBox(UiService::Shared& shared, const std::shared_ptr<SearchTargets>& targets,
                            const std::shared_ptr<std::string>& query) {
    controls::TextBox search;
    if (shared.probe.textBox) search.Style(shared.probe.textBox);
    search.MinWidth(200);
    search.PlaceholderText(Wide(i18n::Text("settings.search")));
    automation::AutomationProperties::SetName(search, Wide(i18n::Text("settings.search")));
    search.TextChanged([targets, query](IInspectable const& sender, controls::TextChangedEventArgs const&) noexcept {
        try {
            *query = Utf8(sender.as<controls::TextBox>().Text());
            ApplySearch(*targets, *query);
        } catch (...) {
        }
    });
    search.Text(Wide(*query));
    return search;
}

// The line above the settings: the search (when there are enough), then Reset all, Export and Import. The line under
// them shows the result of an export or an import.
controls::StackPanel SettingsToolbar(UiService::Shared& shared, const uirender::ModEntry& mod,
                                     const std::shared_ptr<SettingsCard>& card,
                                     const std::function<void()>& refresh, const std::shared_ptr<SearchTargets>& targets,
                                     const std::shared_ptr<std::string>& query) {
    // Made first: the search box applies its text to the rows, and shows this line when nothing matches.
    targets->none = MakeText(Wide(i18n::Text("settings.noMatch")).c_str(), "caption", true, &shared.probe);
    targets->none.Visibility(xaml::Visibility::Collapsed);
    controls::TextBlock status = MakeText(L"", "caption", true, &shared.probe);
    status.Visibility(xaml::Visibility::Collapsed);
    card->status = status;
    SettingsSay say = [weak = std::weak_ptr<SettingsCard>(card)](const std::string& line) {
        std::shared_ptr<SettingsCard> held = weak.lock();
        if (!held || !held->status) return;
        held->status.Text(Wide(line));
        held->status.Visibility(xaml::Visibility::Visible);
    };

    controls::StackPanel buttons;
    buttons.Orientation(controls::Orientation::Horizontal);
    buttons.Spacing(8);
    if (mod.settings.size() >= kSearchFromSettings) buttons.Children().Append(SearchBox(shared, targets, query));
    buttons.Children().Append(ToolButton(shared, "settings.resetAll", [&shared, mod, refresh] { ResetAll(shared, mod, refresh); }));
    buttons.Children().Append(ToolButton(shared, "settings.export", [&shared, mod, say] { ExportElementSettings(shared, mod, say); }));
    buttons.Children().Append(ToolButton(shared, "settings.import", [&shared, mod, refresh, say] {
        ImportElementSettings(shared, mod, refresh, say);
    }));

    controls::StackPanel bar;
    bar.Spacing(8);
    bar.Children().Append(buttons);
    bar.Children().Append(targets->none);
    bar.Children().Append(status);
    return bar;
}

// The card of one element: its toolbar, then its settings. Built again after a change, from what settings.txt holds, and
// with the same search text.
void FillSettings(UiService::Shared& shared, const std::shared_ptr<SettingsCard>& card, const uirender::ModEntry& mod,
                  const std::shared_ptr<std::string>& query) {
    card->panel.Children().Clear();
    card->status = nullptr;
    std::function<void()> refresh = [owner = &shared, weak = std::weak_ptr<SettingsCard>(card),
                                     kind = std::string(SettingKindOf(mod)), id = mod.id, query]() {
        // Next turn of the interface: the button that asked for it is still in its own click.
        dispatching::DispatcherQueue::GetForCurrentThread().TryEnqueue([owner, weak, kind, id, query]() {
            try {
                std::shared_ptr<SettingsCard> live = weak.lock();
                std::optional<uirender::ModEntry> fresh = uirender::ElementSettings(owner->kit, kind, id);
                if (live && fresh) FillSettings(*owner, live, *fresh, query);
            } catch (...) {
            }
        });
    };
    auto targets = std::make_shared<SearchTargets>();
    std::vector<xaml::FrameworkElement> body = GroupedRows(shared, mod, *targets, refresh);
    card->panel.Children().Append(SettingsToolbar(shared, mod, card, refresh, targets, query));
    for (const xaml::FrameworkElement& element : body) card->panel.Children().Append(element);
}

}  // namespace

// The settings an extension or the active theme declares, under its row: a toolbar, then the settings by group, each
// with its label, its description and its controls.
xaml::FrameworkElement ModSettings(UiService::Shared& shared, const uirender::ModEntry& mod) {
    auto card = std::make_shared<SettingsCard>();
    card->panel = controls::StackPanel();
    card->panel.Spacing(8);
    card->panel.Margin(xaml::Thickness{16, 0, 0, 0});
    std::erase_if(shared.settingsCards, [](const std::shared_ptr<SettingsCard>& old) { return !old->panel.Parent(); });
    shared.settingsCards.push_back(card);
    FillSettings(shared, card, mod, std::make_shared<std::string>());
    return card->panel;
}

// The active theme's settings: its name, a line on what they do, then the settings.
xaml::FrameworkElement ThemeSettingsSection(UiService::Shared& shared, const uirender::ModEntry& theme) {
    controls::StackPanel section;
    section.Spacing(8);
    Name(section, (theme.snippet ? "AMPIFY_SnippetSettings_" : "AMPIFY_ThemeSettings_") + theme.id,
         theme.NameIn(i18n::Language()));
    controls::TextBlock title =
        MakeText(Wide(i18n::Text(theme.snippet ? "mods.snippetTitle" : "mods.themeTitle",
                                 {{"name", theme.NameIn(i18n::Language())}}))
                     .c_str(),
                 "bodyStrong", false, &shared.probe);
    section.Children().Append(title);
    section.Children().Append(MakeText(Wide(i18n::Text(theme.snippet ? "mods.snippetSubtitle" : "mods.themeSubtitle")).c_str(),
                                       "caption", true, &shared.probe));
    section.Children().Append(ModSettings(shared, theme));
    return section;
}

}  // namespace ampify::win::ui_service_detail

#endif  // AMPIFY_WITH_XAML
