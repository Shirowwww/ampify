// The rows of the installed extensions and themes (what the Marketplace installs) on the settings page, and the
// controls of their own settings (fields, sliders, switches, choices), grouped, searchable and resettable.

#if defined(AMPIFY_WITH_XAML)

#include "ui_service_detail.hpp"

#include <shellapi.h>

#include "../core/text_util.hpp"

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

// Whose setting a control writes: an extension's, the active theme's or a snippet's, and the card it sits in.
struct SettingOwner {
    ElementRef element;
    std::weak_ptr<SettingsCard> card;
};

// What the journal adds after a setting written from the card.
std::string AfterWrite(const ElementRef& element) {
    switch (element.kind) {
        case ElementKind::Snippet: return " (snippet)";
        case ElementKind::Theme: return " (theme)";
        case ElementKind::Extension:
        case ElementKind::App: break;
    }
    return " (restart " + element.id + " requested)";
}

// The start of the automation name of a setting's control, which the bench and the inspector look for.
const char* SettingNamePrefix(ElementKind kind) {
    switch (kind) {
        case ElementKind::Snippet: return "AMPIFY_SnippetSetting_";
        case ElementKind::Theme: return "AMPIFY_ThemeSetting_";
        case ElementKind::Extension:
        case ElementKind::App: break;
    }
    return "AMPIFY_ModsSetting_";
}

// Reads the element back from settings.txt and turns each Reset on or off by it: a change made with a control shows
// on its Reset at once, without building the card again under the pointer.
void FollowValues(UiService::Shared& shared, const std::weak_ptr<SettingsCard>& weak) {
    std::shared_ptr<SettingsCard> card = weak.lock();
    if (!card) return;
    std::optional<uirender::ModEntry> now = uirender::ElementSettings(shared.kit, ToString(card->mod.kind), card->mod.id);
    if (!now) return;
    card->mod = std::move(*now);
    std::vector<bool> changed = settings_panel::ChangedSettings(card->mod.settings, card->mod.values);
    for (size_t index = 0; index < changed.size(); index++) {
        auto reset = card->resets.find(card->mod.settings[index].key);
        if (reset != card->resets.end()) reset->second.IsEnabled(changed[index]);
    }
}

// Writes one declared setting from the card (SetElementSetting).
void CommitSetting(UiService::Shared& shared, const SettingOwner& owner, const std::string& key, const std::string& value) {
    std::string error;
    const std::string& id = owner.element.id;
    if (uirender::SetElementSetting(shared.kit, owner.element, key, value, error)) {
        FollowValues(shared, owner.card);
        shared.journal.Write("Settings: \"" + id + "\" " + key + " = " + value + " from the Ampify page" +
                             AfterWrite(owner.element));
    } else {
        shared.journal.Write("Settings: could not change \"" + id + "\" " + key + ": " + error);
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
    return ShortestNumberText(number);
}

// A text or number field, written when it is left or on Enter. A number that does not read puts the last value back.
controls::TextBox SettingField(UiService::Shared& shared, const SettingOwner& target, const SettingSpec& spec,
                               const std::string& value) {
    controls::TextBox box;
    if (shared.probe.textBox) box.Style(shared.probe.textBox);
    if (spec.type == SettingSpec::Type::Number) {
        box.MinWidth(96);
    } else {
        box.Width(kSettingControlWidth);
    }
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

// A number with both bounds: a slider, so the range shows, with its value written beside it. settings.txt is written
// once the slider rests, not at each step of a drag: a theme applies again on every write.
xaml::FrameworkElement SettingSlider(UiService::Shared& shared, const SettingOwner& target, const SettingSpec& spec,
                                     const std::string& value) {
    constexpr auto kRestDelay = std::chrono::milliseconds(350);
    constexpr double kReadingWidth = 48;
    constexpr double kReadingGap = 12;
    constexpr double kSliderWidth = kSettingControlWidth - kReadingWidth - kReadingGap;  // as wide as a menu in all
    const double low = *spec.min;
    const double high = *spec.max;
    const bool whole = std::floor(low) == low && std::floor(high) == high;
    std::optional<std::string> stored = NumberIn(value, spec);
    std::optional<std::string> fallback = NumberIn(spec.defaultValue, spec);
    std::string start = ShortestNumberText(low);
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
        std::string text = ShortestNumberText(number);
        if (i18n::UsesDecimalComma()) std::replace(text.begin(), text.end(), '.', ',');
        return Wide(text);
    };
    controls::TextBlock reading = MakeText(shown(slider.Value()).c_str(), "body", false, &shared.probe);
    reading.Width(kReadingWidth);
    reading.TextAlignment(xaml::TextAlignment::Right);
    reading.VerticalAlignment(xaml::VerticalAlignment::Center);

    controls::StackPanel box;
    box.Orientation(controls::Orientation::Horizontal);
    box.Spacing(kReadingGap);
    box.Children().Append(reading);
    box.Children().Append(slider);

    dispatching::DispatcherQueueTimer rest = dispatching::DispatcherQueue::GetForCurrentThread().CreateTimer();
    rest.Interval(kRestDelay);
    rest.IsRepeating(false);
    auto last = std::make_shared<std::string>(start);
    // The value is kept aside rather than read from the slider: a weak reference to it would never resolve, and a
    // strong one in the timer would make a cycle with the slider's own handler.
    auto moved = std::make_shared<double>(slider.Value());
    UiService::Shared* owner = &shared;
    rest.Tick([owner, target, key = spec.key, last, moved](auto const&, auto const&) noexcept {
        try {
            std::string next = ShortestNumberText(*moved);
            if (next == *last) return;
            *last = next;
            CommitSetting(*owner, target, key, next);
        } catch (...) {
            // A handler runs inside Apple Music's XAML frame: nothing may escape it.
        }
    });
    slider.ValueChanged([rest, reading, shown, moved](IInspectable const&,
                                                      primitives::RangeBaseValueChangedEventArgs const& args) noexcept {
        try {
            *moved = args.NewValue();
            reading.Text(shown(args.NewValue()));
            rest.Stop();
            rest.Start();
        } catch (...) {
            // A handler runs inside Apple Music's XAML frame: nothing may escape it.
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
                // A handler runs inside Apple Music's XAML frame: nothing may escape it.
            }
        });
        return toggle;
    }
    if (spec.type == SettingSpec::Type::Choice) {
        controls::ComboBox combo;
        if (shared.probe.comboBox) combo.Style(shared.probe.comboBox);
        combo.Width(kSettingControlWidth);
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
                // A handler runs inside Apple Music's XAML frame: nothing may escape it.
            }
        });
        return combo;
    }
    if (spec.type == SettingSpec::Type::Number && spec.min && spec.max && *spec.min < *spec.max) {
        return SettingSlider(shared, target, spec, value);
    }
    return SettingField(shared, target, spec, value);
}

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

// The "Reset" of one setting: its key leaves settings.txt, so it takes the manifest default, then the card is built
// again. It is off while the setting holds its default (FollowValues keeps it so), so a click does not restart an
// extension for nothing.
controls::Button ResetButton(UiService::Shared& shared, SettingsCard& card, const SettingSpec& spec,
                             const std::string& label, bool changed, const SettingsRefresh& refresh) {
    controls::Button button;
    if (shared.probe.button) button.Style(shared.probe.button);
    button.Content(winrt::box_value(Wide(i18n::Text("settings.reset"))));
    button.VerticalAlignment(xaml::VerticalAlignment::Center);
    button.IsEnabled(changed);
    automation::AutomationProperties::SetName(button, Wide(i18n::Text("settings.reset") + " " + label));
    UiService::Shared* owner = &shared;
    button.Click([owner, kind = std::string(ToString(card.mod.kind)), id = card.mod.id, key = spec.key,
                  refresh](IInspectable const&, xaml::RoutedEventArgs const&) noexcept {
        try {
            std::string error;
            if (uirender::ResetElementValues(owner->kit, kind, id, {key}, error)) {
                owner->journal.Write("Settings: \"" + id + "\" " + key + " back to its default from the Ampify page");
            } else {
                owner->journal.Write("Settings: could not reset \"" + id + "\" " + key + ": " + error);
            }
            refresh("");
        } catch (...) {
            // A handler runs inside Apple Music's XAML frame: nothing may escape it.
        }
    });
    card.resets[spec.key] = button;
    return button;
}

// One setting: its label and description on the left, its control and Reset on the right.
xaml::FrameworkElement SettingRowOf(UiService::Shared& shared, const std::shared_ptr<SettingsCard>& card, size_t index,
                                    bool changed, const SettingsRefresh& refresh) {
    const uirender::ModEntry& mod = card->mod;
    const SettingSpec& spec = mod.settings[index];
    settings_panel::SettingText shown = ShownText(mod, spec);
    SettingOwner target{mod.Ref(), card};
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
    Name(input, std::string(SettingNamePrefix(mod.kind)) + mod.id + "_" + spec.key, shown.label);

    controls::StackPanel inputRow;
    inputRow.Orientation(controls::Orientation::Horizontal);
    inputRow.Spacing(8);
    inputRow.Children().Append(input);
    inputRow.Children().Append(ResetButton(shared, *card, spec, shown.label, changed, refresh));
    controls::Grid::SetColumn(inputRow, 1);
    row.Children().Append(inputRow);
    return row;
}

controls::TextBlock GroupHeading(UiService::Shared& shared, const uirender::ModEntry& mod, const std::string& group) {
    const std::string& shown = Localized(LocalTextOf(mod), &ManifestText::groups, group, group);
    controls::TextBlock heading = MakeText(Wide(shown).c_str(), "bodyStrong", false, &shared.probe);
    heading.Margin(xaml::Thickness{0, 8, 0, 0});
    return heading;
}

// The rows and headings of the card, group by group, in the order of the settings' groups. `targets` keeps the rows
// for the search.
std::vector<xaml::FrameworkElement> GroupedRows(UiService::Shared& shared, const std::shared_ptr<SettingsCard>& card,
                                                SearchTargets& targets, const SettingsRefresh& refresh) {
    const uirender::ModEntry& mod = card->mod;
    std::vector<bool> changed = settings_panel::ChangedSettings(mod.settings, mod.values);
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
            xaml::FrameworkElement row = SettingRowOf(shared, card, index, changed[index], refresh);
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
            // A handler runs inside Apple Music's XAML frame: nothing may escape it.
        }
    });
    return button;
}

// The line under the card's toolbar: what an export, an import or a reset did, or why it was refused.
void SayUnder(SettingsCard& card, const std::string& line) {
    if (!card.status) return;
    card.status.Text(Wide(line));
    card.status.Visibility(xaml::Visibility::Visible);
}

// Every setting back to its manifest default: their keys leave settings.txt in one write, then the card again.
void ResetAll(UiService::Shared& shared, const uirender::ModEntry& mod, const SettingsRefresh& refresh) {
    std::string error;
    if (!uirender::ResetElementValues(shared.kit, ToString(mod.kind), mod.id, {}, error)) {
        shared.journal.Write("Settings: could not reset \"" + mod.id + "\": " + error);
        refresh("");
        return;
    }
    std::string done = i18n::Text("settings.resetDone", {{"id", mod.id}});
    shared.journal.Write(done);
    refresh(done);
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
            // A handler runs inside Apple Music's XAML frame: nothing may escape it.
        }
    });
    search.Text(Wide(*query));
    return search;
}

// The line above the settings: the search (when there are enough), then Reset all, Export and Import. The line under
// them shows the result of a reset all, an export or an import, or why it was refused.
controls::StackPanel SettingsToolbar(UiService::Shared& shared, const std::shared_ptr<SettingsCard>& card,
                                     const SettingsRefresh& refresh, const std::shared_ptr<SearchTargets>& targets,
                                     const std::shared_ptr<std::string>& query) {
    const uirender::ModEntry& mod = card->mod;
    // Made first: the search box applies its text to the rows, and shows this line when nothing matches.
    targets->none = MakeText(Wide(i18n::Text("settings.noMatch")).c_str(), "caption", true, &shared.probe);
    targets->none.Visibility(xaml::Visibility::Collapsed);
    controls::TextBlock status = MakeText(L"", "caption", true, &shared.probe);
    status.Visibility(xaml::Visibility::Collapsed);
    card->status = status;
    SettingsSay say = [weak = std::weak_ptr<SettingsCard>(card)](const std::string& line) {
        if (std::shared_ptr<SettingsCard> held = weak.lock()) SayUnder(*held, line);
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

// The card of one element: its toolbar, then its settings. Built again in the same panel after a reset or an import,
// from what settings.txt holds, with the same search text, and then `notice` under the toolbar when there is one.
void FillSettings(UiService::Shared& shared, const std::shared_ptr<SettingsCard>& card, uirender::ModEntry mod,
                  const std::shared_ptr<std::string>& query) {
    card->panel.Children().Clear();
    card->status = nullptr;
    card->resets.clear();
    card->mod = std::move(mod);
    SettingsRefresh refresh = [owner = &shared, weak = std::weak_ptr<SettingsCard>(card),
                               kind = std::string(ToString(card->mod.kind)), id = card->mod.id, query](const std::string& notice) {
        // Next turn of the interface: the button that asked for it is still in its own click.
        dispatching::DispatcherQueue::GetForCurrentThread().TryEnqueue([owner, weak, kind, id, query, notice]() {
            try {
                std::shared_ptr<SettingsCard> live = weak.lock();
                std::optional<uirender::ModEntry> fresh = uirender::ElementSettings(owner->kit, kind, id);
                if (!live || !fresh) return;
                FillSettings(*owner, live, std::move(*fresh), query);
                if (!notice.empty()) SayUnder(*live, notice);
            } catch (...) {
                // The card left the tree meanwhile: the rows are not rebuilt.
            }
        });
    };
    auto targets = std::make_shared<SearchTargets>();
    std::vector<xaml::FrameworkElement> body = GroupedRows(shared, card, *targets, refresh);
    card->panel.Children().Append(SettingsToolbar(shared, card, refresh, targets, query));
    for (const xaml::FrameworkElement& element : body) card->panel.Children().Append(element);
    // The search box gets its text back, but its TextChanged did not filter the new rows (seen on screen): filter here.
    if (!query->empty()) ApplySearch(*targets, *query);
}

}  // namespace

std::vector<settings_panel::SettingText> ElementSettingTexts(const uirender::ModEntry& mod) {
    std::vector<settings_panel::SettingText> texts;
    for (const SettingSpec& spec : mod.settings) texts.push_back(ShownText(mod, spec));
    return texts;
}

// The settings an extension or the active theme declares, under its row: a toolbar, then the settings by group, each
// with its label, its description and its controls.
xaml::FrameworkElement ModSettings(UiService::Shared& shared, const uirender::ModEntry& mod) {
    auto card = std::make_shared<SettingsCard>();
    card->panel = controls::StackPanel();
    card->panel.Spacing(8);
    card->panel.Margin(xaml::Thickness{16, 0, 0, 0});
    // A card replaces only the one of the same element (its theme section built again). Parent() cannot tell whether
    // a card is still in the page: WinUI gives null for an element not yet connected to the window, as the whole page
    // is while it is built, so each new card dropped the others and only the last one built kept working.
    std::string_view kind = ToString(mod.kind);
    std::erase_if(shared.settingsCards, [&](const std::shared_ptr<SettingsCard>& old) {
        return old->mod.id == mod.id && ToString(old->mod.kind) == kind;
    });
    shared.settingsCards.push_back(card);
    FillSettings(shared, card, mod, std::make_shared<std::string>());
    return card->panel;
}

}  // namespace ampify::win::ui_service_detail

#endif  // AMPIFY_WITH_XAML
