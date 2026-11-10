#include <algorithm>
#include <filesystem>
#include <set>
#include <string>

#include "../src/core/manifest.hpp"
#include "release_facts.hpp"
#include "../src/core/text_util.hpp"
#include "test.hpp"

namespace {

// Pretends that every file of `files` exists in the element's folder.
ampify::ManifestResult Check(const std::string& json, const std::string& folder, std::set<std::string> files = {"main.js"}) {
    return ampify::ParseManifest(json, folder, 1, [&](const std::string& file) { return files.count(file) > 0; });
}

// The errors of a manifest's settings field, as the theme engine reads them.
std::vector<std::string> SettingsErrorsOf(const std::string& manifest) {
    std::vector<std::string> errors;
    ampify::DeclaredSettings(manifest, &errors);
    return errors;
}

const std::string kHello = R"({
  "id": "hello-extension", "name": "Hello", "version": "0.1.0", "kind": "extension",
  "author": "Shirowwww", "description": "Example.", "kitApi": "1.0", "main": "main.js",
  "permissions": ["storage", "network:api.listenbrainz.org"]
})";

}  // namespace

TEST_CASE(Manifest_AcceptsAValidExtension) {
    ampify::ManifestResult result = Check(kHello, "hello-extension");
    CHECK(result.errors.empty());
    CHECK(result.manifest.has_value());
    CHECK(result.manifest->HasPermission("storage"));
    CHECK(result.manifest->NetworkHosts().size() == 1);
    CHECK(result.manifest->NetworkHosts()[0] == "api.listenbrainz.org");
}

TEST_CASE(Manifest_ReadsTheOldestAmpifyAVersionRunsOn) {
    std::string withMinimum = kHello;
    withMinimum.replace(withMinimum.find("\"main\""), 0, "\"minAmpify\": \"1.3.0\", ");
    ampify::ManifestResult result = Check(withMinimum, "hello-extension");
    CHECK(result.errors.empty());
    CHECK(result.manifest && result.manifest->minAmpify == std::optional<std::string>("1.3.0"));
    CHECK(Check(kHello, "hello-extension").manifest->minAmpify == std::nullopt);

    std::string malformed = kHello;
    malformed.replace(malformed.find("\"main\""), 0, "\"minAmpify\": \"1.3\", ");
    CHECK(!Check(malformed, "hello-extension").manifest);
}

TEST_CASE(Manifest_RejectsAnIdThatDiffersFromTheFolder) {
    CHECK(!Check(kHello, "other-folder").manifest);
}

TEST_CASE(Manifest_RejectsUnknownAndDuplicatePermissions) {
    std::string unknown = kHello;
    unknown.replace(unknown.find("\"storage\""), 9, "\"filesystem\"");
    CHECK(!Check(unknown, "hello-extension").manifest);

    std::string duplicate = kHello;
    duplicate.replace(duplicate.find("\"network:api.listenbrainz.org\""), 30, "\"storage\"");
    CHECK(!Check(duplicate, "hello-extension").manifest);
}

TEST_CASE(Manifest_SuggestsTheNearestPermissionName) {
    std::string typo = kHello;
    typo.replace(typo.find("\"storage\""), 9, "\"storag\"");
    ampify::ManifestResult result = Check(typo, "hello-extension");
    CHECK(!result.manifest);
    CHECK(result.errors.size() == 1);
    CHECK(result.errors[0] == "permissions[0]: unknown permission: storag (did you mean \"storage\"?)");
}

TEST_CASE(Manifest_NamesNoSuggestionWhenNothingIsClose) {
    std::string unknown = kHello;
    unknown.replace(unknown.find("\"storage\""), 9, "\"filesystem\"");
    ampify::ManifestResult result = Check(unknown, "hello-extension");
    CHECK(result.errors.size() == 1);
    CHECK(result.errors[0] == "permissions[0]: unknown permission: filesystem");
}

// A key written twice is not refused, by the kernel or by the command line: both say it, and the first value counts.
TEST_CASE(Manifest_RepeatedKeyIsAWarningWhenLoadingAndAuthoring) {
    std::string twice = kHello;
    twice.insert(twice.find("\"main\""), "\"name\": \"Second\", ");
    for (ampify::ManifestMode mode : {ampify::ManifestMode::Load, ampify::ManifestMode::Author}) {
        ampify::ManifestResult result = ampify::ParseManifest(
            twice, "hello-extension", 1, [](const std::string& file) { return file == "main.js"; }, mode);
        REQUIRE(result.manifest.has_value());
        CHECK(result.manifest->name == "Hello");
        CHECK(result.warnings == std::vector<std::string>({"duplicate key: name"}));
    }
}

TEST_CASE(Manifest_SuggestsTheNearestFieldName) {
    std::string typo = kHello;
    typo.insert(typo.find("\"main\""), "\"descripton\": \"x\", ");
    ampify::ManifestResult result = Check(typo, "hello-extension");
    CHECK(result.manifest.has_value());
    CHECK(result.warnings.size() == 1);
    CHECK(result.warnings[0] == "unknown field ignored: descripton (did you mean \"description\"?)");
}

TEST_CASE(Manifest_ShowsAnExampleWhenPermissionsIsNotAList) {
    const std::string text = R"({
  "id": "hello-extension", "name": "Hello", "version": "0.1.0", "kind": "extension",
  "author": "Shirowwww", "description": "Example.", "kitApi": "1.0", "main": "main.js",
  "permissions": "storage"
})";
    ampify::ManifestResult result = Check(text, "hello-extension");
    CHECK(!result.manifest);
    CHECK(result.errors.size() == 1);
    CHECK(result.errors[0] == "permissions: list expected");
}

TEST_CASE(Manifest_AcceptsEveryPermissionOfTheV2Schema) {
    for (const char* permission : {"ipc.discord", "midi.input", "library.write", "services.read", "ui.panel",
                                   "ui.window", "ui.tray", "audio.gain", "player.control"}) {
        CHECK(ampify::IsKnownPermission(permission));
    }
    CHECK(!ampify::IsKnownPermission("network:Api.Example.com"));  // hosts are lowercase
    CHECK(!ampify::IsKnownPermission("fs.write"));
}

TEST_CASE(Manifest_RunsInTheInterfaceUnlessDeclared) {
    ampify::ManifestResult plain = Check(kHello, "hello-extension");
    CHECK(plain.manifest->RunsIn("ui"));
    CHECK(!plain.manifest->RunsIn("engine"));

    std::string both = kHello;
    both.insert(both.find("\"main\""), "\"processes\": [\"ui\", \"engine\"], ");
    ampify::ManifestResult result = Check(both, "hello-extension");
    CHECK(result.manifest.has_value());
    CHECK(result.manifest->RunsIn("ui"));
    CHECK(result.manifest->RunsIn("engine"));
}

TEST_CASE(Manifest_RejectsUnknownOrRepeatedProcesses) {
    std::string unknown = kHello;
    unknown.insert(unknown.find("\"main\""), "\"processes\": [\"tray\"], ");
    CHECK(!Check(unknown, "hello-extension").manifest);

    std::string repeated = kHello;
    repeated.insert(repeated.find("\"main\""), "\"processes\": [\"ui\", \"ui\"], ");
    CHECK(!Check(repeated, "hello-extension").manifest);

    std::string empty = kHello;
    empty.insert(empty.find("\"main\""), "\"processes\": [], ");
    CHECK(!Check(empty, "hello-extension").manifest);
}

TEST_CASE(Manifest_RejectsAnUnsupportedKitApiMajor) {
    std::string v2 = kHello;
    v2.replace(v2.find("\"1.0\""), 5, "\"2.0\"");
    CHECK(!Check(v2, "hello-extension").manifest);
}

TEST_CASE(Manifest_RequiresMainForExtensionsAndForbidsItForSnippets) {
    std::string noMain = kHello;
    noMain.replace(noMain.find("\"main\": \"main.js\","), 19, "");
    CHECK(!Check(noMain, "hello-extension").manifest);

    std::string snippet = R"({"id":"snip","name":"S","version":"1.0.0","kind":"snippet","author":"a",
        "description":"d","kitApi":"1.0","resources":"r.xaml","main":"main.js"})";
    CHECK(!Check(snippet, "snip", {"r.xaml", "main.js"}).manifest);
}

TEST_CASE(Manifest_RequiresColorsOrResourcesForThemes) {
    std::string theme = R"({"id":"night","name":"Night","version":"1.0.0","kind":"theme","author":"a",
        "description":"d","kitApi":"1.0"})";
    CHECK(!Check(theme, "night").manifest);
    std::string withColors = theme.substr(0, theme.size() - 1) + R"(,"colors":"colors.ini"})";
    CHECK(Check(withColors, "night", {"colors.ini"}).manifest.has_value());
}

TEST_CASE(Manifest_ReportsMissingFiles) {
    ampify::ManifestResult result = Check(kHello, "hello-extension", {});
    CHECK(!result.manifest);
    CHECK(!result.errors.empty());
}

TEST_CASE(Manifest_IgnoresUnknownFieldsWithAWarning) {
    std::string extra = kHello;
    extra.insert(extra.find("\"main\""), "\"futureField\": 1, ");
    ampify::ManifestResult result = Check(extra, "hello-extension");
    CHECK(result.manifest.has_value());
    CHECK(result.warnings.size() == 1);
}

TEST_CASE(Manifest_ChecksFormatsAndLengths) {
    std::string badVersion = kHello;
    badVersion.replace(badVersion.find("0.1.0"), 5, "v0.1");
    CHECK(!Check(badVersion, "hello-extension").manifest);

    std::string httpsOnly = kHello;
    httpsOnly.insert(httpsOnly.find("\"main\""), "\"homepage\": \"http://example.org\", ");
    CHECK(!Check(httpsOnly, "hello-extension").manifest);

    CHECK(!Check("not json", "hello-extension").manifest);
}

TEST_CASE(Manifest_SurvivesMutatedInput) {
    unsigned state = 777;
    auto next = [&state] {
        state = state * 1103515245u + 12345u;
        return (state >> 16) & 0x7fff;
    };
    for (int round = 0; round < 1500; round++) {
        std::string text = kHello;
        size_t edits = 1 + next() % 5;
        for (size_t i = 0; i < edits && !text.empty(); i++) {
            size_t at = next() % text.size();
            if (next() % 2) text.erase(at, 1);
            else text[at] = static_cast<char>(32 + next() % 95);
        }
        ampify::ManifestResult result = Check(text, "hello-extension");
        // A manifest is either accepted with no error, or rejected with at least one message.
        CHECK(result.manifest.has_value() == result.errors.empty());
    }
}

TEST_CASE(Manifest_TypeIsScriptOrModule) {
    ampify::ManifestResult plain = Check(kHello, "hello-extension");
    CHECK(plain.manifest && !plain.manifest->module);

    std::string module = kHello;
    module.insert(module.find("\"main\""), "\"type\": \"module\", ");
    ampify::ManifestResult parsed = Check(module, "hello-extension");
    CHECK(parsed.warnings.empty());
    CHECK(parsed.manifest && parsed.manifest->module);

    std::string script = kHello;
    script.insert(script.find("\"main\""), "\"type\": \"script\", ");
    CHECK(Check(script, "hello-extension").manifest);

    std::string other = kHello;
    other.insert(other.find("\"main\""), "\"type\": \"commonjs\", ");
    CHECK(!Check(other, "hello-extension").manifest);
}

namespace {

std::string WithSettings(const std::string& settings) {
    std::string text = kHello;
    text.insert(text.find("\"main\""), "\"settings\": " + settings + ", ");
    return text;
}

}  // namespace

TEST_CASE(Manifest_ReadsTheSettingsAnExtensionDeclares) {
    ampify::ManifestResult result = Check(WithSettings(R"([
        {"key": "seconds", "label": "Show the seconds", "type": "toggle", "default": true},
        {"key": "side", "label": "Side", "type": "choice", "options": ["Left", "Right"], "default": "Right"},
        {"key": "greeting", "label": "Greeting", "type": "text", "default": ""},
        {"key": "size", "label": "Size", "type": "number", "min": 8, "max": 32, "default": 12.5}])"),
                                          "hello-extension");
    CHECK(result.errors.empty());
    CHECK(result.warnings.empty());
    if (!result.manifest) return;
    const std::vector<ampify::SettingSpec>& settings = result.manifest->settings;
    CHECK(settings.size() == 4);
    if (settings.size() != 4) return;
    CHECK(settings[0].type == ampify::SettingSpec::Type::Toggle && settings[0].defaultValue == "true");
    CHECK(settings[1].options == std::vector<std::string>({"Left", "Right"}) && settings[1].defaultValue == "Right");
    CHECK(settings[2].type == ampify::SettingSpec::Type::Text && settings[2].defaultValue.empty());
    CHECK(settings[3].defaultValue == "12.5" && settings[3].min == 8.0 && settings[3].max == 32.0);
    CHECK(result.manifest->Setting("size") == &settings[3]);
    CHECK(result.manifest->Setting("other") == nullptr);
    CHECK(ampify::ShortestNumberText(12) == "12");
}

TEST_CASE(Manifest_RefusesSettingsThatDoNotHold) {
    const char* wrong[] = {
        R"([])",
        R"({"key": "a"})",
        R"([{"key": "enabled", "label": "A", "type": "toggle", "default": true}])",
        R"([{"key": "Big", "label": "A", "type": "toggle", "default": true}])",
        R"([{"key": "a", "label": "", "type": "toggle", "default": true}])",
        R"([{"key": "a", "label": "A", "type": "slider", "default": 1}])",
        R"([{"key": "a", "label": "A", "type": "toggle", "default": "yes"}])",
        R"([{"key": "a", "label": "A", "type": "choice", "options": ["x"], "default": "x"}])",
        R"([{"key": "a", "label": "A", "type": "choice", "options": ["x", "y"], "default": "z"}])",
        R"([{"key": "a", "label": "A", "type": "choice", "default": "x"}])",
        R"([{"key": "a", "label": "A", "type": "text", "default": "two\nlines"}])",
        R"([{"key": "a", "label": "A", "type": "text", "default": "padded "}])",
        R"([{"key": "a", "label": "A", "type": "choice", "options": [" x", "y"], "default": "y"}])",
        R"([{"key": "a", "label": "A", "type": "choice", "options": ["x", "y\nz"], "default": "x"}])",
        R"([{"key": "a", "label": "A", "type": "number", "min": 1, "max": 5, "default": 9}])",
        R"([{"key": "a", "label": "A", "type": "text", "default": "x", "min": 1}])",
        R"([{"key": "a", "label": "A", "type": "toggle", "default": true, "hint": "x"}])",
        R"([{"key": "a", "label": "A", "type": "toggle", "default": true},
            {"key": "a", "label": "B", "type": "toggle", "default": false}])",
    };
    for (const char* settings : wrong) {
        CHECK(!Check(WithSettings(settings), "hello-extension").manifest);
        CHECK(!SettingsErrorsOf(WithSettings(settings)).empty());
    }
    CHECK(SettingsErrorsOf(kHello).empty());

    std::string app = WithSettings(R"([{"key": "a", "label": "A", "type": "toggle", "default": true}])");
    app.replace(app.find("\"extension\""), 11, "\"app\"");
    CHECK(!Check(app, "hello-extension").manifest);
    CHECK(!SettingsErrorsOf(app).empty());
}

TEST_CASE(Manifest_AThemeAndASnippetDeclareSettingsAndAnAppDoesNot) {
    const std::string theme = R"({"id": "night", "name": "Night", "version": "1.0.0", "kind": "theme",
        "author": "Shirowwww", "description": "Dark.", "kitApi": "1.0", "colors": "colors.ini",
        "settings": [{"key": "radius", "label": "Corners", "type": "number", "min": 0, "max": 24, "default": 8},
                     {"key": "hide-radio", "label": "Hide Radio", "type": "toggle", "default": false}]})";
    ampify::ManifestResult result = Check(theme, "night", {"colors.ini"});
    CHECK(result.errors.empty());
    CHECK(result.manifest && result.manifest->settings.size() == 2);
    CHECK(SettingsErrorsOf(theme).empty());

    std::vector<std::string> errors;
    std::vector<ampify::SettingSpec> declared = ampify::DeclaredSettings(theme, &errors);
    CHECK(errors.empty());
    CHECK(declared.size() == 2 && declared[0].key == "radius" && declared[1].defaultValue == "false");

    std::string snippet = theme;
    snippet.replace(snippet.find("\"theme\""), 7, "\"snippet\"");
    CHECK(SettingsErrorsOf(snippet).empty());
    CHECK(ampify::DeclaredSettings(snippet, &errors).size() == 2);

    std::string app = theme;
    app.replace(app.find("\"theme\""), 7, "\"app\"");
    CHECK(!SettingsErrorsOf(app).empty());
    CHECK(ampify::DeclaredSettings(app, &errors).empty());
}

TEST_CASE(Manifest_AStoredSettingValueCountsOnlyWhenItFits) {
    ampify::SettingSpec number;
    number.type = ampify::SettingSpec::Type::Number;
    number.defaultValue = "8";
    number.min = 0;
    number.max = 24;
    CHECK(ampify::SettingValue(number, std::nullopt) == "8");
    CHECK(ampify::SettingValue(number, "12.50") == "12.5");
    CHECK(ampify::SettingValue(number, "30") == "8");
    CHECK(ampify::SettingValue(number, "wide") == "8");

    ampify::SettingSpec toggle;
    toggle.defaultValue = "false";
    CHECK(ampify::SettingValue(toggle, "1") == "true");
    CHECK(ampify::SettingValue(toggle, "off") == "false");
    CHECK(ampify::SettingValue(toggle, "maybe") == "false");

    ampify::SettingSpec choice;
    choice.type = ampify::SettingSpec::Type::Choice;
    choice.options = {"Blue", "Pink"};
    choice.defaultValue = "Blue";
    CHECK(ampify::SettingValue(choice, "Pink") == "Pink");
    CHECK(ampify::SettingValue(choice, "Green") == "Blue");
}

TEST_CASE(Manifest_SaysWhyAStoredSettingValueDoesNotFit) {
    ampify::SettingSpec number;
    number.type = ampify::SettingSpec::Type::Number;
    number.min = 0;
    number.max = 24;
    CHECK(ampify::SettingProblem(number, "12.5").empty());
    CHECK(ampify::SettingProblem(number, "30").find("above its maximum, 24") != std::string::npos);
    CHECK(ampify::SettingProblem(number, "-1").find("below its minimum, 0") != std::string::npos);
    CHECK(ampify::SettingProblem(number, "wide").find("not a number") != std::string::npos);

    ampify::SettingSpec toggle;
    CHECK(ampify::SettingProblem(toggle, "Yes").empty());
    CHECK(ampify::SettingProblem(toggle, "maybe").find("not on or off") != std::string::npos);

    ampify::SettingSpec choice;
    choice.type = ampify::SettingSpec::Type::Choice;
    choice.options = {"Blue", "Pink"};
    CHECK(ampify::SettingProblem(choice, "Pink").empty());
    CHECK(ampify::SettingProblem(choice, "Green") == "\"Green\" is not one of its choices: Blue, Pink");
}

namespace {

// The fixed names of a schema's permission pattern, ^(player\.read|...|network:...)$, without their escapes.
std::set<std::string> SchemaPermissions(const ampify::Json& schema) {
    const ampify::Json* defs = schema.Find("$defs");
    const ampify::Json* permission = defs ? defs->Find("permission") : nullptr;
    const ampify::Json* pattern = permission ? permission->Find("pattern") : nullptr;
    std::string text = pattern && pattern->AsString() ? *pattern->AsString() : "";
    std::set<std::string> names;
    if (text.size() < 4) return names;
    text = text.substr(2, text.size() - 4);  // inside ^( and )$
    for (size_t start = 0; start <= text.size();) {
        size_t end = std::min(text.find('|', start), text.size());
        std::string name = text.substr(start, end - start);
        name.erase(std::remove(name.begin(), name.end(), '\\'), name.end());
        if (name.rfind(std::string(ampify::kNetworkPermissionPrefix), 0) != 0) names.insert(name);
        start = end + 1;
    }
    return names;
}

std::set<std::string> SchemaLanguages(const ampify::Json& schema) {
    const ampify::Json* defs = schema.Find("$defs");
    const ampify::Json* translations = defs ? defs->Find("translations") : nullptr;
    const ampify::Json* properties = translations ? translations->Find("properties") : nullptr;
    std::set<std::string> codes;
    if (properties && properties->AsObject()) {
        for (const auto& [code, value] : *properties->AsObject()) codes.insert(code);
    }
    return codes;
}

}  // namespace

// The schemas that check an element before publication take the permissions and the languages the kernel takes. The
// catalog's entries leave out the permissions only Ampify's own elements may hold, as every client does.
TEST_CASE(Manifest_SchemasListThePermissionsAndLanguagesOfTheCore) {
    std::set<std::string> permissions;
    std::set<std::string> downloadable;
    for (const ampify::PermissionInfo& permission : ampify::kPermissions) {
        permissions.insert(std::string(permission.name));
        if (!permission.firstPartyOnly) downloadable.insert(std::string(permission.name));
    }
    std::set<std::string> languages(std::begin(ampify::kTranslationLanguages), std::end(ampify::kTranslationLanguages));
    for (const char* name : {"manifest", "catalog"}) {
        std::filesystem::path file =
            std::filesystem::path(AMPIFY_SOURCE_DIR) / "schemas" / (std::string(name) + ".schema.json");
        std::optional<std::string> text = ampify::ReadWholeFile(file);
        REQUIRE(text.has_value());
        std::optional<ampify::Json> schema = ampify::Json::Parse(*text);
        REQUIRE(schema.has_value());
        CHECK(SchemaPermissions(*schema) == (std::string(name) == "catalog" ? downloadable : permissions));
        CHECK(SchemaLanguages(*schema) == languages);
    }
}
