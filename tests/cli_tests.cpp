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

#include "../src/core/json.hpp"
#include "../src/core/manifest.hpp"
#include "../src/core/text_util.hpp"
#include "../src/cli/anonymize.hpp"
#include "../src/cli/archive.hpp"
#include "../src/cli/backup.hpp"
#include "../src/cli/catalog.hpp"
#include "../src/cli/compile.hpp"
#include "../src/cli/element.hpp"
#include "../src/cli/kit.hpp"
#include "../src/cli/release.hpp"
#include "../src/cli/sha256.hpp"
#include "../src/cli/watch.hpp"
#include "../src/cli/zip_writer.hpp"
#include "support.hpp"
#include "test.hpp"

namespace {

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

// A manifest under the command line's rules, for a folder named demo, without looking at its files.
std::optional<ampify::Manifest> ParseAuthored(const std::string& manifest, std::vector<std::string>& errors) {
    ampify::ManifestResult result = ampify::ParseManifest(manifest, "demo", 1, nullptr, ampify::ManifestMode::Author);
    errors.insert(errors.end(), result.errors.begin(), result.errors.end());
    return result.manifest;
}

std::vector<std::string> ErrorsOf(const std::string& manifest) {
    std::vector<std::string> errors;
    ParseAuthored(manifest, errors);
    return errors;
}

bool MentionsField(const std::vector<std::string>& errors, const std::string& field) {
    for (const std::string& error : errors) {
        if (error.rfind(field, 0) == 0) return true;
    }
    return false;
}

}  // namespace

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
    ampify::test::ScratchDir scratch("sha-file");
    std::filesystem::path file = scratch / "data.bin";
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

namespace {

// How the command line reads manifests and catalogs: strict, and a repeated key is an error.
std::optional<ampify::Json> ParseStrict(std::string_view text, std::string& error) {
    return ampify::Json::Parse(text, &error, ampify::Json::DuplicateKeys::Refuse);
}

}  // namespace

TEST_CASE(Json_ParsesNestedDocument) {
    std::string error;
    std::optional<ampify::Json> document = ParseStrict(R"({"a": [1, -2.5e1, true, null, "x"], "b": {"c": false}})", error);
    CHECK(document.has_value());
    if (!document) return;
    const ampify::Json* a = document->Find("a");
    CHECK(a != nullptr && a->IsArray() && a->AsArray()->size() == 5);
    CHECK(a && *(*a->AsArray())[1].AsNumber() == -25.0);
    CHECK(a && (*a->AsArray())[3].IsNull());
    const ampify::Json* b = document->Find("b");
    CHECK(b && b->Find("c") && !*b->Find("c")->AsBool());
    CHECK(document->Find("missing") == nullptr);
}

TEST_CASE(Json_StrictDecodesEscapesAndSurrogatePairs) {
    std::string error;
    std::optional<ampify::Json> document = ParseStrict(R"(["é", "🎵", "tab\there\n", "\"q\""])", error);
    CHECK(document.has_value() && document->AsArray()->size() == 4);
    if (!document || document->AsArray()->size() != 4) return;
    const ampify::Json::Array& items = *document->AsArray();
    CHECK(*items[0].AsString() == "\xC3\xA9");          // é
    CHECK(*items[1].AsString() == "\xF0\x9F\x8E\xB5");  // U+1F3B5
    CHECK(*items[2].AsString() == "tab\there\n");
    CHECK(*items[3].AsString() == "\"q\"");
}

TEST_CASE(Json_ReadsADocumentSavedWithAByteOrderMark) {
    // Notepad and PowerShell 5 start a UTF-8 file with EF BB BF: both readers of manifest.json skip it.
    std::optional<ampify::Json> kernel = ampify::Json::Parse("\xEF\xBB\xBF{\"id\": \"my-theme\"}");
    CHECK(kernel && kernel->Find("id") && kernel->Find("id")->AsString() && *kernel->Find("id")->AsString() == "my-theme");
    std::string error;
    std::optional<ampify::Json> cli = ParseStrict("\xEF\xBB\xBF{\"id\": \"my-theme\"}", error);
    CHECK(cli && cli->Find("id") && cli->Find("id")->AsString() && *cli->Find("id")->AsString() == "my-theme");
}

TEST_CASE(Json_RejectsMalformedDocuments) {
    std::string error;
    CHECK(!ParseStrict(R"({"a": 1,})", error).has_value());
    CHECK(!ParseStrict(R"({"a": 1} x)", error).has_value());
    error.clear();
    CHECK(!ParseStrict(R"({"a": 1, "a": 2})", error).has_value());
    CHECK(error.find("duplicate member") != std::string::npos);
    CHECK(ampify::Json::Parse(R"({"a": 1, "a": 2})").has_value());  // the kernel's own files allow it
    CHECK(!ParseStrict("\"unterminated", error).has_value());
    CHECK(!ParseStrict("[01]", error).has_value());
    CHECK(!ParseStrict("\"\\ud800\"", error).has_value());
    CHECK(!ParseStrict("\"raw\ncontrol\"", error).has_value());
    CHECK(!ParseStrict("", error).has_value());
}

TEST_CASE(Json_RefusesExcessiveNesting) {
    std::string deep(100, '[');
    deep += std::string(100, ']');
    std::string error;
    CHECK(!ParseStrict(deep, error).has_value());
    CHECK(error.find("nesting") != std::string::npos);
}

TEST_CASE(Json_QuoteEscapesControlCharacters) {
    CHECK(ampify::Json::Quote("a\"b\\c\n") == "\"a\\\"b\\\\c\\n\"");
    CHECK(ampify::Json::Quote(std::string(1, '\x01')) == "\"\\u0001\"");
}

TEST_CASE(Manifest_ValidExtensionParses) {
    std::vector<std::string> errors;
    std::optional<ampify::Manifest> manifest = ParseAuthored(kValidExtension, errors);
    CHECK(errors.empty());
    CHECK(manifest.has_value());
    if (!manifest) return;
    CHECK(manifest->id == "demo");
    CHECK(manifest->name == "Demo");
    CHECK(manifest->kind == ampify::ElementKind::Extension);
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
    ampify::test::ScratchDir root("folder-rules");

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
    ampify::test::ScratchDir root("folder-kinds");

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
    CHECK(MentionsField(snippet.errors, "resources or user.css: at least one of the two is required for a snippet"));
    CHECK(MentionsField(snippet.errors, "main: not allowed for a snippet"));

    // A snippet made of a user.css alone, as most Spicetify snippets are.
    WriteFile(root / "css-only" / "manifest.json",
              R"({"id": "css-only", "name": "S", "version": "1.0.0", "kind": "snippet", "author": "a",
                  "description": "d", "kitApi": "1.0"})");
    WriteFile(root / "css-only" / "user.css", ".card .artwork { corner-radius: 16; }\n");
    ampify::FolderValidation cssOnly = ampify::ValidateElementFolder(root / "css-only");
    CHECK(cssOnly.errors.empty());
    std::string text = R"({"id": "css-only", "name": "S", "version": "1.0.0", "kind": "snippet", "author": "a",
                           "description": "d", "kitApi": "1.0"})";
    auto inFolder = [](const std::string& file) { return file == "user.css"; };
    CHECK(ampify::ParseManifest(text, "css-only", 1, inFolder).errors.empty());
    auto empty = [](const std::string&) { return false; };
    CHECK(!ampify::ParseManifest(text, "css-only", 1, empty).errors.empty());
}

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

// The public catalog lives in its own repository: checked when a clone of ampify-catalog sits next to this one.
TEST_CASE(Catalog_PublicCatalogIsValid) {
    std::string path = std::string(AMPIFY_SOURCE_DIR) + "/../ampify-catalog/catalog.json";
    if (!std::filesystem::exists(path)) {
        std::cout << "Catalog: no ampify-catalog clone next to the repository, test skipped\n";
        return;
    }
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

    // A field a later catalog adds is left aside: Ampify 1.1.0 refused the whole catalog for one, and every user of it
    // would have lost the Marketplace when the catalog gained a field.
    errors.clear();
    std::string extra = kCatalogEntry;
    extra.insert(extra.size() - 1, R"(, "stars": 5)");
    std::optional<ampify::Catalog> later = ampify::ParseCatalog(CatalogWith(extra), false, errors);
    CHECK(later.has_value() && later->entries.size() == 1);
    CHECK(errors.empty());
}

namespace {

// The demo entry at 1.2.3 for every Ampify, with later versions for newer Ampify releases in `newer`.
std::string CatalogWithNewer(const std::string& newer) {
    std::string entry = kCatalogEntry;
    entry.insert(entry.size() - 1, R"(, "newer": [)" + newer + "]");
    return CatalogWith(entry);
}

std::string NewerDemo(const std::string& version, const std::string& minAmpify) {
    return R"({"id": "demo", "name": "Demo", "version": ")" + version + R"(", "kind": "extension",
      "author": "Shirowwww", "description": "A test extension.", "kitApi": "1.0", "minAmpify": ")" + minAmpify +
           R"(", "url": "https://example.com/demo-)" + version + R"(.zip",
      "sha256": "1111111111111111111111111111111111111111111111111111111111111111", "permissions": ["storage"]})";
}

}  // namespace

// Ampify 1.2.1 and older read only the entry itself, so the catalog keeps their version there and lists later ones in
// `newer`: each Ampify takes the newest version whose minAmpify it reaches.
TEST_CASE(Catalog_TakesTheNewestVersionThisAmpifyReaches) {
    std::string text = CatalogWithNewer(NewerDemo("1.4.0", "9.0.0") + "," + NewerDemo("1.3.0", "1.3.0"));
    std::vector<std::string> errors;
    std::optional<ampify::Catalog> current = ampify::ParseCatalog(text, false, errors, "1.3.0");
    CHECK(errors.empty());
    CHECK(current && current->entries.size() == 1);
    CHECK(current && current->entries[0].version == "1.3.0");
    CHECK(current && current->entries[0].url == "https://example.com/demo-1.3.0.zip");

    std::optional<ampify::Catalog> older = ampify::ParseCatalog(text, false, errors, "1.2.9");
    CHECK(older && older->entries[0].version == "1.2.3");
    std::optional<ampify::Catalog> later = ampify::ParseCatalog(text, false, errors, "9.0.0");
    CHECK(later && later->entries[0].version == "1.4.0");
    CHECK(errors.empty());
}

TEST_CASE(Catalog_LeavesOutAnElementThisAmpifyIsTooOldFor) {
    std::string entry = kCatalogEntry;
    entry.insert(entry.size() - 1, R"(, "minAmpify": "9.0.0")");
    std::vector<std::string> errors;
    std::optional<ampify::Catalog> catalog = ampify::ParseCatalog(CatalogWith(entry), false, errors, "1.3.0");
    CHECK(errors.empty());
    CHECK(catalog && catalog->entries.empty());
}

TEST_CASE(Catalog_RefusesANewerVersionOfAnotherElement) {
    std::vector<std::string> errors;
    std::string other = NewerDemo("1.3.0", "1.3.0");
    other.replace(other.find("\"demo\""), 6, "\"other\"");
    CHECK(!ampify::ParseCatalog(CatalogWithNewer(other), false, errors, "1.3.0").has_value());
    CHECK(MentionsField(errors, "extensions[0].newer[0].id"));

    errors.clear();
    CHECK(!ampify::ParseCatalog(CatalogWithNewer(NewerDemo("1.3.0", "1.3")), false, errors, "1.3.0").has_value());
    CHECK(MentionsField(errors, "extensions[0].newer[0].minAmpify"));

    errors.clear();
    std::string nested = NewerDemo("1.3.0", "1.3.0");
    nested.insert(nested.size() - 1, R"(, "newer": [])");
    CHECK(!ampify::ParseCatalog(CatalogWithNewer(nested), false, errors, "1.3.0").has_value());
    CHECK(MentionsField(errors, "extensions[0].newer[0].newer"));
}

TEST_CASE(Catalog_TagsAndPreviewAreChecked) {
    auto with = [](const std::string& fields) {
        std::string entry = kCatalogEntry;
        entry.insert(entry.size() - 1, ", " + fields);
        return CatalogWith(entry);
    };
    std::vector<std::string> errors;
    std::optional<ampify::Catalog> catalog = ampify::ParseCatalog(
        with(R"("tags": ["dark", "glass"], "preview": "https://example.com/demo.png")"), false, errors);
    CHECK(errors.empty());
    CHECK(catalog && catalog->entries[0].tags.size() == 2);
    CHECK(catalog && catalog->entries[0].preview == std::optional<std::string>("https://example.com/demo.png"));

    CHECK(!ampify::ParseCatalog(with(R"("tags": ["Dark"])"), false, errors).has_value());
    CHECK(MentionsField(errors, "extensions[0].tags[0]"));
    errors.clear();
    CHECK(!ampify::ParseCatalog(with(R"("preview": "http://example.com/demo.png")"), false, errors).has_value());
    CHECK(MentionsField(errors, "extensions[0].preview"));
    errors.clear();
    CHECK(!ampify::ParseCatalog(with(R"("preview": "https://example.com/demo.gif")"), false, errors).has_value());
    CHECK(!ampify::ParseCatalog(with(R"("preview": 3)"), false, errors).has_value());
    std::string longUrl = "https://example.com/" + std::string(500, 'a') + ".png";
    CHECK(!ampify::ParseCatalog(with("\"preview\": \"" + longUrl + "\""), false, errors).has_value());
}

TEST_CASE(Catalog_UrlHostIsTheLowercaseAuthorityOfAnHttpsAddress) {
    CHECK(ampify::UrlHost("https://Example.com/a/b.png") == "example.com");
    CHECK(ampify::UrlHost("https://example.com:8443/x?y#z") == "example.com:8443");
    CHECK(ampify::UrlHost("https://example.com") == "example.com");
    CHECK(ampify::UrlHost("http://example.com/a.png").empty());
    CHECK(ampify::UrlHost("file:///C:/a.png").empty());
    CHECK(ampify::UrlHost("https://good.com@evil.com/a.png").empty());
}

TEST_CASE(Catalog_RejectsUnknownCatalogVersion) {
    std::vector<std::string> errors;
    CHECK(!ampify::ParseCatalog(R"({"catalogVersion": 2, "extensions": []})", false, errors).has_value());
    CHECK(MentionsField(errors, "catalogVersion"));
}

TEST_CASE(Catalog_AnArchiveMustAskForThePermissionsItsEntryShowed) {
    ampify::CatalogEntry entry;
    entry.id = "demo";
    entry.version = "1.0.0";
    entry.permissions = {"storage", "network:example.com"};
    ampify::ElementInfo manifest = entry;
    manifest.permissions = {"network:example.com", "storage"};  // the order does not matter
    CHECK(ampify::CheckArchiveManifest(manifest, entry).empty());

    manifest.permissions.push_back("player.control");
    CHECK(!ampify::CheckArchiveManifest(manifest, entry).empty());
    manifest.permissions = {"storage"};
    CHECK(!ampify::CheckArchiveManifest(manifest, entry).empty());
    manifest.permissions = entry.permissions;
    manifest.version = "1.0.1";
    CHECK(!ampify::CheckArchiveManifest(manifest, entry).empty());
}

TEST_CASE(Catalog_AnArchiveCannotHoldAMarketplacePermission) {
    ampify::CatalogEntry entry;  // an entry that slipped through with kit.manage is refused all the same
    entry.id = "demo";
    entry.version = "1.0.0";
    entry.permissions = {"kit.manage"};
    std::string problem = ampify::CheckArchiveManifest(entry, entry);
    CHECK(problem.find("kit.manage") != std::string::npos);
}

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
    ampify::test::ScratchDir root("archive-links");
    WriteFile(root / "manifest.json", "{}");
    CHECK(!ampify::ContainsLink(root));
    std::error_code ec;
    std::filesystem::create_directory_symlink(root / "manifest.json", root / "link.json", ec);
    if (!ec) CHECK(ampify::ContainsLink(root));
}

TEST_CASE(Version_ComparesNumerically) {
    CHECK(ampify::CompareDottedVersions("0.10.0", "0.9.0") == 1);
    CHECK(ampify::CompareDottedVersions("v1.2.3", "1.2.3") == 0);
    CHECK(ampify::CompareDottedVersions("1.2", "1.2.0") == 0);
    CHECK(ampify::CompareDottedVersions("0.1.0", "0.2.0") == -1);
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

TEST_CASE(Kit_ListsValidElementsAndSkipsWorkInProgress) {
    ampify::test::ScratchDir root("kit-list");
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

TEST_CASE(Kit_TheElementsThatShipWithTheKitAreOnUntilTurnedOff) {
    ampify::Settings settings;  // a new install: no key written yet
    CHECK(ampify::IsEnabled(settings, "app", "marketplace"));
    CHECK(ampify::IsEnabled(settings, "extension", "now-playing"));
    CHECK(!ampify::IsEnabled(settings, "app", "lyrics"));

    ampify::SetEnabled(settings, "app", "marketplace", false);
    CHECK(!ampify::IsEnabled(settings, "app", "marketplace"));
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

    CHECK((ampify::ElementRef{ampify::ElementKind::Extension, "atmos-leveler"}.EnabledKey() ==
           "extensions.atmos-leveler.enabled"));
}

TEST_CASE(Activation_ReportsWhatLoadsAndWhatCannot) {
    ampify::test::ScratchDir root("kit-active");
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
    ampify::test::ScratchDir root("kit-invalid-active");
    ampify::KitLayout kit(root);
    WriteFile(kit.Element("extension", "demo") / "manifest.json", kValidExtension);  // main.js missing

    ampify::Settings settings;
    settings.SetBool("extensions.demo.enabled", true);
    ampify::ActiveReport report = ampify::ComputeActive(kit, settings);
    CHECK(report.items.empty());
    CHECK(report.problems.size() == 1);
}

TEST_CASE(WriteTextFile_CreatesFoldersAndReplacesAtomically) {
    ampify::test::ScratchDir scratch("write-text");
    std::filesystem::path file = scratch / "deep" / "active.txt";
    std::string error;
    CHECK(ampify::WriteTextFile(file, "first\n", error));
    CHECK(ampify::WriteTextFile(file, "second\n", error));
    CHECK(ReadFile(file) == "second\n");
    CHECK(!std::filesystem::exists(file.string() + ".tmp"));
}

TEST_CASE(Backup_CreateListAndRestore) {
    ampify::test::ScratchDir root("backup-cycle");
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
    ampify::test::ScratchDir root("backup-names");
    ampify::KitLayout kit(root);
    std::string error;
    CHECK(!ampify::RestoreBackup(kit, "../etc", error));
    CHECK(!ampify::RestoreBackup(kit, "", error));
    CHECK(!ampify::RestoreBackup(kit, "20990101-000000", error));
    CHECK(error.find("not found") != std::string::npos);
}

TEST_CASE(Backup_WithoutSettingsStillCreatesTheSnapshot) {
    ampify::test::ScratchDir root("backup-empty");
    ampify::KitLayout kit(root);
    std::string error;
    std::optional<std::string> name = ampify::CreateBackup(kit, error);
    CHECK(name.has_value());
    if (name) {
        CHECK(std::filesystem::is_directory(kit.Backups() / *name / "extensions"));
        CHECK(!std::filesystem::exists(kit.Backups() / *name / "settings.txt"));
    }
}

TEST_CASE(Fingerprint_ChangesOnlyWhenTheKitChanges) {
    ampify::test::ScratchDir root("fingerprint");
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

TEST_CASE(Path_ElementIdsAreCheckedBeforeUse) {
    CHECK(ampify::IsElementId("atmos-leveler"));
    CHECK(!ampify::IsElementId("../x"));
    CHECK(!ampify::IsElementId("x"));
    CHECK(!ampify::IsElementId("Demo"));
    CHECK(ampify::IsReleaseVersion("1.0.0"));
    CHECK(!ampify::IsReleaseVersion("1.0"));
    CHECK(ampify::IsHttpsAddress("https://example.com/a.zip", ampify::kMaxAddressLength));
    CHECK(!ampify::IsHttpsAddress("http://example.com/a.zip", ampify::kMaxAddressLength));
}

TEST_CASE(Crc32_MatchesTheStandardCheckValue) {
    CHECK(ampify::cli::Crc32("123456789") == 0xCBF43926u);
    CHECK(ampify::cli::Crc32("") == 0u);
}

TEST_CASE(WriteZip_IsDeterministicAndRefusesUnsafeNames) {
    ampify::test::ScratchDir root("zip-writer");
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

// tools/pack.py writes the same bytes (tools/test_pack.py holds the same hash): one archive, one SHA-256, whichever
// tool made it.
TEST_CASE(WriteZip_GivesTheBytesThatPackPyGives) {
    ampify::test::ScratchDir root("zip-golden");
    std::string error;
    std::vector<ampify::cli::ZipSource> files = {
        {"main.js", {}, "Ampify.log(1);\n"}, {"manifest.json", {}, "{}"}, {"lib/\xC3\xA9.js", {}, "x=1;\n"}};
    REQUIRE(ampify::cli::WriteZip(root / "golden.zip", files, error));
    CHECK(ampify::Sha256File(root / "golden.zip") ==
          std::optional<std::string>("37e93ce42dec78b704561e8191016bad688a7414c1e2a80a5842b139211ad6fd"));
}

TEST_CASE(Manifest_ProcessesAreOneOrTwoDistinctNames) {
    const std::string base =
        R"({"id": "demo", "name": "Demo", "version": "1.0.0", "kind": "extension", "author": "Shirowwww", )"
        R"("description": "Test.", "kitApi": "1.0", "main": "main.js", )";
    std::vector<std::string> errors;
    CHECK(ParseAuthored(base + R"("processes": ["ui"]})", errors).has_value());
    CHECK(ParseAuthored(base + R"("processes": ["ui", "engine"]})", errors).has_value());
    CHECK(errors.empty());
    CHECK(!ParseAuthored(base + R"("processes": []})", errors).has_value());
    CHECK(!ParseAuthored(base + R"("processes": ["ui", "ui"]})", errors).has_value());
    CHECK(!ParseAuthored(base + R"("processes": ["gpu"]})", errors).has_value());
    CHECK(!errors.empty());
}


TEST_CASE(Manifest_TypeIsScriptOrModuleForTheCli) {
    const std::string base =
        R"({"id": "demo", "name": "Demo", "version": "1.0.0", "kind": "extension", "author": "Shirowwww", )"
        R"("description": "Test.", "kitApi": "1.0", "main": "main.js", )";
    std::vector<std::string> errors;
    std::optional<ampify::Manifest> module = ParseAuthored(base + R"("type": "module"})", errors);
    CHECK(module && module->module);
    CHECK(ParseAuthored(base + R"("type": "script"})", errors).has_value());
    CHECK(errors.empty());
    CHECK(!ParseAuthored(base + R"("type": "commonjs"})", errors).has_value());
    CHECK(MentionsField(errors, "type: invalid value"));
}

// The command line takes every member of a translation the kernel takes.
TEST_CASE(Manifest_TranslationsTakeGroupsAndSettingDescriptionsForTheCli) {
    const std::string text =
        R"({"id": "demo", "name": "Demo", "version": "1.0.0", "kind": "extension", "author": "Shirowwww", )"
        R"("description": "Test.", "kitApi": "1.0", "main": "main.js", "translations": {"fr": {"name": "Démo", )"
        R"("description": "Un test.", "groups": {"Look": "Aspect"}, "settingDescriptions": {"size": "La taille."}}}})";
    std::vector<std::string> errors;
    std::optional<ampify::Manifest> manifest = ParseAuthored(text, errors);
    CHECK(errors.empty());
    REQUIRE(manifest.has_value());
    CHECK(manifest->translations.at("fr").groups.at("Look") == "Aspect");
    CHECK(manifest->translations.at("fr").settingDescriptions.at("size") == "La taille.");
}

TEST_CASE(Manifest_TranslationsKeepTheSameLengthRulesInEveryLanguage) {
    const std::string base =
        R"({"id": "demo", "name": "Demo", "version": "1.0.0", "kind": "extension", "author": "Shirowwww", )"
        R"("description": "Test.", "kitApi": "1.0", "main": "main.js", )";
    std::vector<std::string> errors;
    std::optional<ampify::Manifest> translated = ParseAuthored(
        base + R"("translations": {"fr": {"name": "Démo", "description": "Un test."}}})", errors);
    CHECK(errors.empty());
    CHECK(translated && translated->translations.at("fr").name == "Démo");
    CHECK(translated && translated->translations.at("fr").description == "Un test.");

    CHECK(!ParseAuthored(base + R"("translations": {"sv": {"name": "a", "description": "b"}}})", errors).has_value());
    CHECK(MentionsField(errors, "translations.sv: unknown language"));
    errors.clear();
    CHECK(!ParseAuthored(base + R"("translations": {"fr": {"name": "Démo"}}})", errors).has_value());
    CHECK(MentionsField(errors, "translations.fr.description: required field missing"));
    errors.clear();
    CHECK(!ParseAuthored(base + R"("translations": {"fr": {"name": "", "description": "b"}}})", errors).has_value());
    CHECK(!ParseAuthored(base + R"("translations": {"fr": {"name": "a", "description": "b", "x": 1}}})", errors).has_value());
    CHECK(MentionsField(errors, "translations.fr.x: unknown field"));
}

TEST_CASE(Catalog_TranslationsAreRead) {
    std::string entry = kCatalogEntry;
    entry.insert(entry.size() - 1, R"(, "translations": {"fr": {"name": "Démo", "description": "Un test."}})");
    std::vector<std::string> errors;
    std::optional<ampify::Catalog> catalog = ampify::ParseCatalog(CatalogWith(entry), false, errors);
    CHECK(errors.empty());
    CHECK(catalog && catalog->entries[0].translations.at("fr").name == "Démo");
}

TEST_CASE(Manifest_TagsAreFiveShortUniqueWords) {
    const std::string base =
        R"({"id": "demo", "name": "Demo", "version": "1.0.0", "kind": "extension", "author": "Shirowwww", )"
        R"("description": "Test.", "kitApi": "1.0", "main": "main.js", )";
    std::vector<std::string> errors;
    std::optional<ampify::Manifest> tagged =
        ParseAuthored(base + R"("tags": ["dark", "glass-2", "a"]})", errors);
    CHECK(errors.empty());
    CHECK(tagged && tagged->tags.size() == 3 && tagged->tags[1] == "glass-2");
    CHECK(ParseAuthored(base + R"("tags": ["a", "b", "c", "d", "e"]})", errors).has_value());

    CHECK(!ParseAuthored(base + R"("tags": ["Dark"]})", errors).has_value());
    CHECK(!ParseAuthored(base + R"("tags": ["two words"]})", errors).has_value());
    CHECK(!ParseAuthored(base + R"("tags": ["-dark"]})", errors).has_value());
    CHECK(!ParseAuthored(base + R"("tags": ["abcdefghijklmnopqrstu"]})", errors).has_value());
    CHECK(!ParseAuthored(base + R"("tags": ["a", "b", "c", "d", "e", "f"]})", errors).has_value());
    CHECK(MentionsField(errors, "tags: at most 5 items"));
    errors.clear();
    CHECK(!ParseAuthored(base + R"("tags": ["dark", "dark"]})", errors).has_value());
    CHECK(MentionsField(errors, "tags[1]: duplicate value"));
}

TEST_CASE(Manifest_PreviewIsAnImageNameAtTheRoot) {
    const std::string base =
        R"({"id": "demo", "name": "Demo", "version": "1.0.0", "kind": "extension", "author": "Shirowwww", )"
        R"("description": "Test.", "kitApi": "1.0", "main": "main.js", )";
    std::vector<std::string> errors;
    for (const char* name : {"preview.png", "shot.jpg", "Shot-2.jpeg"}) {
        std::optional<ampify::Manifest> manifest =
            ParseAuthored(base + R"("preview": ")" + name + R"("})", errors);
        CHECK(manifest && manifest->preview == std::optional<std::string>(name));
    }
    CHECK(errors.empty());
    CHECK(!ParseAuthored(base + R"("preview": "anim.gif"})", errors).has_value());
    CHECK(MentionsField(errors, "preview: invalid value"));
    CHECK(!ParseAuthored(base + R"("preview": "img/shot.png"})", errors).has_value());
    CHECK(!ParseAuthored(base + R"("preview": "../shot.png"})", errors).has_value());
}

TEST_CASE(Folder_PreviewMustExistAndStayUnderTwoMegabytes) {
    ampify::test::ScratchDir root("folder-preview");
    std::filesystem::path folder = root / "demo";
    std::string manifest = kValidExtension;
    manifest.replace(manifest.find("\"main\": \"main.js\","), 18, "\"main\": \"main.js\", \"preview\": \"shot.png\",");
    WriteFile(folder / "manifest.json", manifest);
    WriteFile(folder / "main.js", "Ampify.log('ok');");

    ampify::FolderValidation missing = ampify::ValidateElementFolder(folder);
    CHECK(!missing.ok());
    CHECK(MentionsField(missing.errors, "preview: file missing from the folder"));

    WriteFile(folder / "shot.png", std::string(ampify::kMaxPreviewBytes, 'x'));
    CHECK(ampify::ValidateElementFolder(folder).ok());

    WriteFile(folder / "shot.png", std::string(ampify::kMaxPreviewBytes + 1, 'x'));
    ampify::FolderValidation large = ampify::ValidateElementFolder(folder);
    CHECK(!large.ok());
    CHECK(MentionsField(large.errors, "preview: file larger than 2 MB"));
}

TEST_CASE(Folder_RefusesASymbolicLink) {
    ampify::test::ScratchDir root("folder-link");
    std::filesystem::path good = root / "demo";
    WriteFile(good / "manifest.json", kValidExtension);
    WriteFile(good / "main.js", "Ampify.log('ok');");
    WriteFile(good / "lib" / "util.js", "export const x = 1;");
    CHECK(ampify::ValidateElementFolder(good).ok());  // sub-folders are allowed: main.js may import from them

    std::error_code ec;
    std::filesystem::create_symlink(root / "elsewhere.js", good / "lib" / "link.js", ec);
    if (ec) SKIP("this account may not create symbolic links");
    ampify::FolderValidation linked = ampify::ValidateElementFolder(good);
    CHECK(!linked.ok());
    CHECK(MentionsField(linked.errors, "symbolic link not allowed in the element: lib/link.js"));
}

namespace {

ampify::cli::Identity Someone() {
    ampify::cli::Identity who;
    who.homes = {"C:\\Users\\zoe.martin"};
    who.user = "zoe.martin";
    who.computer = "ZOE-DESKTOP";
    return who;
}

bool Has(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

}  // namespace

TEST_CASE(Report_HidesTheProfileFolderUserAndComputer) {
    std::string out = ampify::cli::Anonymize(
        "kit C:\\Users\\zoe.martin\\Ampify, also c:/users/ZOE.MARTIN/Ampify\n"
        "D:\\Users\\other\\x and /home/zoe/.config\nmachine ZOE-DESKTOP signed in as zoe.martin",
        Someone());
    CHECK(Has(out, "kit <home>\\Ampify, also <home>/Ampify"));
    CHECK(Has(out, "D:\\Users\\<user>\\x and /home/<user>/.config"));
    CHECK(Has(out, "machine <computer> signed in as <user>"));
    CHECK(!Has(out, "zoe") && !Has(out, "ZOE"));
}

TEST_CASE(Report_HidesWhatWasPlayed) {
    std::string out = ampify::cli::Anonymize(
        "[08:07:11.533] Player: cover of \"Praise The Lord\": address "
        "\"amp-artwork-id://17820666364333712412.dbid/783EDFEC-D43D-47E2-B448-8ECCFFE09C81_1.1\", format \"\"\n"
        "[08:07:12.000] [scrobbler] sent \"Song\" by \"Band\"\n"
        "now playing track \"Secret Song\" from album: \"Hidden Album\" DBID:0x1A2B3C\n"
        "atmos 0cf52051b249e48024d5d5a08cbffa5e2380314f7b9108fd5b5ffc9749c4ff68 -20.9\n"
        "Atmos: stereo track, variant ecvyw6j2l",
        Someone());
    CHECK(!Has(out, "Praise") && !Has(out, "Song\"") && !Has(out, "Band") && !Has(out, "Hidden"));
    CHECK(!Has(out, "1782066636") && !Has(out, "783EDFEC") && !Has(out, "1A2B3C") && !Has(out, "0cf52051"));
    CHECK(Has(out, "Player: cover of \"<text>\""));
    CHECK(Has(out, "track \"<text>\" from album: \"<text>\" DBID:<id>"));
    CHECK(Has(out, "atmos <hash> -20.9"));
    CHECK(Has(out, "stereo track, variant <id>"));
}

TEST_CASE(Report_KeepsWhatHelpsToDebug) {
    std::string text =
        "[08:07:11.781] version 1.1792.39682.0 tested: kernel injected\n"
        "Theme \"glass\": 750 value(s) applied, 44 already in place, 0 refused\n"
        "Grid#LCD Background = #33FFFFFF (user.css line 82)\n"
        "C:\\Program Files\\WindowsApps\\AppleInc.AppleMusicWin_1.1792.39682.0_x64__nzyj5cx40ttqa";
    CHECK(ampify::cli::Anonymize(text, Someone()) == text);
}

TEST_CASE(Report_HidesSecretsAndAddresses) {
    std::string out = ampify::cli::Anonymize(
        "GET https://ws.example.com/?method=x&api_key=abc123&sk=SESSION9 from zoe@example.org, Bearer eyJhbGciOiJIUzI1",
        Someone());
    CHECK(Has(out, "api_key=<hidden>&sk=<hidden>"));
    CHECK(Has(out, "from <email>"));
    CHECK(Has(out, "Bearer <hidden>"));
    CHECK(!Has(out, "abc123") && !Has(out, "SESSION9") && !Has(out, "eyJ"));
}

TEST_CASE(Report_HidesSecretLookingSettings) {
    std::string out = ampify::cli::HideSecretSettings(
        "# comment with token = kept\ntheme.active = glass\nextensions.scrobbler.session_key = 0123abc\n"
        "extensions.keyboard-skip.enabled = 1\nextensions.discord-presence.user = zoe");
    CHECK(Has(out, "# comment with token = kept"));
    CHECK(Has(out, "theme.active = glass"));
    CHECK(Has(out, "extensions.scrobbler.session_key = <hidden>"));
    CHECK(Has(out, "extensions.keyboard-skip.enabled = 1"));
    CHECK(Has(out, "extensions.discord-presence.user = <hidden>"));
}

namespace {

// An element folder with a script: its manifest names main.js, and `files` are written beside it.
ampify::test::ScratchDir ScriptFolder(const std::string& name, const std::vector<std::string>& files) {
    ampify::test::ScratchDir dir(name);
    WriteFile(dir / "manifest.json", R"({"id":"demo","main":"main.js"})");
    for (const std::string& file : files) WriteFile(dir / file, "// " + file);
    return dir;
}

void SetAge(const std::filesystem::path& file, std::chrono::hours age) {
    std::filesystem::last_write_time(file, std::filesystem::file_time_type::clock::now() - age);
}

// A stand-in for esbuild: remembers the command it was given and answers with `code`.
struct FakeProgram {
    int code = 0;
    int calls = 0;
    std::vector<std::string> command;
    ampify::cli::RunProgram Runner() {
        return [this](const std::vector<std::string>& given) {
            calls++;
            command = given;
            return code;
        };
    }
};

// An environment variable set for the length of a test, then put back as it was.
struct ScopedEnvironment {
    std::string name;
    std::string before;
    bool had = false;
    ScopedEnvironment(const std::string& variable, const std::string& value) : name(variable) {
        if (const char* current = std::getenv(name.c_str())) {
            had = true;
            before = current;
        }
        Set(value);
    }
    ~ScopedEnvironment() { Set(had ? before : std::string()); }
    void Set(const std::string& value) const {
#ifdef _WIN32
        _putenv_s(name.c_str(), value.c_str());
#else
        if (value.empty()) unsetenv(name.c_str());
        else setenv(name.c_str(), value.c_str(), 1);
#endif
    }
};

bool HasArgument(const std::vector<std::string>& command, const std::string& wanted) {
    for (const std::string& argument : command) {
        if (argument == wanted) return true;
    }
    return false;
}

}  // namespace

TEST_CASE(Compile_NoSourceWithoutTypeScriptOrJsx) {
    FakeProgram program;
    std::string message;
    CHECK(ampify::cli::CompileSources(ScriptFolder("compile-js", {"main.js"}), message, program.Runner()) ==
          ampify::cli::CompileOutcome::NoSource);
    // A declaration file is types, not code: it never makes main.js a build output.
    CHECK(ampify::cli::CompileSources(ScriptFolder("compile-dts", {"main.js", "ampify.d.ts"}), message,
                                      program.Runner()) == ampify::cli::CompileOutcome::NoSource);
    CHECK(program.calls == 0);
}

TEST_CASE(Compile_NoSourceForAFolderWithoutScript) {
    FakeProgram program;
    ampify::test::ScratchDir dir("compile-theme");
    WriteFile(dir / "manifest.json", R"({"id":"demo","kind":"theme"})");
    WriteFile(dir / "main.ts", "export {};");
    std::string message;
    CHECK(ampify::cli::CompileSources(dir, message, program.Runner()) == ampify::cli::CompileOutcome::NoSource);
    CHECK(program.calls == 0);
}

TEST_CASE(Compile_UpToDateWhenMainIsNewerThanEverySource) {
    FakeProgram program;
    auto dir = ScriptFolder("compile-fresh", {"main.ts", "util.tsx", "main.js"});
    SetAge(dir / "main.ts", std::chrono::hours(3));
    SetAge(dir / "util.tsx", std::chrono::hours(2));
    SetAge(dir / "main.js", std::chrono::hours(1));
    std::string message;
    CHECK(ampify::cli::CompileSources(dir, message, program.Runner()) == ampify::cli::CompileOutcome::UpToDate);
    CHECK(program.calls == 0);
}

TEST_CASE(Compile_AChangedSourceRunsEsbuildOnMainWithTheJsxFactory) {
    ampify::test::ScratchDir scratch("compile-no-npm");
    ScopedEnvironment roaming("APPDATA", scratch.string());  // no global esbuild: the PATH one
    FakeProgram program;
    auto dir = ScriptFolder("compile-stale", {"main.tsx", "util.ts", "main.js"});
    SetAge(dir / "main.tsx", std::chrono::hours(3));
    SetAge(dir / "main.js", std::chrono::hours(2));
    SetAge(dir / "util.ts", std::chrono::hours(1));  // an imported file changed after the build
    std::string message;
    CHECK(ampify::cli::CompileSources(dir, message, program.Runner()) == ampify::cli::CompileOutcome::Compiled);
    CHECK(program.calls == 1);
    CHECK(program.command.size() > 2 && program.command[0] == "esbuild");
    CHECK(HasArgument(program.command, (dir / "main.tsx").string()));
    CHECK(HasArgument(program.command, "--bundle"));
    CHECK(HasArgument(program.command, "--jsx-factory=Ampify.React.createElement"));
    CHECK(HasArgument(program.command, "--jsx-fragment=Ampify.React.Fragment"));
    CHECK(HasArgument(program.command, "--outfile=" + (dir / "main.js").string()));
    CHECK(Has(message, "main.tsx compiled to main.js"));
}

TEST_CASE(Compile_BuildsMainWhenItIsMissing) {
    FakeProgram program;
    auto dir = ScriptFolder("compile-first", {"main.ts"});
    std::string message;
    CHECK(ampify::cli::CompileSources(dir, message, program.Runner()) == ampify::cli::CompileOutcome::Compiled);
    CHECK(program.calls == 1);
}

TEST_CASE(Compile_SaysHowToInstallEsbuildWhenItIsNotThere) {
    FakeProgram program;
    program.code = -1;  // the program cannot start
    auto dir = ScriptFolder("compile-missing", {"main.ts"});
    std::string message;
    CHECK(ampify::cli::CompileSources(dir, message, program.Runner()) == ampify::cli::CompileOutcome::Failed);
    CHECK(Has(message, "npm install --save-dev esbuild"));
}

TEST_CASE(Compile_ReportsAFailedBuild) {
    FakeProgram program;
    program.code = 1;
    auto dir = ScriptFolder("compile-broken", {"main.ts"});
    std::string message;
    CHECK(ampify::cli::CompileSources(dir, message, program.Runner()) == ampify::cli::CompileOutcome::Failed);
    CHECK(Has(message, "esbuild failed") && Has(message, "code 1"));
}

TEST_CASE(Compile_PrefersTheEsbuildInstalledInNodeModules) {
    FakeProgram program;
    auto dir = ScriptFolder("compile-local", {"main.ts"});
    WriteFile(dir / "node_modules" / "esbuild" / "bin" / "esbuild", "// stub");
    std::string message;
    CHECK(ampify::cli::CompileSources(dir, message, program.Runner()) == ampify::cli::CompileOutcome::Compiled);
    CHECK(program.command.size() > 2 && program.command[0] == "node");
    CHECK(Has(program.command[1], "esbuild"));
    CHECK(Has(program.command[1], "node_modules"));
}

#ifdef _WIN32
TEST_CASE(Compile_StartsNpmsGlobalEsbuildThroughNodeOnWindows) {
    // npm leaves only an esbuild.cmd on the PATH there, which CreateProcess cannot start.
    ampify::test::ScratchDir roaming("compile-roaming");
    WriteFile(roaming / "npm" / "node_modules" / "esbuild" / "bin" / "esbuild", "// stub");
    ScopedEnvironment appData("APPDATA", roaming.string());
    FakeProgram program;
    auto dir = ScriptFolder("compile-global", {"main.ts"});
    std::string message;
    CHECK(ampify::cli::CompileSources(dir, message, program.Runner()) == ampify::cli::CompileOutcome::Compiled);
    CHECK(program.command.size() > 2 && program.command[0] == "node");
    CHECK(program.command.size() > 2 && Has(program.command[1], "npm"));
}
#endif

TEST_CASE(Compile_APlainModuleOrJsonThatChangedAfterTheBuildRunsEsbuild) {
    FakeProgram program;
    auto dir = ScriptFolder("compile-plain", {"main.ts", "helper.js", "data.json", "main.js"});
    SetAge(dir / "main.ts", std::chrono::hours(4));
    SetAge(dir / "helper.js", std::chrono::hours(4));
    SetAge(dir / "main.js", std::chrono::hours(3));
    SetAge(dir / "data.json", std::chrono::hours(1));  // bundled into main.js, changed afterwards
    std::string message;
    CHECK(ampify::cli::CompileSources(dir, message, program.Runner()) == ampify::cli::CompileOutcome::Compiled);
    CHECK(program.calls == 1);
}

TEST_CASE(Compile_MainIsAFileNameAtTheRootOfTheFolder) {
    FakeProgram program;
    ampify::test::ScratchDir outer("compile-escape");
    std::filesystem::path dir = outer / "folder";
    WriteFile(outer / "escape.ts", "export {};");
    WriteFile(dir / "main.ts", "export {};");
    std::filesystem::create_directories(dir / "sub");
    std::string message;
    for (const char* name : {"../escape.js", "sub/main.js", "sub\\\\main.js", "/etc/main.js", "C:/Temp/main.js"}) {
        WriteFile(dir / "manifest.json", std::string(R"({"id":"demo","main":")") + name + R"("})");
        WriteFile(dir / "sub" / "main.ts", "export {};");
        CHECK(ampify::cli::CompileSources(dir, message, program.Runner()) == ampify::cli::CompileOutcome::NoSource);
    }
    CHECK(program.calls == 0);
}

TEST_CASE(Compile_IgnoresNodeModulesAndHiddenFolders) {
    FakeProgram program;
    auto dir = ScriptFolder("compile-deps", {"main.ts", "main.js"});
    WriteFile(dir / "node_modules" / "lib" / "index.ts", "export {};");
    WriteFile(dir / ".cache" / "old.ts", "export {};");
    SetAge(dir / "main.ts", std::chrono::hours(3));
    SetAge(dir / "main.js", std::chrono::hours(2));
    SetAge(dir / "node_modules" / "lib" / "index.ts", std::chrono::hours(1));
    SetAge(dir / ".cache" / "old.ts", std::chrono::hours(1));
    std::string message;
    CHECK(ampify::cli::CompileSources(dir, message, program.Runner()) == ampify::cli::CompileOutcome::UpToDate);
    CHECK(program.calls == 0);
}
