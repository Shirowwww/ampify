// Tests of the portable part of `amk`: JSON, SHA-256, manifests, catalog, kit layout, backups, archives,
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
    std::filesystem::path dir = std::filesystem::temp_directory_path() / "amk-cli-tests" / name;
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
  "name": "Démo",
  "version": "1.2.3",
  "kind": "extension",
  "author": "Shirowwww",
  "description": "Une extension de test.",
  "kitApi": "1.0",
  "main": "main.js",
  "permissions": ["storage", "network:api.example.com"]
})";

std::vector<std::string> ErrorsOf(const std::string& manifest) {
    std::vector<std::string> errors;
    amk::ParseManifest(manifest, errors);
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
    CHECK(amk::Sha256Hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(amk::Sha256Hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(amk::Sha256Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST_CASE(Sha256_LongInputAcrossBlocks) {
    std::string million(1000000, 'a');
    CHECK(amk::Sha256Hex(million) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST_CASE(Sha256_FileMatchesText) {
    std::filesystem::path file = ScratchDir("sha-file") / "data.bin";
    std::string content(200000, 'x');
    WriteFile(file, content);
    std::optional<std::string> digest = amk::Sha256File(file);
    CHECK(digest.has_value());
    CHECK(digest && *digest == amk::Sha256Hex(content));
    CHECK(!amk::Sha256File(file.parent_path() / "absent.bin").has_value());
}

TEST_CASE(Sha256_HexFormatIsStrict) {
    CHECK(amk::IsSha256Hex(std::string(64, 'a')));
    CHECK(!amk::IsSha256Hex(std::string(64, 'A')));
    CHECK(!amk::IsSha256Hex(std::string(63, 'a')));
    CHECK(!amk::IsSha256Hex(std::string(64, 'g')));
}

// ---- JSON --------------------------------------------------------------------------------------

TEST_CASE(Json_ParsesNestedDocument) {
    std::string error;
    std::optional<amk::json::Value> document =
        amk::json::Parse(R"({"a": [1, -2.5e1, true, null, "x"], "b": {"c": false}})", error);
    CHECK(document.has_value());
    if (!document) return;
    const amk::json::Value* a = document->Find("a");
    CHECK(a != nullptr && a->IsArray() && a->Items().size() == 5);
    CHECK(a && a->Items()[1].AsNumber() == -25.0);
    CHECK(a && a->Items()[3].IsNull());
    const amk::json::Value* b = document->Find("b");
    CHECK(b && b->Find("c") && !b->Find("c")->AsBool());
    CHECK(document->Find("missing") == nullptr);
}

TEST_CASE(Json_DecodesEscapesAndSurrogatePairs) {
    std::string error;
    std::optional<amk::json::Value> document =
        amk::json::Parse(R"(["\u00e9", "\ud83c\udfb5", "tab\there\n", "\"q\""])", error);
    CHECK(document.has_value());
    if (!document) return;
    CHECK(document->Items()[0].AsString() == "\xC3\xA9");          // é
    CHECK(document->Items()[1].AsString() == "\xF0\x9F\x8E\xB5");  // U+1F3B5
    CHECK(document->Items()[2].AsString() == "tab\there\n");
    CHECK(document->Items()[3].AsString() == "\"q\"");
}

TEST_CASE(Json_RejectsMalformedDocuments) {
    std::string error;
    CHECK(!amk::json::Parse(R"({"a": 1,})", error).has_value());
    CHECK(!amk::json::Parse(R"({"a": 1} x)", error).has_value());
    CHECK(!amk::json::Parse(R"({"a": 1, "a": 2})", error).has_value());
    CHECK(error.find("membre en double") != std::string::npos);
    CHECK(!amk::json::Parse("\"unterminated", error).has_value());
    CHECK(!amk::json::Parse("[01]", error).has_value());
    CHECK(!amk::json::Parse("\"\\ud800\"", error).has_value());
    CHECK(!amk::json::Parse("\"raw\ncontrol\"", error).has_value());
    CHECK(!amk::json::Parse("", error).has_value());
}

TEST_CASE(Json_RefusesExcessiveNesting) {
    std::string deep(100, '[');
    deep += std::string(100, ']');
    std::string error;
    CHECK(!amk::json::Parse(deep, error).has_value());
    CHECK(error.find("imbrication") != std::string::npos);
}

TEST_CASE(Json_QuoteEscapesControlCharacters) {
    CHECK(amk::json::Quote("a\"b\\c\n") == "\"a\\\"b\\\\c\\n\"");
    CHECK(amk::json::Quote(std::string(1, '\x01')) == "\"\\u0001\"");
}

// ---- Manifests ---------------------------------------------------------------------------------

TEST_CASE(Manifest_ValidExtensionParses) {
    std::vector<std::string> errors;
    std::optional<amk::ElementManifest> manifest = amk::ParseManifest(kValidExtension, errors);
    CHECK(errors.empty());
    CHECK(manifest.has_value());
    if (!manifest) return;
    CHECK(manifest->id == "demo");
    CHECK(manifest->name == "Démo");
    CHECK(manifest->kind == "extension");
    CHECK(manifest->main && *manifest->main == "main.js");
    CHECK(manifest->permissions.size() == 2);
}

TEST_CASE(Manifest_RejectsUnknownKeysAndMissingFields) {
    std::vector<std::string> errors = ErrorsOf(R"({"id": "demo", "surprise": true})");
    CHECK(MentionsField(errors, "surprise : champ inconnu"));
    CHECK(MentionsField(errors, "name : champ obligatoire absent"));
    CHECK(MentionsField(errors, "kitApi : champ obligatoire absent"));
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
    CHECK(MentionsField(ErrorsOf(text), "kitApi : version d'API non prise en charge"));
    CHECK(ErrorsOf(kValidExtension).empty());
}

TEST_CASE(Manifest_RejectsUnknownPermissionAndDuplicates) {
    std::string text = kValidExtension;
    text.replace(text.find("\"storage\""), 9, "\"fairplay\"");
    CHECK(MentionsField(ErrorsOf(text), "permissions[0]"));

    std::string duplicate = kValidExtension;
    duplicate.replace(duplicate.find("\"network:api.example.com\""), 25, "\"storage\"");
    CHECK(MentionsField(ErrorsOf(duplicate), "permissions[1] : valeur en double"));
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
    WriteFile(good / "main.js", "AMK.log('ok');");
    CHECK(amk::ValidateElementFolder(good).ok());

    std::filesystem::path missingMain = root / "demo-no-main";
    WriteFile(missingMain / "manifest.json", kValidExtension);
    amk::FolderValidation missing = amk::ValidateElementFolder(missingMain);
    // The id is not the folder name either, so both problems are reported.
    CHECK(!missing.ok());
    CHECK(MentionsField(missing.errors, "main : fichier absent du dossier : main.js"));
    CHECK(MentionsField(missing.errors, "id : doit être le nom du dossier"));

    amk::FolderValidation noManifest = amk::ValidateElementFolder(root / "empty");
    CHECK(!noManifest.ok());
    CHECK(MentionsField(noManifest.errors, "manifest.json absent"));
}

TEST_CASE(Folder_ThemeAndSnippetRules) {
    std::filesystem::path root = ScratchDir("folder-kinds");

    WriteFile(root / "theme-a" / "manifest.json",
              R"({"id": "theme-a", "name": "T", "version": "1.0.0", "kind": "theme", "author": "a",
                  "description": "d", "kitApi": "1.0"})");
    amk::FolderValidation theme = amk::ValidateElementFolder(root / "theme-a");
    CHECK(MentionsField(theme.errors, "colors ou resources"));

    WriteFile(root / "snip" / "manifest.json",
              R"({"id": "snip", "name": "S", "version": "1.0.0", "kind": "snippet", "author": "a",
                  "description": "d", "kitApi": "1.0", "main": "x.js"})");
    WriteFile(root / "snip" / "x.js", "");
    amk::FolderValidation snippet = amk::ValidateElementFolder(root / "snip");
    CHECK(MentionsField(snippet.errors, "resources : requis pour un snippet"));
    CHECK(MentionsField(snippet.errors, "main : interdit pour un snippet"));
}

// ---- Catalog -----------------------------------------------------------------------------------

namespace {

const char* const kCatalogEntry = R"({
  "id": "demo",
  "name": "Démo",
  "version": "1.2.3",
  "kind": "extension",
  "author": "Shirowwww",
  "description": "Une extension de test.",
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
    std::string path = std::string(AMK_SOURCE_DIR) + "/catalog/catalog.json";
    std::vector<std::string> errors;
    std::optional<amk::Catalog> catalog = amk::ParseCatalog(ReadFile(path), false, errors);
    CHECK(errors.empty());
    CHECK(catalog.has_value());
}

TEST_CASE(Catalog_ValidEntryIsFound) {
    std::vector<std::string> errors;
    std::optional<amk::Catalog> catalog = amk::ParseCatalog(CatalogWith(kCatalogEntry), false, errors);
    CHECK(errors.empty());
    CHECK(catalog.has_value());
    if (!catalog) return;
    const amk::CatalogEntry* entry = amk::FindCatalogEntry(*catalog, "demo");
    CHECK(entry != nullptr);
    CHECK(entry && entry->url == "https://example.com/demo-1.2.3.zip");
    CHECK(amk::FindCatalogEntry(*catalog, "absent") == nullptr);
}

TEST_CASE(Catalog_RefusesPlainHttpAndLocalFilesFromRemoteCatalog) {
    std::string http = kCatalogEntry;
    http.replace(http.find("https://"), 8, "http://");
    std::vector<std::string> errors;
    CHECK(!amk::ParseCatalog(CatalogWith(http), false, errors).has_value());
    CHECK(MentionsField(errors, "extensions[0].url"));

    std::string file = kCatalogEntry;
    const std::string remote = "https://example.com/demo-1.2.3.zip";
    file.replace(file.find(remote), remote.size(), "file:///C:/dev/demo-1.2.3.zip");
    errors.clear();
    CHECK(!amk::ParseCatalog(CatalogWith(file), false, errors).has_value());

    // A catalog read from a local file may point at local archives: this is how development works.
    errors.clear();
    CHECK(amk::ParseCatalog(CatalogWith(file), true, errors).has_value());
}

TEST_CASE(Catalog_RejectsBadChecksumDuplicatesAndUnknownKeys) {
    std::string badSum = kCatalogEntry;
    badSum.replace(badSum.find("0000"), 4, "ABCD");
    std::vector<std::string> errors;
    CHECK(!amk::ParseCatalog(CatalogWith(badSum), false, errors).has_value());
    CHECK(MentionsField(errors, "extensions[0].sha256"));

    errors.clear();
    std::string twice = std::string(kCatalogEntry) + "," + kCatalogEntry;
    CHECK(!amk::ParseCatalog(CatalogWith(twice), false, errors).has_value());
    CHECK(MentionsField(errors, "extensions[1].id : identifiant déjà présent"));

    errors.clear();
    std::string extra = kCatalogEntry;
    extra.insert(extra.size() - 1, R"(, "stars": 5)");
    CHECK(!amk::ParseCatalog(CatalogWith(extra), false, errors).has_value());
    CHECK(MentionsField(errors, "extensions[0].stars : champ inconnu"));
}

TEST_CASE(Catalog_RejectsUnknownCatalogVersion) {
    std::vector<std::string> errors;
    CHECK(!amk::ParseCatalog(R"({"catalogVersion": 2, "extensions": []})", false, errors).has_value());
    CHECK(MentionsField(errors, "catalogVersion"));
}

// ---- Archives ----------------------------------------------------------------------------------

TEST_CASE(Archive_RefusesEscapingEntries) {
    std::string reason;
    CHECK(amk::IsSafeArchiveEntry("manifest.json", reason));
    CHECK(amk::IsSafeArchiveEntry("scripts/main.js", reason));
    CHECK(amk::IsSafeArchiveEntry("assets/", reason));
    CHECK(!amk::IsSafeArchiveEntry("../evil.js", reason));
    CHECK(!amk::IsSafeArchiveEntry("a/../../evil.js", reason));
    CHECK(!amk::IsSafeArchiveEntry("/etc/passwd", reason));
    CHECK(!amk::IsSafeArchiveEntry("C:/Windows/evil.dll", reason));
    CHECK(!amk::IsSafeArchiveEntry("..\\evil.js", reason));
    CHECK(!amk::IsSafeArchiveEntry("a//b", reason));
    CHECK(!amk::IsSafeArchiveEntry("./main.js", reason));
    CHECK(!amk::IsSafeArchiveEntry("", reason));
    CHECK(!amk::IsSafeArchiveEntry("bad\nname", reason));
}

TEST_CASE(Archive_ListingReportsFirstProblem) {
    CHECK(amk::CheckArchiveListing({"manifest.json", "main.js"}).empty());
    std::string problem = amk::CheckArchiveListing({"manifest.json", "../x.js"});
    CHECK(problem.find("archive refusée") == 0);
}

TEST_CASE(Archive_DetectsLinksInExtractedTree) {
    std::filesystem::path root = ScratchDir("archive-links");
    WriteFile(root / "manifest.json", "{}");
    CHECK(!amk::ContainsLink(root));
    std::error_code ec;
    std::filesystem::create_directory_symlink(root / "manifest.json", root / "link.json", ec);
    if (!ec) CHECK(amk::ContainsLink(root));
}

// ---- Versions and release metadata ----------------------------------------------------------------

TEST_CASE(Version_ComparesNumerically) {
    CHECK(amk::CompareReleaseVersions("0.10.0", "0.9.0") == 1);
    CHECK(amk::CompareReleaseVersions("v1.2.3", "1.2.3") == 0);
    CHECK(amk::CompareReleaseVersions("1.2", "1.2.0") == 0);
    CHECK(amk::CompareReleaseVersions("0.1.0", "0.2.0") == -1);
}

TEST_CASE(Release_ParsesGitHubJson) {
    std::string error;
    std::optional<amk::ReleaseInfo> release = amk::ParseRelease(R"({
        "tag_name": "v0.2.0",
        "assets": [
          {"name": "amk-kit-0.2.0-windows-x64.zip", "browser_download_url": "https://example.com/kit.zip"},
          {"name": "SHA256SUMS.txt", "browser_download_url": "https://example.com/SHA256SUMS.txt", "size": 9}
        ]})",
                                                                 error);
    CHECK(release.has_value());
    if (!release) return;
    CHECK(release->version == "0.2.0");
    CHECK(release->assets.size() == 2);
    CHECK(release->assets.at("SHA256SUMS.txt") == "https://example.com/SHA256SUMS.txt");

    CHECK(!amk::ParseRelease(R"({"name": "no tag"})", error).has_value());
}

TEST_CASE(Release_ChecksumsAcceptBothFormats) {
    std::string hash(64, 'a');
    std::string text = hash + "  kit.zip\r\n" + std::string(64, 'b') + " *other.zip\n" + "not a line\n" +
                       std::string(64, 'C') + "  upper.zip\n";
    std::map<std::string, std::string> sums = amk::ParseChecksums(text);
    CHECK(sums.size() == 2);
    CHECK(sums["kit.zip"] == hash);
    CHECK(sums["other.zip"] == std::string(64, 'b'));
    CHECK(sums.count("upper.zip") == 0);
}

// ---- Kit layout, elements and activation -------------------------------------------------------------

TEST_CASE(Kit_ListsValidElementsAndSkipsWorkInProgress) {
    std::filesystem::path root = ScratchDir("kit-list");
    amk::KitLayout kit(root);
    WriteFile(kit.Element("extension", "demo") / "manifest.json", kValidExtension);
    WriteFile(kit.Element("extension", "demo") / "main.js", "");
    WriteFile(kit.Folder("extension") / ".demo.previous" / "manifest.json", kValidExtension);
    WriteFile(kit.Element("theme", "broken") / "manifest.json", "{ not json");

    std::vector<amk::InstalledElement> elements = amk::ListInstalled(kit);
    CHECK(elements.size() == 2);
    if (elements.size() != 2) return;
    CHECK(elements[0].kind == "extension" && elements[0].id == "demo" && elements[0].Valid());
    CHECK(elements[1].kind == "theme" && !elements[1].Valid());
    CHECK(!elements[1].errors.empty());
}

TEST_CASE(Kit_EnableAndDisableWriteTheRightKeys) {
    amk::Settings settings;
    amk::SetEnabled(settings, "extension", "demo", true);
    CHECK(settings.GetBool("extensions.demo.enabled", false));
    CHECK(amk::IsEnabled(settings, "extension", "demo"));

    amk::SetEnabled(settings, "snippet", "dark", true);
    CHECK(settings.GetBool("snippets.dark.enabled", false));
    amk::SetEnabled(settings, "app", "lyrics", true);
    CHECK(settings.GetBool("apps.lyrics.enabled", false));

    amk::SetEnabled(settings, "theme", "night", true);
    CHECK(settings.GetString("theme.active", "") == "night");
    amk::SetEnabled(settings, "theme", "day", false);  // another theme: the active one stays
    CHECK(settings.GetString("theme.active", "") == "night");
    amk::SetEnabled(settings, "theme", "night", false);
    CHECK(settings.GetString("theme.active", "x") == "");

    CHECK(amk::EnabledKey("extension", "atmos-leveler") == "extensions.atmos-leveler.enabled");
}

TEST_CASE(Activation_ReportsWhatLoadsAndWhatCannot) {
    std::filesystem::path root = ScratchDir("kit-active");
    amk::KitLayout kit(root);
    WriteFile(kit.Element("extension", "demo") / "manifest.json", kValidExtension);
    WriteFile(kit.Element("extension", "demo") / "main.js", "");
    WriteFile(kit.Element("theme", "night") / "manifest.json",
              R"({"id": "night", "name": "Nuit", "version": "2.0.0", "kind": "theme", "author": "a",
                  "description": "d", "kitApi": "1.0", "colors": "colors.ini", "defaultScheme": "Dark"})");
    WriteFile(kit.Element("theme", "night") / "colors.ini", "[Dark]\naccent = FA2D48\n");

    amk::Settings settings;
    settings.SetBool("extensions.demo.enabled", true);
    settings.SetBool("extensions.atmos-leveler.enabled", true);
    settings.SetBool("extensions.gone.enabled", true);
    settings.Set("theme.active", "night");

    amk::ActiveReport report = amk::ComputeActive(kit, settings);
    CHECK(report.items.size() == 3);
    CHECK(report.problems.size() == 1);
    CHECK(report.problems.size() == 1 && report.problems[0].find("extension gone") != std::string::npos);

    bool sawTheme = false;
    for (const amk::ActiveItem& item : report.items) {
        if (item.kind == "theme") {
            sawTheme = true;
            CHECK(item.id == "night");
            CHECK(item.scheme == "Dark");
        }
    }
    CHECK(sawTheme);

    std::string text = amk::FormatActive(report);
    CHECK(text.find("extension demo 1.2.3\n") != std::string::npos);
    CHECK(text.find("extension atmos-leveler builtin\n") != std::string::npos);
    CHECK(text.find("theme night 2.0.0 scheme=Dark\n") != std::string::npos);
}

TEST_CASE(Activation_InvalidEnabledElementIsAProblemNotAnItem) {
    std::filesystem::path root = ScratchDir("kit-invalid-active");
    amk::KitLayout kit(root);
    WriteFile(kit.Element("extension", "demo") / "manifest.json", kValidExtension);  // main.js missing

    amk::Settings settings;
    settings.SetBool("extensions.demo.enabled", true);
    amk::ActiveReport report = amk::ComputeActive(kit, settings);
    CHECK(report.items.empty());
    CHECK(report.problems.size() == 1);
}

TEST_CASE(WriteTextFile_CreatesFoldersAndReplacesAtomically) {
    std::filesystem::path file = ScratchDir("write-text") / "deep" / "active.txt";
    std::string error;
    CHECK(amk::WriteTextFile(file, "first\n", error));
    CHECK(amk::WriteTextFile(file, "second\n", error));
    CHECK(ReadFile(file) == "second\n");
    CHECK(!std::filesystem::exists(file.string() + ".tmp"));
}

// ---- Backups -----------------------------------------------------------------------------------

TEST_CASE(Backup_CreateListAndRestore) {
    std::filesystem::path root = ScratchDir("backup-cycle");
    amk::KitLayout kit(root);
    WriteFile(kit.Settings(), "theme.active = night\n");
    WriteFile(kit.Element("theme", "night") / "manifest.json", "{}");

    std::string error;
    std::optional<std::string> first = amk::CreateBackup(kit, error);
    CHECK(first.has_value());
    if (!first) {
        std::cerr << "backup failed: " << error << '\n';
        return;
    }
    CHECK(amk::ListBackups(kit).size() == 1);

    // Change the configuration: a new theme, the old one removed, settings edited.
    WriteFile(kit.Settings(), "theme.active = day\n");
    std::filesystem::remove_all(kit.Element("theme", "night"));
    WriteFile(kit.Element("theme", "day") / "manifest.json", "{}");

    CHECK(amk::RestoreBackup(kit, *first, error));
    CHECK(ReadFile(kit.Settings()) == "theme.active = night\n");
    CHECK(std::filesystem::exists(kit.Element("theme", "night") / "manifest.json"));
    CHECK(!std::filesystem::exists(kit.Element("theme", "day")));

    // The restore itself was preceded by a snapshot of the state it replaced.
    CHECK(amk::ListBackups(kit).size() == 2);
}

TEST_CASE(Backup_RestoreRefusesUnsafeOrUnknownNames) {
    std::filesystem::path root = ScratchDir("backup-names");
    amk::KitLayout kit(root);
    std::string error;
    CHECK(!amk::RestoreBackup(kit, "../etc", error));
    CHECK(!amk::RestoreBackup(kit, "", error));
    CHECK(!amk::RestoreBackup(kit, "20990101-000000", error));
    CHECK(error.find("introuvable") != std::string::npos);
}

TEST_CASE(Backup_WithoutSettingsStillCreatesTheSnapshot) {
    std::filesystem::path root = ScratchDir("backup-empty");
    amk::KitLayout kit(root);
    std::string error;
    std::optional<std::string> name = amk::CreateBackup(kit, error);
    CHECK(name.has_value());
    if (name) {
        CHECK(std::filesystem::is_directory(kit.Backups() / *name / "extensions"));
        CHECK(!std::filesystem::exists(kit.Backups() / *name / "settings.txt"));
    }
}

// ---- Change detection --------------------------------------------------------------------------------

TEST_CASE(Fingerprint_ChangesOnlyWhenTheKitChanges) {
    std::filesystem::path root = ScratchDir("fingerprint");
    amk::KitLayout kit(root);
    WriteFile(kit.Settings(), "a = 1\n");
    WriteFile(kit.Element("theme", "night") / "colors.ini", "[Dark]\n");

    std::string before = amk::KitFingerprint(kit);
    CHECK(before == amk::KitFingerprint(kit));

    // Give the file a new modification time, then edit it. The fingerprint must follow.
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    WriteFile(kit.Element("theme", "night") / "colors.ini", "[Dark]\naccent = FA2D48\n");
    CHECK(amk::KitFingerprint(kit) != before);
    CHECK(amk::KitFingerprint(kit).size() == 16);
}

// ---- Fixed-width output helpers used by the commands ----------------------------------------------

TEST_CASE(Path_ElementIdsAreCheckedBeforeUse) {
    CHECK(amk::IsValidId("atmos-leveler"));
    CHECK(!amk::IsValidId("../x"));
    CHECK(!amk::IsValidId("x"));
    CHECK(!amk::IsValidId("Demo"));
    CHECK(amk::IsValidVersion("1.0.0"));
    CHECK(!amk::IsValidVersion("1.0"));
    CHECK(amk::IsHttpsUrl("https://example.com/a.zip"));
    CHECK(!amk::IsHttpsUrl("http://example.com/a.zip"));
}

TEST_CASE(Crc32_MatchesTheStandardCheckValue) {
    CHECK(amk::cli::Crc32("123456789") == 0xCBF43926u);
    CHECK(amk::cli::Crc32("") == 0u);
}

TEST_CASE(WriteZip_IsDeterministicAndRefusesUnsafeNames) {
    std::filesystem::path root = ScratchDir("zip-writer");
    std::string error;
    std::vector<amk::cli::ZipSource> files = {{"manifest.json", {}, "{}"}, {"main.js", {}, "AMK.log(1);\n"}};
    CHECK(amk::cli::WriteZip(root / "a.zip", files, error));
    CHECK(amk::cli::WriteZip(root / "b.zip", files, error));

    std::ifstream first(root / "a.zip", std::ios::binary);
    std::ifstream second(root / "b.zip", std::ios::binary);
    std::string a((std::istreambuf_iterator<char>(first)), std::istreambuf_iterator<char>());
    std::string b((std::istreambuf_iterator<char>(second)), std::istreambuf_iterator<char>());
    CHECK(a == b);
    CHECK(a.compare(0, 4, "PK\x03\x04") == 0);

    CHECK(!amk::cli::WriteZip(root / "bad.zip", {{"../escape.txt", {}, "x"}}, error));
    CHECK(!error.empty());
    CHECK(!std::filesystem::exists(root / "bad.zip"));
}

TEST_CASE(Manifest_ProcessesAreOneOrTwoDistinctNames) {
    const std::string base =
        R"({"id": "demo", "name": "Démo", "version": "1.0.0", "kind": "extension", "author": "Shirowwww", )"
        R"("description": "Test.", "kitApi": "1.0", "main": "main.js", )";
    std::vector<std::string> errors;
    CHECK(amk::ParseManifest(base + R"("processes": ["ui"]})", errors).has_value());
    CHECK(amk::ParseManifest(base + R"("processes": ["ui", "engine"]})", errors).has_value());
    CHECK(errors.empty());
    CHECK(!amk::ParseManifest(base + R"("processes": []})", errors).has_value());
    CHECK(!amk::ParseManifest(base + R"("processes": ["ui", "ui"]})", errors).has_value());
    CHECK(!amk::ParseManifest(base + R"("processes": ["gpu"]})", errors).has_value());
    CHECK(!errors.empty());
}

