#include <string>
#include <vector>

#include "../src/core/element_settings.hpp"
#include "../src/core/i18n.hpp"
#include "../src/core/json.hpp"
#include "support.hpp"
#include "test.hpp"

namespace {

using ampify::SettingSpec;
using ampify::settings_panel::ExportSettings;
using ampify::settings_panel::ImportRefusal;
using ampify::settings_panel::ImportSettings;

SettingSpec Toggle(const std::string& key, const std::string& group, bool on) {
    SettingSpec spec;
    spec.key = key;
    spec.label = "Label " + key;
    spec.type = SettingSpec::Type::Toggle;
    spec.defaultValue = on ? "true" : "false";
    spec.group = group;
    return spec;
}

SettingSpec Number(const std::string& key, double low, double high, const std::string& fallback) {
    SettingSpec spec;
    spec.key = key;
    spec.label = "Size " + key;
    spec.type = SettingSpec::Type::Number;
    spec.min = low;
    spec.max = high;
    spec.defaultValue = fallback;
    return spec;
}

SettingSpec Choice(const std::string& key, std::vector<std::string> options) {
    SettingSpec spec;
    spec.key = key;
    spec.label = "Side " + key;
    spec.type = SettingSpec::Type::Choice;
    spec.options = options;
    spec.defaultValue = options.front();
    return spec;
}

SettingSpec Text(const std::string& key, const std::string& fallback) {
    SettingSpec spec;
    spec.key = key;
    spec.label = "Text " + key;
    spec.type = SettingSpec::Type::Text;
    spec.defaultValue = fallback;
    return spec;
}

std::vector<SettingSpec> Sample() {
    return {Toggle("seconds", "Display", true), Number("radius", 0, 24, "8"), Choice("side", {"Left", "Right"}),
            Text("greeting", ""), Toggle("halo", "Effects", false)};
}

std::string Replace(std::string text, const std::string& from, const std::string& to) {
    size_t at = text.find(from);
    if (at != std::string::npos) text.replace(at, from.size(), to);
    return text;
}

const std::string kFile = R"({"format": "ampify-settings", "version": 1, "kind": "theme", "id": "night",
  "values": {"seconds": false, "radius": 12, "side": "Right", "greeting": "Hi", "halo": true}})";

}  // namespace

TEST_CASE(SettingsPanel_SettingsWithoutGroupsFormOneUnnamedGroup) {
    std::vector<SettingSpec> plain = {Toggle("a", "", true), Toggle("b", "", false)};
    std::vector<ampify::settings_panel::SettingGroup> groups = ampify::settings_panel::GroupSettings(plain);
    CHECK(groups.size() == 1);
    if (groups.size() != 1) return;
    CHECK(groups[0].name.empty());
    CHECK(groups[0].settings == std::vector<size_t>({0, 1}));
    CHECK(ampify::settings_panel::GroupSettings({}).empty());
}

TEST_CASE(SettingsPanel_GroupsKeepTheirFirstAppearanceAndTheUngroupedFirst) {
    std::vector<SettingSpec> settings = {Toggle("a", "Effects", true), Toggle("b", "", true), Toggle("c", "Display", true),
                                         Toggle("d", "Effects", false)};
    std::vector<ampify::settings_panel::SettingGroup> groups = ampify::settings_panel::GroupSettings(settings);
    CHECK(groups.size() == 3);
    if (groups.size() != 3) return;
    CHECK(groups[0].name.empty() && groups[0].settings == std::vector<size_t>({1}));
    CHECK(groups[1].name == "Effects" && groups[1].settings == std::vector<size_t>({0, 3}));
    CHECK(groups[2].name == "Display" && groups[2].settings == std::vector<size_t>({2}));
}

TEST_CASE(SettingsPanel_SearchMatchesEveryWordInLabelOrDescription) {
    std::vector<ampify::settings_panel::SettingText> texts = {
        {"Show the seconds", "Adds the seconds to the clock"},
        {"Corner radius", "How round the buttons are"},
        {"Halo", ""},
    };
    using ampify::settings_panel::MatchingSettings;
    CHECK(MatchingSettings(texts, "").size() == 3);
    CHECK(MatchingSettings(texts, "SECONDS") == std::vector<size_t>({0}));
    CHECK(MatchingSettings(texts, "round buttons") == std::vector<size_t>({1}));
    CHECK(MatchingSettings(texts, "round halo").empty());
    CHECK(MatchingSettings(texts, "   ") .size() == 3);
}

TEST_CASE(SettingsPanel_SearchIgnoresTheAccentsOfFrench) {
    std::vector<ampify::settings_panel::SettingText> texts = {{"Réglage du halo", "Couleur d'arrière-plan"}};
    using ampify::settings_panel::MatchingSettings;
    CHECK(MatchingSettings(texts, "reglage") == std::vector<size_t>({0}));
    CHECK(MatchingSettings(texts, "RÉGLAGE") == std::vector<size_t>({0}));
    CHECK(MatchingSettings(texts, "arriere") == std::vector<size_t>({0}));
    CHECK(MatchingSettings(texts, "fond").empty());
}

TEST_CASE(SettingsPanel_SearchIgnoresTheAccentsOfGermanSpanishAndPortuguese) {
    using ampify::settings_panel::MatchingSettings;
    std::vector<ampify::settings_panel::SettingText> texts = {
        {"Zurücksetzen der Größe", "Schaltfläche"}, {"Niño grande", ""}, {"Ação", "Órgão"}};
    CHECK(MatchingSettings(texts, "zurucksetzen") == std::vector<size_t>({0}));
    CHECK(MatchingSettings(texts, "grosse") == std::vector<size_t>({0}));
    CHECK(MatchingSettings(texts, "schaltflache") == std::vector<size_t>({0}));
    CHECK(MatchingSettings(texts, "nino") == std::vector<size_t>({1}));
    CHECK(MatchingSettings(texts, "ACAO") == std::vector<size_t>({2}));
    CHECK(MatchingSettings(texts, "orgao") == std::vector<size_t>({2}));
}

TEST_CASE(SettingsPanel_ResetValuesAreTheManifestDefaults) {
    std::map<std::string, std::string> defaults = ampify::settings_panel::DefaultValues(Sample());
    CHECK(defaults.size() == 5);
    CHECK(defaults["seconds"] == "true");
    CHECK(defaults["radius"] == "8");
    CHECK(defaults["side"] == "Left");
    CHECK(defaults["greeting"].empty());
    CHECK(defaults["halo"] == "false");
}

TEST_CASE(SettingsPanel_ExportKeepsTheTypeOfEachValue) {
    std::map<std::string, std::string> values = {{"seconds", "false"}, {"radius", "12.5"}, {"side", "Right"},
                                                 {"greeting", "Hi"}};
    std::string text = ExportSettings("theme", "night", Sample(), values);
    std::optional<ampify::Json> root = ampify::Json::Parse(text);
    CHECK(root && root->IsObject());
    if (!root) return;
    CHECK(root->Find("format")->AsString() && *root->Find("format")->AsString() == "ampify-settings");
    CHECK(root->Find("version")->AsNumber() && *root->Find("version")->AsNumber() == 1);
    CHECK(*root->Find("kind")->AsString() == "theme");
    CHECK(*root->Find("id")->AsString() == "night");
    const ampify::Json* entries = root->Find("values");
    CHECK(entries && entries->AsObject() && entries->AsObject()->size() == 5);
    if (!entries) return;
    CHECK(*entries->Find("seconds")->AsBool() == false);
    CHECK(*entries->Find("radius")->AsNumber() == 12.5);
    CHECK(*entries->Find("side")->AsString() == "Right");
    CHECK(*entries->Find("halo")->AsBool() == false);
}

TEST_CASE(SettingsPanel_ImportReadsWhatExportWrote) {
    std::map<std::string, std::string> values = {{"radius", "3"}, {"halo", "true"}};
    std::string text = ExportSettings("snippet", "gloss", {Number("radius", 0, 24, "8"), Toggle("halo", "", false)}, values);
    ImportRefusal error;
    std::optional<std::map<std::string, std::string>> read =
        ImportSettings(text, "snippet", "gloss", {Number("radius", 0, 24, "8"), Toggle("halo", "", false)}, error);
    CHECK(read.has_value());
    if (!read) return;
    CHECK((*read)["radius"] == "3");
    CHECK((*read)["halo"] == "true");
}

TEST_CASE(SettingsPanel_ImportAcceptsAFileThatLeavesKeysOut) {
    ImportRefusal error;
    std::optional<std::map<std::string, std::string>> read = ImportSettings(kFile, "theme", "night", Sample(), error);
    CHECK(read.has_value());
    if (!read) return;
    CHECK(read->size() == 5);
    CHECK((*read)["radius"] == "12");
    CHECK((*read)["seconds"] == "false");

    std::string partial = Replace(kFile, R"("radius": 12, )", "");
    std::optional<std::map<std::string, std::string>> part = ImportSettings(partial, "theme", "night", Sample(), error);
    CHECK(part.has_value() && part->size() == 4 && part->count("radius") == 0);
}

TEST_CASE(SettingsPanel_ImportRefusesAFileOfAnotherElementOrFormatWithItsReason) {
    ImportRefusal error;
    CHECK(!ImportSettings(kFile, "theme", "other", Sample(), error));
    CHECK(error.reason == "settings.refusedOtherElement");
    CHECK(!ImportSettings(kFile, "extension", "night", Sample(), error));
    CHECK(error.reason == "settings.refusedOtherElement");
    CHECK(!ImportSettings(Replace(kFile, "ampify-settings", "other-app"), "theme", "night", Sample(), error));
    CHECK(error.reason == "settings.refusedNotSettings");
    CHECK(!ImportSettings(Replace(kFile, "\"version\": 1", "\"version\": 2"), "theme", "night", Sample(), error));
    CHECK(error.reason == "settings.refusedVersion" && error.detail == "2");
    CHECK(!ImportSettings("[]", "theme", "night", Sample(), error));
    CHECK(error.reason == "settings.refusedNotSettings");
    CHECK(!ImportSettings("not json", "theme", "night", Sample(), error));
    CHECK(error.reason == "settings.refusedNotSettings");
}

TEST_CASE(SettingsPanel_ImportRefusesAnUnknownKeyAndAWrongType) {
    ImportRefusal error;
    const std::vector<std::string> wrong = {
        Replace(kFile, "\"halo\": true", "\"halo\": true, \"other\": 1"),  // unknown key
        Replace(kFile, "\"seconds\": false", "\"seconds\": \"no\""),      // a switch given as a text
        Replace(kFile, "\"radius\": 12", "\"radius\": \"12\""),           // a number given as a text
        Replace(kFile, "\"radius\": 12", "\"radius\": 40"),               // out of the bounds
        Replace(kFile, "\"side\": \"Right\"", "\"side\": \"Up\""),        // not one of the choices
        Replace(kFile, "\"greeting\": \"Hi\"", "\"greeting\": 4"),        // a text given as a number
        Replace(kFile, "\"greeting\": \"Hi\"", "\"greeting\": \"two\\nlines\""),
        Replace(kFile, "\"greeting\": \"Hi\"", "\"greeting\": \" padded\""),
        Replace(kFile, "\"values\": {", "\"values\": [], \"x\": {"),
        Replace(kFile, "\"seconds\": false", "\"seconds\": false, \"seconds\": true"),  // a repeated key
    };
    for (const std::string& text : wrong) {
        CHECK(!ImportSettings(text, "theme", "night", Sample(), error));
        CHECK(error.reason.rfind("settings.refused", 0) == 0);
    }
    CHECK(!ImportSettings(wrong[0], "theme", "night", Sample(), error));
    CHECK(error.reason == "settings.refusedUnknown" && error.detail == "other");
    CHECK(!ImportSettings(wrong[3], "theme", "night", Sample(), error));
    CHECK(error.reason == "settings.refusedWrongValue" && error.detail == "radius");
}

TEST_CASE(SettingsPanel_ImportRefusesAFileBiggerThanTheLimit) {
    ImportRefusal error;
    std::string big = kFile + std::string(ampify::settings_panel::kMaxFileBytes, ' ');
    CHECK(!ImportSettings(big, "theme", "night", Sample(), error));
    CHECK(error.reason == "settings.refusedTooBig");
}

TEST_CASE(SettingsPanel_ARefusalIsToldInTheLanguageOfTheInterface) {
    const char* reasons[] = {"settings.refusedTooBig",       "settings.refusedNotSettings", "settings.refusedVersion",
                             "settings.refusedOtherElement", "settings.refusedUnknown",     "settings.refusedWrongValue"};
    ampify::test::LanguageScope scope("en");
    for (std::string_view language : ampify::i18n::Languages()) {
        ampify::i18n::SetLanguage(language);
        for (const char* reason : reasons) CHECK(ampify::i18n::Text(reason) != reason);
    }
    ImportRefusal refusal{"settings.refusedWrongValue", "radius"};
    ampify::i18n::SetLanguage("fr");

    std::string shown = ampify::settings_panel::RefusalText(refusal);

    CHECK(shown.find("radius") != std::string::npos);
    CHECK(shown.find("wrong value") == std::string::npos);
    ampify::i18n::SetLanguage("en");
    CHECK(ampify::settings_panel::RefusalText(refusal) == "wrong value for radius");
}

TEST_CASE(SettingsPanel_ASettingIsChangedOnlyWhenItsValueDiffersFromItsDefault) {
    using ampify::settings_panel::ChangedSettings;
    std::vector<SettingSpec> settings = {Toggle("seconds", "", true), Number("radius", 0, 24, "8.0"), Text("greeting", "hi")};

    std::vector<bool> changed = ChangedSettings(settings, {"true", "8", "hello"});

    CHECK(changed == std::vector<bool>({false, false, true}));
    CHECK(ChangedSettings(settings, {"false"}) == std::vector<bool>({true, false, false}));  // a missing value: unchanged
}
