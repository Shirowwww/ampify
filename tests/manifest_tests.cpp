#include <set>
#include <string>

#include "../core/manifest.hpp"
#include "test.hpp"

namespace {

// Pretends that every file of `files` exists in the element's folder.
ampify::ManifestResult Check(const std::string& json, const std::string& folder, std::set<std::string> files = {"main.js"}) {
    return ampify::ParseManifest(json, folder, 1, [&](const std::string& file) { return files.count(file) > 0; });
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
    CHECK(ampify::SettingNumberText(12) == "12");
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
        CHECK(!ampify::SettingsErrors(WithSettings(settings)).empty());
    }
    CHECK(ampify::SettingsErrors(kHello).empty());

    std::string app = WithSettings(R"([{"key": "a", "label": "A", "type": "toggle", "default": true}])");
    app.replace(app.find("\"extension\""), 11, "\"app\"");
    CHECK(!Check(app, "hello-extension").manifest);
    CHECK(!ampify::SettingsErrors(app).empty());
}
