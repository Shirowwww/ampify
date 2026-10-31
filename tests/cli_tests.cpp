// Tests of the portable part of `ampify`: JSON, SHA-256, manifests, catalog, kit layout, backups, archives,
// change detection and release metadata. The command line itself is tested by tools/test_cli.py.

#include <chrono>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include "../cli/archive.hpp"
#include "../cli/backup.hpp"
#include "../cli/catalog.hpp"
#include "../cli/element.hpp"
#include "../cli/json.hpp"
#include "../cli/kit.hpp"
#include "../cli/release.hpp"
#include "../cli/sha256.hpp"
#include "../cli/watch.hpp"
#include "../cli/zip_writer.hpp"
#include "test.hpp"

namespace {

// A fresh, empty directory for each test, so files never leak between runs.
std::filesystem::path ScratchDir(const std::string& name) {
    std::filesystem::path dir = std::filesystem::temp_directory_path() / "ampify-cli-tests" / name;
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

void WriteFile(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << text;
}

std::string ReadFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// A manifest that is valid for an extension called "demo" when the folder is named so.
const char* const kValidExtension = R"({
  "id": "demo",
  "name": "Demo",
  "version": "1.2.3",
  "kind": "extension",
  "author": "Shirowwww",
  "description": "A test extension.",
  "kitApi": "1.0",
  "main": "main.js",
  "permissions": ["storage", "network:api.example.com"]
})";

std::vector<std::string> ErrorsOf(const std::string& manifest) {
    std::vector<std::string> errors;
    ampify::ParseManifest(manifest, errors);
    return errors;
}

bool MentionsField(const std::vector<std::string>& errors, const std::string& field) {
    for (const std::string& error : errors) {
        if (error.rfind(field, 0) == 0) return true;
    }
    return false;
}

}  // namespace

// ---- SHA-256 -----------------------------------------------------------------------------------

TEST_CASE(Sha256_KnownVectors) {
    CHECK(ampify::Sha256Hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(ampify::Sha256Hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(ampify::Sha256Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST_CASE(Sha256_LongInputAcrossBlocks) {
    std::string million(1000000, 'a');
    CHECK(ampify::Sha256Hex(million) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST_CASE(Sha256_FileMatchesText) {
    std::filesystem::path file = ScratchDir("sha-file") / "data.bin";
    std::string content(200000, 'x');
    WriteFile(file, content);
    std::optional<std::string> digest = ampify::Sha256File(file);
    CHECK(digest.has_value());
    CHECK(digest && *digest == ampify::Sha256Hex(content));
    CHECK(!ampify::Sha256File(file.parent_path() / "absent.bin").has_value());
}

TEST_CASE(Sha256_HexFormatIsStrict) {
    CHECK(ampify::IsSha256Hex(std::string(64, 'a')));
    CHECK(!ampify::IsSha256Hex(std::string(64, 'A')));
    CHECK(!ampify::IsSha256Hex(std::string(63, 'a')));
    CHECK(!ampify::IsSha256Hex(std::string(64, 'g')));
}

// ---- JSON --------------------------------------------------------------------------------------

TEST_CASE(Json_ParsesNestedDocument) {
    std::string error;
    std::optional<ampify::json::Value> document =
        ampify::json::Parse(R"({"a": [1, -2.5e1, true, null, "x"], "b": {"c": false}})", error);
    CHECK(document.has_value());
    if (!document) return;
    const ampify::json::Value* a = document->Find("a");
    CHECK(a != nullptr && a->IsArray() && a->Items().size() == 5);
    CHECK(a && a->Items()[1].AsNumber() == -25.0);
    CHECK(a && a->Items()[3].IsNull());
    const ampify::json::Value* b = document->Find("b");
    CHECK(b && b->Find("c") && !b->Find("c")->AsBool());
    CHECK(document->Find("missing") == nullptr);
}

TEST_CASE(Json_DecodesEscapesAndSurrogatePairs) {
    std::string error;
    std::optional<ampify::json::Value> document =
        ampify::json::Parse(R"(["\u00e9", "\ud83c\udfb5", "tab\there\n", "\"q\""])", error);
    CHECK(document.has_value());
    if (!document) return;
    CHECK(document->Items()[0].AsString() == "\xC3\xA9");          // é
    CHECK(document->Items()[1].AsString() == "\xF0\x9F\x8E\xB5");  // U+1F3B5
    CHECK(document->Items()[2].AsString() == "tab\there\n");
    CHECK(document->Items()[3].AsString() == "\"q\"");
}

TEST_CASE(Json_RejectsMalformedDocuments) {
    std::string error;
    CHECK(!ampify::json::Parse(R"({"a": 1,})", error).has_value());
    CHECK(!ampify::json::Parse(R"({"a": 1} x)", error).has_value());
    CHECK(!ampify::json::Parse(R"({"a": 1, "a": 2})", error).has_value());
    CHECK(error.find("duplicate member") != std::string::npos);
    CHECK(!ampify::json::Parse("\"unterminated", error).has_value());
    CHECK(!ampify::json::Parse("[01]", error).has_value());
    CHECK(!ampify::json::Parse("\"\\ud800\"", error).has_value());
    CHECK(!ampify::json::Parse("\"raw\ncontrol\"", error).has_value());
    CHECK(!ampify::json::Parse("", error).has_value());
}

TEST_CASE(Json_RefusesExcessiveNesting) {
    std::string deep(100, '[');
    deep += std::string(100, ']');
    std::string error;
    CHECK(!ampify::json::Parse(deep, error).has_value());
    CHECK(error.find("nesting") != std::string::npos);
}

TEST_CASE(Json_QuoteEscapesControlCharacters) {
    CHECK(ampify::json::Quote("a\"b\\c\n") == "\"a\\\"b\\\\c\\n\"");
    CHECK(ampify::json::Quote(std::string(1, '\x01')) == "\"\\u0001\"");
}

// ---- Manifests ---------------------------------------------------------------------------------

TEST_CASE(Manifest_ValidExtensionParses) {
    std::vector<std::string> errors;
    std::optional<ampify::ElementManifest> manifest = ampify::ParseManifest(kValidExtension, errors);
    CHECK(errors.empty());
    CHECK(manifest.has_value());
    if (!manifest) return;
    CHECK(manifest->id == "demo");
    CHECK(manifest->name == "Demo");
    CHECK(manifest->kind == "extension");
    CHECK(manifest->main && *manifest->main == "main.js");
    CHECK(manifest->permissions.size() == 2);
}

TEST_CASE(Manifest_RejectsUnknownKeysAndMissingFields) {
    std::vector<std::string> errors = ErrorsOf(R"({"id": "demo", "surprise": true})");
    CHECK(MentionsField(errors, "surprise: unknown field"));
    CHECK(MentionsField(errors, "name: required field missing"));
    CHECK(MentionsField(errors, "kitApi: required field missing"));
}

TEST_CASE(Manifest_RejectsBadIdVersionKindAndAuthor) {
    std::string base = kValidExtension;
    auto with = [&](const std::string& from, const std::string& to) {
        std::string text = base;
        text.replace(text.find(from), from.size(), to);
        return ErrorsOf(text);
    };
    CHECK(MentionsField(with("\"demo\"", "\"Demo\""), "id"));
    CHECK(MentionsField(with("\"1.2.3\"", "\"1.2\""), "version"));
    CHECK(MentionsField(with("\"extension\"", "\"plugin\""), "kind"));
    CHECK(MentionsField(with("\"Shirowwww\"", "\"-bad\""), "author"));
}

TEST_CASE(Manifest_RejectsUnsupportedKitApiMajor) {
    std::string text = kValidExtension;
    text.replace(text.find("\"1.0\""), 5, "\"2.0\"");
    CHECK(MentionsField(ErrorsOf(text), "kitApi: unsupported API version"));
    CHECK(ErrorsOf(kValidExtension).empty());
}

TEST_CASE(Manifest_RejectsUnknownPermissionAndDuplicates) {
    std::string text = kValidExtension;
    text.replace(text.find("\"storage\""), 9, "\"fairplay\"");
    CHECK(MentionsField(ErrorsOf(text), "permissions[0]"));

    std::string duplicate = kValidExtension;
    duplicate.replace(duplicate.find("\"network:api.example.com\""), 25, "\"storage\"");
    CHECK(MentionsField(ErrorsOf(duplicate), "permissions[1]: duplicate value"));
}

TEST_CASE(Manifest_CountsCharactersNotBytes) {
    std::string accents;
    for (int i = 0; i < 300; i++) accents += "é";  // 300 characters, 600 bytes
    std::string text = std::string(R"({"id": "demo", "name": "x", "version": "1.0.0", "kind": "extension",
        "author": "a", "kitApi": "1.0", "main": "main.js", "description": ")") + accents + "\"}";
    CHECK(MentionsField(ErrorsOf(text), "description") == false);

    accents += "é";
    text = std::string(R"({"id": "demo", "name": "x", "version": "1.0.0", "kind": "extension",
        "author": "a", "kitApi": "1.0", "main": "main.js", "description": ")") + accents + "\"}";
    CHECK(MentionsField(ErrorsOf(text), "description"));
}

TEST_CASE(Manifest_RejectsSubfoldersAndTraversalInFileNames) {
    std::string text = kValidExtension;
    text.replace(text.find("\"main.js\""), 9, "\"../main.js\"");
    CHECK(MentionsField(ErrorsOf(text), "main"));
}

TEST_CASE(Folder_CheckKindRulesAndFiles) {
    std::filesystem::path root = ScratchDir("folder-rules");

    std::filesystem::path good = root / "demo";
    WriteFile(good / "manifest.json", kValidExtension);
    WriteFile(good / "main.js", "Ampify.log('ok');");
    CHECK(ampify::ValidateElementFolder(good).ok());

    std::filesystem::path missingMain = root / "demo-no-main";
    WriteFile(missingMain / "manifest.json", kValidExtension);
    ampify::FolderValidation missing = ampify::ValidateElementFolder(missingMain);
    // The id is not the folder name either, so both problems are reported.
    CHECK(!missing.ok());
    CHECK(MentionsField(missing.errors, "main: file missing from the folder: main.js"));
    CHECK(MentionsField(missing.errors, "id: must be the folder name"));

    ampify::FolderValidation noManifest = ampify::ValidateElementFolder(root / "empty");
    CHECK(!noManifest.ok());
    CHECK(MentionsField(noManifest.errors, "manifest.json missing"));
}

TEST_CASE(Folder_ThemeAndSnippetRules) {
    std::filesystem::path root = ScratchDir("folder-kinds");

    WriteFile(root / "theme-a" / "manifest.json",
              R"({"id": "theme-a", "name": "T", "version": "1.0.0", "kind": "theme", "author": "a",
                  "description": "d", "kitApi": "1.0"})");
    ampify::FolderValidation theme = ampify::ValidateElementFolder(root / "theme-a");
    CHECK(MentionsField(theme.errors, "colors or resources"));

    WriteFile(root / "snip" / "manifest.json",
              R"({"id": "snip", "name": "S", "version": "1.0.0", "kind": "snippet", "author": "a",
                  "description": "d", "kitApi": "1.0", "main": "x.js"})");
    WriteFile(root / "snip" / "x.js", "");
    ampify::FolderValidation snippet = ampify::ValidateElementFolder(root / "snip");
    CHECK(MentionsField(snippet.errors, "resources: required for a snippet"));
    CHECK(MentionsField(snippet.errors, "main: not allowed for a snippet"));
}

// ---- Catalog -----------------------------------------------------------------------------------

namespace {

const char* const kCatalogEntry = R"({
  "id": "demo",
  "name": "Demo",
  "version": "1.2.3",
  "kind": "extension",
  "author": "Shirowwww",
  "description": "A test extension.",
  "kitApi": "1.0",
  "url": "https://example.com/demo-1.2.3.zip",
  "sha256": "0000000000000000000000000000000000000000000000000000000000000000",
  "permissions": ["storage"]
})";

std::string CatalogWith(const std::string& entry) {
    return std::string(R"({"catalogVersion": 1, "extensions": [)") + entry + "]}";
}

}  // namespace

TEST_CASE(Catalog_RepositoryCatalogIsValid) {
    std::string path = std::string(AMPIFY_SOURCE_DIR) + "/catalog/catalog.json";
    std::vector<std::string> errors;
    std::optional<ampify::Catalog> catalog = ampify::ParseCatalog(ReadFile(path), false, errors);
    CHECK(errors.empty());
    CHECK(catalog.has_value());
}

TEST_CASE(Catalog_ValidEntryIsFound) {
    std::vector<std::string> errors;
    std::optional<ampify::Catalog> catalog = ampify::ParseCatalog(CatalogWith(kCatalogEntry), false, errors);
    CHECK(errors.empty());
    CHECK(catalog.has_value());
    if (!catalog) return;
    const ampify::CatalogEntry* entry = ampify::FindCatalogEntry(*catalog, "demo");
    CHECK(entry != nullptr);
    CHECK(entry && entry->url == "https://example.com/demo-1.2.3.zip");
    CHECK(ampify::FindCatalogEntry(*catalog, "absent") == nullptr);
}

TEST_CASE(Catalog_RefusesPlainHttpAndLocalFilesFromRemoteCatalog) {
    std::string http = kCatalogEntry;
    http.replace(http.find("https://"), 8, "http://");
    std::vector<std::string> errors;
    CHECK(!ampify::ParseCatalog(CatalogWith(http), false, errors).has_value());
    CHECK(MentionsField(errors, "extensions[0].url"));

    std::string file = kCatalogEntry;
    const std::string remote = "https://example.com/demo-1.2.3.zip";
    file.replace(file.find(remote), remote.size(), "file:///C:/dev/demo-1.2.3.zip");
    errors.clear();
    CHECK(!ampify::ParseCatalog(CatalogWith(file), false, errors).has_value());

    // A catalog read from a local file may point at local archives: this is how development works.
    errors.clear();
    CHECK(ampify::ParseCatalog(CatalogWith(file), true, errors).has_value());
}

TEST_CASE(Catalog_RejectsBadChecksumDuplicatesAndUnknownKeys) {
    std::string badSum = kCatalogEntry;
    badSum.replace(badSum.find("0000"), 4, "ABCD");
    std::vector<std::string> errors;
    CHECK(!ampify::ParseCatalog(CatalogWith(badSum), false, errors).has_value());
    CHECK(MentionsField(errors, "extensions[0].sha256"));

    errors.clear();
    std::string twice = std::string(kCatalogEntry) + "," + kCatalogEntry;
    CHECK(!ampify::ParseCatalog(CatalogWith(twice), false, errors).has_value());
    CHECK(MentionsField(errors, "extensions[1].id: id already in the catalog"));

    errors.clear();
    std::string extra = kCatalogEntry;
    extra.insert(extra.size() - 1, R"(, "stars": 5)");
    CHECK(!ampify::ParseCatalog(CatalogWith(extra), false, errors).has_value());
    CHECK(MentionsField(errors, "extensions[0].stars: unknown field"));
}

TEST_CASE(Catalog_RejectsUnknownCatalogVersion) {
    std::vector<std::string> errors;
    CHECK(!ampify::ParseCatalog(R"({"catalogVersion": 2, "extensions": []})", false, errors).has_value());
    CHECK(MentionsField(errors, "catalogVersion"));
}

// ---- Archives ----------------------------------------------------------------------------------

TEST_CASE(Archive_RefusesEscapingEntries) {
    std::string reason;
    CHECK(ampify::IsSafeArchiveEntry("manifest.json", reason));
    CHECK(ampify::IsSafeArchiveEntry("scripts/main.js", reason));
    CHECK(ampify::IsSafeArchiveEntry("assets/", reason));
    CHECK(!ampify::IsSafeArchiveEntry("../evil.js", reason));
    CHECK(!ampify::IsSafeArchiveEntry("a/../../evil.js", reason));
    CHECK(!ampify::IsSafeArchiveEntry("/etc/passwd", reason));
    CHECK(!ampify::IsSafeArchiveEntry("C:/Windows/evil.dll", reason));
    CHECK(!ampify::IsSafeArchiveEntry("..\\evil.js", reason));
    CHECK(!ampify::IsSafeArchiveEntry("a//b", reason));
    CHECK(!ampify::IsSafeArchiveEntry("./main.js", reason));
    CHECK(!ampify::IsSafeArchiveEntry("", reason));
    CHECK(!ampify::IsSafeArchiveEntry("bad\nname", reason));
}

TEST_CASE(Archive_ListingReportsFirstProblem) {
    CHECK(ampify::CheckArchiveListing({"manifest.json", "main.js"}).empty());
    std::string problem = ampify::CheckArchiveListing({"manifest.json", "../x.js"});
    CHECK(problem.find("archive refused") == 0);
}

TEST_CASE(Archive_DetectsLinksInExtractedTree) {
    std::filesystem::path root = ScratchDir("archive-links");
    WriteFile(root / "manifest.json", "{}");
    CHECK(!ampify::ContainsLink(root));
    std::error_code ec;
    std::filesystem::create_directory_symlink(root / "manifest.json", root / "link.json", ec);
    if (!ec) CHECK(ampify::ContainsLink(root));
}

// ---- Versions and release metadata ----------------------------------------------------------------

TEST_CASE(Version_ComparesNumerically) {
    CHECK(ampify::CompareReleaseVersions("0.10.0", "0.9.0") == 1);
    CHECK(ampify::CompareReleaseVersions("v1.2.3", "1.2.3") == 0);
    CHECK(ampify::CompareReleaseVersions("1.2", "1.2.0") == 0);
    CHECK(ampify::CompareReleaseVersions("0.1.0", "0.2.0") == -1);
}

TEST_CASE(Release_ParsesGitHubJson) {
    std::string error;
    std::optional<ampify::ReleaseInfo> release = ampify::ParseRelease(R"({
        "tag_name": "v0.2.0",
        "assets": [
          {"name": "ampify-0.2.0-windows-x64.zip", "browser_download_url": "https://example.com/kit.zip"},
          {"name": "SHA256SUMS.txt", "browser_download_url": "https://example.com/SHA256SUMS.txt", "size": 9}
        ]})",
                                                                 error);
    CHECK(release.has_value());
    if (!release) return;
    CHECK(release->version == "0.2.0");
    CHECK(release->assets.size() == 2);
    CHECK(release->assets.at("SHA256SUMS.txt") == "https://example.com/SHA256SUMS.txt");

    CHECK(!ampify::ParseRelease(R"({"name": "no tag"})", error).has_value());
}

TEST_CASE(Release_ChecksumsAcceptBothFormats) {
    std::string hash(64, 'a');
    std::string text = hash + "  kit.zip\r\n" + std::string(64, 'b') + " *other.zip\n" + "not a line\n" +
                       std::string(64, 'C') + "  upper.zip\n";
    std::map<std::string, std::string> sums = ampify::ParseChecksums(text);
    CHECK(sums.size() == 2);
    CHECK(sums["kit.zip"] == hash);
    CHECK(sums["other.zip"] == std::string(64, 'b'));
    CHECK(sums.count("upper.zip") == 0);
}

// ---- Kit layout, elements and activation -------------------------------------------------------------

TEST_CASE(Kit_ListsValidElementsAndSkipsWorkInProgress) {
    std::filesystem::path root = ScratchDir("kit-list");
    ampify::KitLayout kit(root);
    WriteFile(kit.Element("extension", "demo") / "manifest.json", kValidExtension);
    WriteFile(kit.Element("extension", "demo") / "main.js", "");
    WriteFile(kit.Folder("extension") / ".demo.previous" / "manifest.json", kValidExtension);
    WriteFile(kit.Element("theme", "broken") / "manifest.json", "{ not json");

    std::vector<ampify::InstalledElement> elements = ampify::ListInstalled(kit);
    CHECK(elements.size() == 2);
    if (elements.size() != 2) return;
    CHECK(elements[0].kind == "extension" && elements[0].id == "demo" && elements[0].Valid());
    CHECK(elements[1].kind == "theme" && !elements[1].Valid());
    CHECK(!elements[1].errors.empty());
}

TEST_CASE(Kit_EnableAndDisableWriteTheRightKeys) {
    ampify::Settings settings;
    ampify::SetEnabled(settings, "extension", "demo", true);
    CHECK(settings.GetBool("extensions.demo.enabled", false));
    CHECK(ampify::IsEnabled(settings, "extension", "demo"));

    ampify::SetEnabled(settings, "snippet", "dark", true);
    CHECK(settings.GetBool("snippets.dark.enabled", false));
    ampify::SetEnabled(settings, "app", "lyrics", true);
    CHECK(settings.GetBool("apps.lyrics.enabled", false));

    ampify::SetEnabled(settings, "theme", "night", true);
    CHECK(settings.GetString("theme.active", "") == "night");
    ampify::SetEnabled(settings, "theme", "day", false);  // another theme: the active one stays
    CHECK(settings.GetString("theme.active", "") == "night");
    ampify::SetEnabled(settings, "theme", "night", false);
    CHECK(settings.GetString("theme.active", "x") == "");

    CHECK(ampify::EnabledKey("extension", "atmos-leveler") == "extensions.atmos-leveler.enabled");
}

TEST_CASE(Activation_ReportsWhatLoadsAndWhatCannot) {
    std::filesystem::path root = ScratchDir("kit-active");
    ampify::KitLayout kit(root);
    WriteFile(kit.Element("extension", "demo") / "manifest.json", kValidExtension);
    WriteFile(kit.Element("extension", "demo") / "main.js", "");
    WriteFile(kit.Element("theme", "night") / "manifest.json",
              R"({"id": "night", "name": "Night", "version": "2.0.0", "kind": "theme", "author": "a",
                  "description": "d", "kitApi": "1.0", "colors": "colors.ini", "defaultScheme": "Dark"})");
    WriteFile(kit.Element("theme", "night") / "colors.ini", "[Dark]\naccent = FA2D48\n");

    ampify::Settings settings;
    settings.SetBool("extensions.demo.enabled", true);
    settings.SetBool("extensions.atmos-leveler.enabled", true);
    settings.SetBool("extensions.gone.enabled", true);
    settings.Set("theme.active", "night");

    ampify::ActiveReport report = ampify::ComputeActive(kit, settings);
    CHECK(report.items.size() == 3);
    CHECK(report.problems.size() == 1);
    CHECK(report.problems.size() == 1 && report.problems[0].find("extension gone") != std::string::npos);

    bool sawTheme = false;
    for (const ampify::ActiveItem& item : report.items) {
        if (item.kind == "theme") {
            sawTheme = true;
            CHECK(item.id == "night");
            CHECK(item.scheme == "Dark");
        }
    }
    CHECK(sawTheme);

    std::string text = ampify::FormatActive(report);
    CHECK(text.find("extension demo 1.2.3\n") != std::string::npos);
    CHECK(text.find("extension atmos-leveler builtin\n") != std::string::npos);
    CHECK(text.find("theme night 2.0.0 scheme=Dark\n") != std::string::npos);
}

TEST_CASE(Activation_InvalidEnabledElementIsAProblemNotAnItem) {
    std::filesystem::path root = ScratchDir("kit-invalid-active");
    ampify::KitLayout kit(root);
    WriteFile(kit.Element("extension", "demo") / "manifest.json", kValidExtension);  // main.js missing

    ampify::Settings settings;
    settings.SetBool("extensions.demo.enabled", true);
    ampify::ActiveReport report = ampify::ComputeActive(kit, settings);
    CHECK(report.items.empty());
    CHECK(report.problems.size() == 1);
}

TEST_CASE(WriteTextFile_CreatesFoldersAndReplacesAtomically) {
    std::filesystem::path file = ScratchDir("write-text") / "deep" / "active.txt";
    std::string error;
    CHECK(ampify::WriteTextFile(file, "first\n", error));
    CHECK(ampify::WriteTextFile(file, "second\n", error));
    CHECK(ReadFile(file) == "second\n");
    CHECK(!std::filesystem::exists(file.string() + ".tmp"));
}

// ---- Backups -----------------------------------------------------------------------------------

TEST_CASE(Backup_CreateListAndRestore) {
    std::filesystem::path root = ScratchDir("backup-cycle");
    ampify::KitLayout kit(root);
    WriteFile(kit.Settings(), "theme.active = night\n");
    WriteFile(kit.Element("theme", "night") / "manifest.json", "{}");

    std::string error;
    std::optional<std::string> first = ampify::CreateBackup(kit, error);
    CHECK(first.has_value());
    if (!first) {
        std::cerr << "backup failed: " << error << '\n';
        return;
    }
    CHECK(ampify::ListBackups(kit).size() == 1);

    // Change the configuration: a new theme, the old one removed, settings edited.
    WriteFile(kit.Settings(), "theme.active = day\n");
    std::filesystem::remove_all(kit.Element("theme", "night"));
    WriteFile(kit.Element("theme", "day") / "manifest.json", "{}");

    CHECK(ampify::RestoreBackup(kit, *first, error));
    CHECK(ReadFile(kit.Settings()) == "theme.active = night\n");
    CHECK(std::filesystem::exists(kit.Element("theme", "night") / "manifest.json"));
    CHECK(!std::filesystem::exists(kit.Element("theme", "day")));

    // The restore itself was preceded by a snapshot of the state it replaced.
    CHECK(ampify::ListBackups(kit).size() == 2);
}

TEST_CASE(Backup_RestoreRefusesUnsafeOrUnknownNames) {
    std::filesystem::path root = ScratchDir("backup-names");
    ampify::KitLayout kit(root);
    std::string error;
    CHECK(!ampify::RestoreBackup(kit, "../etc", error));
    CHECK(!ampify::RestoreBackup(kit, "", error));
    CHECK(!ampify::RestoreBackup(kit, "20990101-000000", error));
    CHECK(error.find("not found") != std::string::npos);
}

TEST_CASE(Backup_WithoutSettingsStillCreatesTheSnapshot) {
    std::filesystem::path root = ScratchDir("backup-empty");
    ampify::KitLayout kit(root);
    std::string error;
    std::optional<std::string> name = ampify::CreateBackup(kit, error);
    CHECK(name.has_value());
    if (name) {
        CHECK(std::filesystem::is_directory(kit.Backups() / *name / "extensions"));
        CHECK(!std::filesystem::exists(kit.Backups() / *name / "settings.txt"));
    }
}

// ---- Change detection --------------------------------------------------------------------------------

TEST_CASE(Fingerprint_ChangesOnlyWhenTheKitChanges) {
    std::filesystem::path root = ScratchDir("fingerprint");
    ampify::KitLayout kit(root);
    WriteFile(kit.Settings(), "a = 1\n");
    WriteFile(kit.Element("theme", "night") / "colors.ini", "[Dark]\n");

    std::string before = ampify::KitFingerprint(kit);
    CHECK(before == ampify::KitFingerprint(kit));

    // Give the file a new modification time, then edit it. The fingerprint must follow.
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    WriteFile(kit.Element("theme", "night") / "colors.ini", "[Dark]\naccent = FA2D48\n");
    CHECK(ampify::KitFingerprint(kit) != before);
    CHECK(ampify::KitFingerprint(kit).size() == 16);
}

// ---- Fixed-width output helpers used by the commands ----------------------------------------------

TEST_CASE(Path_ElementIdsAreCheckedBeforeUse) {
    CHECK(ampify::IsValidId("atmos-leveler"));
    CHECK(!ampify::IsValidId("../x"));
    CHECK(!ampify::IsValidId("x"));
    CHECK(!ampify::IsValidId("Demo"));
    CHECK(ampify::IsValidVersion("1.0.0"));
    CHECK(!ampify::IsValidVersion("1.0"));
    CHECK(ampify::IsHttpsUrl("https://example.com/a.zip"));
    CHECK(!ampify::IsHttpsUrl("http://example.com/a.zip"));
}

TEST_CASE(Crc32_MatchesTheStandardCheckValue) {
    CHECK(ampify::cli::Crc32("123456789") == 0xCBF43926u);
    CHECK(ampify::cli::Crc32("") == 0u);
}

TEST_CASE(WriteZip_IsDeterministicAndRefusesUnsafeNames) {
    std::filesystem::path root = ScratchDir("zip-writer");
    std::string error;
    std::vector<ampify::cli::ZipSource> files = {{"manifest.json", {}, "{}"}, {"main.js", {}, "Ampify.log(1);\n"}};
    CHECK(ampify::cli::WriteZip(root / "a.zip", files, error));
    CHECK(ampify::cli::WriteZip(root / "b.zip", files, error));

    std::ifstream first(root / "a.zip", std::ios::binary);
    std::ifstream second(root / "b.zip", std::ios::binary);
    std::string a((std::istreambuf_iterator<char>(first)), std::istreambuf_iterator<char>());
    std::string b((std::istreambuf_iterator<char>(second)), std::istreambuf_iterator<char>());
    CHECK(a == b);
    CHECK(a.compare(0, 4, "PK\x03\x04") == 0);

    CHECK(!ampify::cli::WriteZip(root / "bad.zip", {{"../escape.txt", {}, "x"}}, error));
    CHECK(!error.empty());
    CHECK(!std::filesystem::exists(root / "bad.zip"));
}

TEST_CASE(Manifest_ProcessesAreOneOrTwoDistinctNames) {
    const std::string base =
        R"({"id": "demo", "name": "Demo", "version": "1.0.0", "kind": "extension", "author": "Shirowwww", )"
        R"("description": "Test.", "kitApi": "1.0", "main": "main.js", )";
    std::vector<std::string> errors;
    CHECK(ampify::ParseManifest(base + R"("processes": ["ui"]})", errors).has_value());
    CHECK(ampify::ParseManifest(base + R"("processes": ["ui", "engine"]})", errors).has_value());
    CHECK(errors.empty());
    CHECK(!ampify::ParseManifest(base + R"("processes": []})", errors).has_value());
    CHECK(!ampify::ParseManifest(base + R"("processes": ["ui", "ui"]})", errors).has_value());
    CHECK(!ampify::ParseManifest(base + R"("processes": ["gpu"]})", errors).has_value());
    CHECK(!errors.empty());
}


TEST_CASE(Manifest_TypeIsScriptOrModuleForTheCli) {
    const std::string base =
        R"({"id": "demo", "name": "Demo", "version": "1.0.0", "kind": "extension", "author": "Shirowwww", )"
        R"("description": "Test.", "kitApi": "1.0", "main": "main.js", )";
    std::vector<std::string> errors;
    std::optional<ampify::ElementManifest> module = ampify::ParseManifest(base + R"("type": "module"})", errors);
    CHECK(module && module->type == std::optional<std::string>("module"));
    CHECK(ampify::ParseManifest(base + R"("type": "script"})", errors).has_value());
    CHECK(errors.empty());
    CHECK(!ampify::ParseManifest(base + R"("type": "commonjs"})", errors).has_value());
    CHECK(MentionsField(errors, "type: invalid value"));
}

TEST_CASE(Folder_RefusesASymbolicLink) {
    std::filesystem::path root = ScratchDir("folder-link");
    std::filesystem::path good = root / "demo";
    WriteFile(good / "manifest.json", kValidExtension);
    WriteFile(good / "main.js", "Ampify.log('ok');");
    WriteFile(good / "lib" / "util.js", "export const x = 1;");
    CHECK(ampify::ValidateElementFolder(good).ok());  // sub-folders are allowed: main.js may import from them

    std::error_code ec;
    std::filesystem::create_symlink(root / "elsewhere.js", good / "lib" / "link.js", ec);
    if (ec) return;  // no symbolic links on this system
    ampify::FolderValidation linked = ampify::ValidateElementFolder(good);
    CHECK(!linked.ok());
    CHECK(MentionsField(linked.errors, "symbolic link not allowed in the element: lib/link.js"));
}
