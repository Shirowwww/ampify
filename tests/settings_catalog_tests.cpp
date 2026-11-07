// The settings catalogue (src/core/settings_catalog.hpp): its rows agree with the permissions, the texts of the
// settings page, the remote's default port and docs/cli.md; and the readers built on it.

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "../src/core/element_ref.hpp"
#include "../src/core/i18n.hpp"
#include "../src/core/permissions.hpp"
#include "../src/core/remote_http.hpp"
#include "../src/core/settings.hpp"
#include "../src/core/settings_catalog.hpp"
#include "support.hpp"
#include "test.hpp"

namespace {

using ampify::KitSetting;
using ampify::Settings;
using ampify::SettingKind;

std::string ReadSource(const char* relative) {
    std::ifstream in(std::filesystem::path(AMPIFY_SOURCE_DIR) / relative, std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}

// The texts between backticks of one table cell.
std::vector<std::string> Quoted(const std::string& cell) {
    std::vector<std::string> quoted;
    for (size_t open = cell.find('`'); open != std::string::npos; open = cell.find('`', open + 1)) {
        size_t close = cell.find('`', open + 1);
        if (close == std::string::npos) break;
        quoted.push_back(cell.substr(open + 1, close - open - 1));
        open = close;
    }
    return quoted;
}

std::vector<std::string> Cells(const std::string& row) {
    std::vector<std::string> cells;
    std::string cell;
    std::istringstream in(row.substr(1));
    while (std::getline(in, cell, '|')) cells.push_back(cell);
    return cells;
}

// One row of the "## Settings" table of docs/cli.md: its keys (".resources" after "theme.parts.colors" is
// "theme.parts.resources"), its value column and its default column.
struct DocRow {
    std::vector<std::string> keys;
    std::string values;
    std::string defaults;
};

std::vector<DocRow> DocumentedSettings() {
    std::string doc = ReadSource("docs/cli.md");
    std::erase(doc, '\r');  // a checkout may have CRLF lines
    size_t start = doc.find("\n## Settings\n");
    CHECK(start != std::string::npos);
    if (start == std::string::npos) return {};
    size_t end = doc.find("\n## ", start + 1);
    std::istringstream table(doc.substr(start, end - start));
    std::vector<DocRow> rows;
    for (std::string line; std::getline(table, line);) {
        if (line.rfind("| `", 0) != 0) continue;
        std::vector<std::string> cells = Cells(line);
        if (cells.size() < 3) continue;
        DocRow row{{}, cells[1], cells[2]};
        for (const std::string& key : Quoted(cells[0])) {
            bool suffix = !key.empty() && key.front() == '.' && !row.keys.empty();
            row.keys.push_back(suffix ? row.keys.back().substr(0, row.keys.back().rfind('.')) + key : key);
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

}  // namespace

TEST_CASE(SettingsCatalog_EveryChoiceDefaultsToOneOfItsValuesAndHasItsTexts) {
    auto known = [](std::string_view key) { return ampify::i18n::Lookup(ampify::i18n::KitMessages(), key).has_value(); };
    for (const KitSetting& row : ampify::FixedKitSettings()) {
        if (row.kind != SettingKind::Choice) continue;
        CHECK(ampify::IsOfferedValue(row.key, row.fallback));
        CHECK(known(row.labelKey) && known(row.helpKey));
        for (const auto& option : row.options) CHECK(known(option.labelKey));
    }
}

TEST_CASE(SettingsCatalog_EveryServiceOfAPermissionIsAnOffSwitch) {
    for (const ampify::PermissionInfo& permission : ampify::kPermissions) {
        if (permission.serviceSetting.empty()) continue;
        const KitSetting* row = ampify::FindKitSetting(permission.serviceSetting);
        CHECK(row && row->key == permission.serviceSetting && row->kind == SettingKind::Toggle && row->fallback == "0");
    }
}

TEST_CASE(SettingsCatalog_TheRemotePortDefaultsToTheServersPort) {
    CHECK(ampify::FindKitSetting("remote.port")->fallback == std::to_string(ampify::remote::kDefaultPort));
    CHECK(ampify::IntegerOf("remote.port", std::nullopt) == ampify::remote::kDefaultPort);
    CHECK(ampify::IntegerOf("remote.port", "9000") == 9000);
    CHECK(ampify::IntegerOf("remote.port", "80") == ampify::remote::kDefaultPort);  // below 1024, as the CLI refuses
    CHECK(ampify::IntegerOf("midi.device", "256") == 0);
    CHECK(ampify::IntegerOf("midi.device", "3x") == 0);
    CHECK(ampify::IntegerOf("midi.device", "") == 0);
}

TEST_CASE(SettingsCatalog_FamiliesSwitchTheirElementsAndHoldTheirSettings) {
    CHECK(ampify::KindOfSetting("extensions.hello.enabled") == SettingKind::Toggle);
    CHECK(ampify::KindOfSetting("snippets.glow.enabled") == SettingKind::Toggle);
    CHECK(ampify::KindOfSetting("extensions.hello.greeting") == SettingKind::Text);
    CHECK(ampify::KindOfSetting("themes.night.enabled") == SettingKind::Text);
    CHECK(ampify::KindOfSetting("theme.parts.colors") == SettingKind::Toggle);
    CHECK(ampify::KindOfSetting("apple.close") == SettingKind::Choice);
    CHECK(!ampify::KindOfSetting("extensions."));
    CHECK(!ampify::KindOfSetting("not.a.key"));
}

TEST_CASE(SettingsCatalog_ChoicesReadInAnyCaseAndFallBackToTheirDefault) {
    Settings settings;
    CHECK(ampify::ChoiceOf(settings, "apple.orphanAgent") == "off");
    settings.Set("apple.orphanAgent", "On");
    CHECK(ampify::ChoiceOf(settings, "apple.orphanAgent") == "on");
    settings.Set("apple.orphanAgent", "true");
    CHECK(ampify::ChoiceOf(settings, "apple.orphanAgent") == "off");
    CHECK(ampify::IsOfferedValue("apple.close", "tray") && !ampify::IsOfferedValue("apple.close", "Tray"));
    CHECK(ampify::OfferedValues("kit_update") == "auto, ask, off");
    CHECK(ampify::OfferedValues("remote.port").empty());
}

TEST_CASE(SettingsCatalog_NoneInAnyCaseIsNoTheme) {
    Settings settings;
    CHECK(ampify::ActiveThemeId(settings).empty());
    settings.Set("theme.active", "None");
    CHECK(ampify::ActiveThemeId(settings).empty());
    settings.Set("theme.active", "night");
    CHECK(ampify::ActiveThemeId(settings) == "night");
}

TEST_CASE(SettingsCatalog_TurningTheRemoteOnCreatesItsSecretTokenOnce) {
    Settings settings;
    settings.Set("remote.enabled", "0");
    CHECK(!ampify::FillDerivedSettings(settings, "remote.enabled"));
    CHECK(!settings.Get("remote.token"));
    settings.Set("remote.enabled", "1");
    CHECK(ampify::FillDerivedSettings(settings, "remote.enabled"));
    std::string token = settings.GetString("remote.token", "");
    CHECK(ampify::remote::IsValidToken(token));
    CHECK(!ampify::FillDerivedSettings(settings, "remote.enabled"));
    CHECK(settings.GetString("remote.token", "") == token);
    CHECK(ampify::IsSecretSetting("remote.token") && !ampify::IsSecretSetting("remote.port"));
}

TEST_CASE(SettingsCatalog_TheKernelAndTheCommandLineCreateTheSameToken) {
    CHECK(ampify::RemoteTokenToCreate(std::nullopt).has_value());
    CHECK(ampify::RemoteTokenToCreate(std::string("short")).has_value());
    std::optional<std::string> token = ampify::RemoteTokenToCreate(std::nullopt);
    CHECK(token && ampify::remote::IsValidToken(*token));
    CHECK(!ampify::RemoteTokenToCreate(token));
}

TEST_CASE(SettingsCatalog_TogglesFallBackToTheDefaultTheirRowShows) {
    Settings settings;
    for (const KitSetting& row : ampify::FixedKitSettings()) {
        if (row.kind != SettingKind::Toggle) continue;
        CHECK(row.fallback == "0" || row.fallback == "1");
        CHECK(ampify::ToggleOf(settings, row.key) == (row.fallback == "1"));
    }
    CHECK(ampify::ToggleOf(settings, ampify::kThemeScriptKey));  // the theme.parts. family is on
    settings.Set(ampify::kExposeApiKey, "maybe");
    CHECK(ampify::ToggleOf(settings, ampify::kExposeApiKey));
    settings.Set(ampify::kExposeApiKey, "off");
    CHECK(!ampify::ToggleOf(settings, ampify::kExposeApiKey));
    CHECK(!ampify::ToggleOf(settings, "not.a.key"));
}

TEST_CASE(SettingsCatalog_AThemeIsOnOnlyWhileItIsTheActiveOne) {
    Settings settings;
    settings.Set("theme.active", "None");
    CHECK(!ampify::IsElementOn(settings, {ampify::ElementKind::Theme, "none"}));
    settings.Set("theme.active", "night");
    CHECK(ampify::IsElementOn(settings, {ampify::ElementKind::Theme, "night"}));
    CHECK(!ampify::IsElementOn(settings, {ampify::ElementKind::Theme, "day"}));
}

TEST_CASE(SettingsCatalog_ToggleWordsAreTheSameForEveryReader) {
    for (const char* on : {"1", "true", "On", "YES"}) CHECK(ampify::ParseToggle(on) == true);
    for (const char* off : {"0", "False", "off", "no"}) CHECK(ampify::ParseToggle(off) == false);
    for (const char* other : {"", "2", "maybe", " on"}) CHECK(!ampify::ParseToggle(other));
}

TEST_CASE(SettingsCatalog_ALineBreakCannotForgeASecondKey) {
    ampify::test::ScratchDir scratch("settings-line-break");
    std::string path = (scratch / "settings.txt").string();
    Settings settings;
    CHECK(!settings.Set("theme.accent", "FA2D48\nexpose_api = 0"));
    CHECK(!settings.Set("a\rb", "1"));
    CHECK(settings.Set("theme.accent", "FA2D48"));
    CHECK(settings.Save(path));
    Settings read;
    CHECK(read.Load(path));
    CHECK(read.Values().size() == 1 && read.GetString("theme.accent", "") == "FA2D48");
}

TEST_CASE(SettingsCatalog_SavedChangesKeepWhatAnotherWriterSavedMeanwhile) {
    ampify::test::ScratchDir scratch("settings-changes");
    std::string path = (scratch / "settings.txt").string();
    ampify::test::WriteFile(scratch / "settings.txt", "apple.close = tray\ntheme.scheme = Dark\n");
    Settings mine;
    CHECK(mine.Load(path));
    mine.Set("theme.active", "night");
    mine.Remove("theme.scheme");
    std::string error;
    CHECK(Settings::UpdateFile(path, "apple.sidebar", "hover", error));  // another writer, after the load
    CHECK(mine.SaveChanges(path, error));
    Settings saved;
    CHECK(saved.Load(path));
    CHECK(saved.GetString("theme.active", "") == "night" && saved.GetString("apple.sidebar", "") == "hover");
    CHECK(saved.GetString("apple.close", "") == "tray" && !saved.Get("theme.scheme"));
}

namespace {

// A documented key, or a family written as extensions.<id>.enabled.
const KitSetting* RowOf(const std::string& key) {
    size_t placeholder = key.find('<');
    if (placeholder == std::string::npos) return ampify::FindKitSetting(key);
    for (const KitSetting& family : ampify::KitSettingFamilies()) {
        if (family.key == key.substr(0, placeholder)) return &family;
    }
    return nullptr;
}

// The default column cut at ", ": one part per key of the row, or one for all of them.
std::string DefaultShown(const DocRow& row, size_t index) {
    std::vector<std::string> parts;
    std::string rest = row.defaults;
    for (size_t comma = rest.find(", "); comma != std::string::npos; comma = rest.find(", ")) {
        parts.push_back(rest.substr(0, comma));
        rest = rest.substr(comma + 2);
    }
    parts.push_back(rest);
    std::vector<std::string> quoted = Quoted(parts.size() == row.keys.size() ? parts[index] : parts.front());
    return quoted.empty() ? std::string() : quoted.front();
}

}  // namespace

TEST_CASE(SettingsCatalog_DocsListEveryKeyWithItsDefaultAndValues) {
    std::vector<DocRow> rows = DocumentedSettings();
    CHECK(rows.size() > 20);
    std::set<std::string> documented;
    for (const DocRow& row : rows) {
        for (size_t index = 0; index < row.keys.size(); index++) {
            const KitSetting* setting = RowOf(row.keys[index]);
            CHECK(setting != nullptr);
            if (!setting) continue;
            documented.insert(std::string(setting->key));
            if (setting->key != row.keys[index] || setting->kind == SettingKind::Text) continue;
            CHECK(DefaultShown(row, index) == setting->fallback);
            std::vector<std::string> named = Quoted(row.values + row.defaults);
            for (const auto& option : setting->options) {
                CHECK(std::find(named.begin(), named.end(), std::string(option.value)) != named.end());
            }
        }
    }
    for (const KitSetting& row : ampify::FixedKitSettings()) CHECK(documented.count(std::string(row.key)) == 1);
    for (const KitSetting& row : ampify::KitSettingFamilies()) CHECK(documented.count(std::string(row.key)) == 1);
}
