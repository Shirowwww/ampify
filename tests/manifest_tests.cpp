#include <set>
#include <string>

#include "../core/manifest.hpp"
#include "test.hpp"

namespace {

// Pretends that every file of `files` exists in the element's folder.
amk::ManifestResult Check(const std::string& json, const std::string& folder, std::set<std::string> files = {"main.js"}) {
    return amk::ParseManifest(json, folder, 1, [&](const std::string& file) { return files.count(file) > 0; });
}

const std::string kHello = R"({
  "id": "hello-extension", "name": "Bonjour", "version": "0.1.0", "kind": "extension",
  "author": "Shirowwww", "description": "Exemple.", "kitApi": "1.0", "main": "main.js",
  "permissions": ["storage", "network:api.listenbrainz.org"]
})";

}  // namespace

TEST_CASE(Manifest_AcceptsAValidExtension) {
    amk::ManifestResult result = Check(kHello, "hello-extension");
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
        CHECK(amk::IsKnownPermission(permission));
    }
    CHECK(!amk::IsKnownPermission("network:Api.Example.com"));  // hosts are lowercase
    CHECK(!amk::IsKnownPermission("fs.write"));
}

TEST_CASE(Manifest_RunsInBothProcessesUnlessDeclared) {
    amk::ManifestResult both = Check(kHello, "hello-extension");
    CHECK(both.manifest->RunsIn("ui"));
    CHECK(both.manifest->RunsIn("engine"));

    std::string uiOnly = kHello;
    uiOnly.insert(uiOnly.find("\"main\""), "\"processes\": [\"ui\"], ");
    amk::ManifestResult result = Check(uiOnly, "hello-extension");
    CHECK(result.manifest.has_value());
    CHECK(result.manifest->RunsIn("ui"));
    CHECK(!result.manifest->RunsIn("engine"));
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
    std::string theme = R"({"id":"night","name":"Nuit","version":"1.0.0","kind":"theme","author":"a",
        "description":"d","kitApi":"1.0"})";
    CHECK(!Check(theme, "night").manifest);
    std::string withColors = theme.substr(0, theme.size() - 1) + R"(,"colors":"colors.ini"})";
    CHECK(Check(withColors, "night", {"colors.ini"}).manifest.has_value());
}

TEST_CASE(Manifest_ReportsMissingFiles) {
    amk::ManifestResult result = Check(kHello, "hello-extension", {});
    CHECK(!result.manifest);
    CHECK(!result.errors.empty());
}

TEST_CASE(Manifest_IgnoresUnknownFieldsWithAWarning) {
    std::string extra = kHello;
    extra.insert(extra.find("\"main\""), "\"futureField\": 1, ");
    amk::ManifestResult result = Check(extra, "hello-extension");
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
        amk::ManifestResult result = Check(text, "hello-extension");
        // A manifest is either accepted with no error, or rejected with at least one message.
        CHECK(result.manifest.has_value() == result.errors.empty());
    }
}
