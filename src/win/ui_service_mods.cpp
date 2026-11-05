// The rows of the installed extensions and themes (what the Marketplace installs) on the settings page, and the
// controls of their own settings (fields, sliders, switches, choices).

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

}  // namespace

// The settings an extension or the active theme declares, under its row: one line each, its label on the left and
// its control on the right.
xaml::FrameworkElement ModSettings(UiService::Shared& shared, const uirender::ModEntry& mod) {
    controls::StackPanel panel;
    panel.Spacing(8);
    panel.Margin(xaml::Thickness{16, 0, 0, 0});
    auto language = mod.translations.find(i18n::Language());
    const ManifestText* local = language == mod.translations.end() ? nullptr : &language->second;
    for (size_t index = 0; index < mod.settings.size() && index < mod.values.size(); index++) {
        const SettingSpec& spec = mod.settings[index];
        controls::Grid row;
        row.ColumnSpacing(16);
        controls::ColumnDefinition text;
        text.Width(xaml::GridLength{1.0, xaml::GridUnitType::Star});
        controls::ColumnDefinition control;
        control.Width(xaml::GridLength{1.0, xaml::GridUnitType::Auto});
        row.ColumnDefinitions().Append(text);
        row.ColumnDefinitions().Append(control);
        const std::string& shown = Localized(local, &ManifestText::settings, spec.key, spec.label);
        controls::TextBlock label = MakeText(Wide(shown).c_str(), "body", false, &shared.probe);
        label.VerticalAlignment(xaml::VerticalAlignment::Center);
        row.Children().Append(label);
        xaml::FrameworkElement input = SettingControl(shared, {mod.id, mod.theme, mod.snippet}, spec, mod.values[index], local);
        input.VerticalAlignment(xaml::VerticalAlignment::Center);
        input.HorizontalAlignment(xaml::HorizontalAlignment::Right);
        const char* prefix = mod.snippet ? "AMPIFY_SnippetSetting_" : mod.theme ? "AMPIFY_ThemeSetting_" : "AMPIFY_ModsSetting_";
        Name(input, std::string(prefix) + mod.id + "_" + spec.key,
             shown);
        controls::Grid::SetColumn(input, 1);
        row.Children().Append(input);
        panel.Children().Append(row);
    }
    return panel;
}

// The active theme's settings: its name, a line on what they do, then one row per setting.
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
