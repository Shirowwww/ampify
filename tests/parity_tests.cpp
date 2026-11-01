// Tests of the portable pieces behind the Spicetify parity rows of the command line: the list of extra tested
// Apple Music builds (versions.txt and versions.json of ampify-catalog), the version gate that reads it, and the splitting
// and quoting of app_launch_flags.

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#include "../src/cli/catalog.hpp"
#include "../src/core/build_signature.hpp"
#include "../src/core/injection_policy.hpp"
#include "../src/core/launch_flags.hpp"
#include "../src/core/version_gate.hpp"
#include "../src/core/version_list.hpp"
#include "test.hpp"

namespace {

namespace fs = std::filesystem;

fs::path Scratch(const std::string& name) {
    fs::path dir = fs::temp_directory_path() / "ampify-parity-tests" / name;
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

void Write(const fs::path& file, const std::string& text) {
    std::ofstream out(file, std::ios::binary);
    out << text;
}

ampify::BuildSignature GoodSignature() {
    ampify::BuildSignature signature;
    signature.readable = true;
    return signature;
}

const std::string kAllExports =
    R"(["AVCFPlayerCopyCurrentItem", "AVCFPlayerItemCopyCurrentStableVariantID", "AVCFPlayerItemCopyTracks",)"
    R"( "AVCFPlayerItemTrackCopyAssetTrack", "AVCFPlayerSetVolume"])";

}  // namespace

TEST_CASE(VersionList_PackageVersionText) {
    CHECK(ampify::IsPackageVersionText("1.1540.23042.0"));
    CHECK(!ampify::IsPackageVersionText("1.1540.23042"));
    CHECK(!ampify::IsPackageVersionText("1.1540.23042.0.1"));
    CHECK(!ampify::IsPackageVersionText("1..2.3"));
    CHECK(!ampify::IsPackageVersionText("1.2.3.x"));
    CHECK(!ampify::IsPackageVersionText("1.2.3.1234567890"));
    CHECK(!ampify::IsPackageVersionText(""));
}

TEST_CASE(VersionList_ParsesHeaderCommentsAndDuplicates) {
    auto list = ampify::ParseVersionList("\xEF\xBB\xBF" "ampify-versions 1\r\n# commentaire\n\n1.1600.1.0\n1.1600.1.0\n 1.1700.2.0 \n");
    CHECK(list.has_value());
    CHECK(list && list->size() == 2);
    CHECK(list && (*list)[0] == "1.1600.1.0" && (*list)[1] == "1.1700.2.0");

    auto empty = ampify::ParseVersionList("ampify-versions 1\n");
    CHECK(empty && empty->empty());
}

TEST_CASE(VersionList_MalformedTextIsRefusedAsAWhole) {
    CHECK(!ampify::ParseVersionList(""));                                   // no header
    CHECK(!ampify::ParseVersionList("1.1600.1.0\n"));                       // no header
    CHECK(!ampify::ParseVersionList("ampify-versions 2\n1.1600.1.0\n"));       // unknown format
    CHECK(!ampify::ParseVersionList("ampify-versions 1\n1.1600.1.0\nfaux\n")); // one bad line spoils the file
    CHECK(!ampify::ParseVersionList("ampify-versions 1\n1.1600.1.0 # note\n"));
}

TEST_CASE(VersionList_FormatReadsBack) {
    std::vector<std::string> versions{"1.1600.1.0", "1.1700.2.0"};
    std::string text = ampify::FormatVersionList(versions, "written by a test");
    auto back = ampify::ParseVersionList(text);
    CHECK(back && *back == versions);
}

TEST_CASE(VersionGate_BuiltInOnlyWhenThereIsNoFile) {
    fs::path dir = Scratch("builtin-only");
    std::vector<std::string> extra = ampify::LoadVersionList(dir / "versions.txt");
    CHECK(extra.empty());
    std::vector<std::string_view> known = ampify::KnownAppVersionsWith(extra);
    CHECK(known.size() == std::size(ampify::kKnownAppVersions));
    CHECK(ampify::IsKnownAppVersion("1.1540.23042.0", known));
    CHECK(!ampify::IsKnownAppVersion("1.1600.1.0", known));
}

TEST_CASE(VersionGate_FileAddsAVersion) {
    fs::path dir = Scratch("adds");
    Write(dir / "versions.txt", "ampify-versions 1\n1.1600.1.0\n1.1540.23042.0\n");
    std::vector<std::string> extra = ampify::LoadVersionList(dir / "versions.txt");
    std::vector<std::string_view> known = ampify::KnownAppVersionsWith(extra);
    CHECK(known.size() == std::size(ampify::kKnownAppVersions) + 1);  // the built-in one is not listed twice
    CHECK(ampify::IsKnownAppVersion("1.1600.1.0", known));
    CHECK(ampify::IsKnownAppVersion("1.1540.23042.0", known));
    CHECK(!ampify::IsKnownAppVersion("1.1600.1.1", known));

    ampify::PolicyVerdict launch = ampify::DecideInjection("1.1600.1.0", false, GoodSignature(), known);
    CHECK(launch.allowed);
    ampify::PolicyVerdict kernel = ampify::DecideKernel(ampify::ProcessRole::AppleMusic, "1.1600.1.0", GoodSignature(), known);
    CHECK(kernel.allowed);
    // Without the file, the same build stays unknown and the kernel idle.
    CHECK(!ampify::DecideKernel(ampify::ProcessRole::AppleMusic, "1.1600.1.0", GoodSignature()).allowed);
}

TEST_CASE(VersionGate_MalformedFileIsIgnored) {
    fs::path dir = Scratch("malformed");
    Write(dir / "versions.txt", "ampify-versions 1\n1.1600.1.0\nn'importe quoi\n");
    std::vector<std::string> extra = ampify::LoadVersionList(dir / "versions.txt");
    CHECK(extra.empty());
    std::vector<std::string_view> known = ampify::KnownAppVersionsWith(extra);
    CHECK(!ampify::IsKnownAppVersion("1.1600.1.0", known));
    CHECK(!ampify::DecideInjection("1.1600.1.0", false, GoodSignature(), known).allowed);

    Write(dir / "versions.txt", std::string(70 * 1024, '1'));  // too big to be a version list
    CHECK(ampify::LoadVersionList(dir / "versions.txt").empty());
}

TEST_CASE(VersionGate_SignatureIsStillRequired) {
    fs::path dir = Scratch("signature");
    Write(dir / "versions.txt", "ampify-versions 1\n1.1600.1.0\n");
    std::vector<std::string> extra = ampify::LoadVersionList(dir / "versions.txt");
    std::vector<std::string_view> known = ampify::KnownAppVersionsWith(extra);

    ampify::BuildSignature missing = GoodSignature();
    missing.missing.push_back("AVCFPlayerSetVolume");
    CHECK(!ampify::DecideInjection("1.1600.1.0", false, missing, known).allowed);
    CHECK(!ampify::DecideKernel(ampify::ProcessRole::LibraryAgent, "1.1600.1.0", missing, known).allowed);

    ampify::BuildSignature unreadable;
    CHECK(!ampify::DecideInjection("1.1600.1.0", false, unreadable, known).allowed);
    CHECK(!ampify::DecideInjection("1.1600.1.0", true, GoodSignature(), known).allowed);  // safe mode still wins
}

TEST_CASE(VersionsCatalog_ParsesAndKeepsOnlyFullySignedVersions) {
    std::string text = R"({"versionsFormat": 1, "playerDll": "AVFoundationCF.dll", "versions": [)"
                       R"({"version": "1.1600.1.0", "tested": "2026-10-30", "exports": )" + kAllExports + R"(},)"
                       R"({"version": "1.1700.2.0", "tested": "2026-10-30", "exports": ["AVCFPlayerSetVolume"]}]})";
    std::vector<std::string> errors;
    std::optional<ampify::VersionsCatalog> catalog = ampify::ParseVersionsCatalog(text, errors);
    CHECK(catalog.has_value());
    CHECK(errors.empty());

    std::vector<std::string> rejected;
    std::vector<std::string> usable = ampify::UsableVersions(*catalog, ampify::kRequiredPlayerExports, rejected);
    CHECK(usable.size() == 1 && usable[0] == "1.1600.1.0");
    CHECK(rejected.size() == 1 && rejected[0].find("1.1700.2.0") == 0);
}

TEST_CASE(VersionsCatalog_RefusesBadDocuments) {
    std::vector<std::string> errors;
    CHECK(!ampify::ParseVersionsCatalog("[]", errors));
    errors.clear();
    CHECK(!ampify::ParseVersionsCatalog(R"({"versionsFormat": 2, "playerDll": "AVFoundationCF.dll", "versions": []})", errors));
    errors.clear();
    CHECK(!ampify::ParseVersionsCatalog(R"({"versionsFormat": 1, "playerDll": "Other.dll", "versions": []})", errors));
    errors.clear();
    CHECK(!ampify::ParseVersionsCatalog(
        R"({"versionsFormat": 1, "playerDll": "AVFoundationCF.dll", "versions": [{"version": "1.2.3", "exports": ["A"]}]})",
        errors));
    errors.clear();
    CHECK(!ampify::ParseVersionsCatalog(R"({"versionsFormat": 1, "playerDll": "AVFoundationCF.dll", "versions": [)"
                                     R"({"version": "1.2.3.4", "exports": ["A"]}, {"version": "1.2.3.4", "exports": ["A"]}]})",
                                     errors));
    errors.clear();
    CHECK(!ampify::ParseVersionsCatalog(R"({"versionsFormat": 1, "playerDll": "AVFoundationCF.dll", "versions": [)"
                                     R"({"version": "1.2.3.4", "exports": ["A B"]}]})",
                                     errors));
}

TEST_CASE(VersionsCatalog_PublicFileIsUsable) {
    fs::path path = fs::path(AMPIFY_SOURCE_DIR) / ".." / "ampify-catalog" / "versions.json";
    if (!fs::exists(path)) {
        std::cout << "VersionsCatalog: no ampify-catalog clone next to the repository, test skipped\n";
        return;
    }
    std::ifstream in(path, std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::vector<std::string> errors;
    std::optional<ampify::VersionsCatalog> catalog = ampify::ParseVersionsCatalog(text, errors);
    CHECK(catalog.has_value());
    std::vector<std::string> rejected;
    CHECK(catalog && !ampify::UsableVersions(*catalog, ampify::kRequiredPlayerExports, rejected).empty());
    CHECK(rejected.empty());
}

TEST_CASE(Catalog_VersionsPinIsReadAndChecked) {
    std::string sha(64, 'a');
    std::vector<std::string> errors;
    auto catalog = ampify::ParseCatalog(R"({"catalogVersion": 1, "versions": {"url": "https://e.com/versions.json", "sha256": ")" +
                                         sha + R"("}, "extensions": []})",
                                     false, errors);
    CHECK(catalog && catalog->versionsUrl == "https://e.com/versions.json" && catalog->versionsSha256 == sha);

    errors.clear();
    CHECK(!ampify::ParseCatalog(R"({"catalogVersion": 1, "versions": {"url": "http://e.com/v.json", "sha256": ")" + sha +
                                 R"("}, "extensions": []})",
                             false, errors));
    errors.clear();
    CHECK(!ampify::ParseCatalog(R"({"catalogVersion": 1, "versions": {"url": "https://e.com/v.json"}, "extensions": []})",
                             false, errors));
}

TEST_CASE(LaunchFlags_SplitsOnSpacesAndKeepsQuotedGroups) {
    auto flags = ampify::SplitLaunchFlags("  --a=1\t\"--b=two words\" --c=\"x y\" ");
    CHECK(flags && flags->size() == 3);
    CHECK(flags && (*flags)[0] == "--a=1" && (*flags)[1] == "--b=two words" && (*flags)[2] == "--c=x y");
    CHECK(ampify::SplitLaunchFlags("") && ampify::SplitLaunchFlags("")->empty());
    CHECK(ampify::SplitLaunchFlags("\"\"") && ampify::SplitLaunchFlags("\"\"")->size() == 1);
}

TEST_CASE(LaunchFlags_RefusesWhatCannotBeAFlag) {
    std::string error;
    CHECK(!ampify::SplitLaunchFlags("--a \"open", &error));
    CHECK(error.find("unclosed quote") != std::string::npos);
    CHECK(!ampify::SplitLaunchFlags("--a\n--b"));
    CHECK(!ampify::SplitLaunchFlags(std::string(ampify::kMaxLaunchFlagsLength + 1, 'a')));
    std::string many;
    for (size_t i = 0; i <= ampify::kMaxLaunchFlags; i++) many += "-x ";
    CHECK(!ampify::SplitLaunchFlags(many));
}

TEST_CASE(LaunchFlags_JoinUsesTheWindowsQuotingRules) {
    CHECK(ampify::JoinWindowsArguments({"--a=1", "--b"}) == "--a=1 --b");
    CHECK(ampify::JoinWindowsArguments({"two words"}) == "\"two words\"");
    CHECK(ampify::JoinWindowsArguments({""}) == "\"\"");
    CHECK(ampify::JoinWindowsArguments({"a\"b"}) == "\"a\\\"b\"");
    CHECK(ampify::JoinWindowsArguments({"C:\\folder with space\\"}) == "\"C:\\folder with space\\\\\"");
    CHECK(ampify::JoinWindowsArguments({"C:\\no\\space\\"}) == "C:\\no\\space\\");

    CHECK(ampify::AppLaunchArguments("--remote-debugging-port=9222 \"--x=a b\"") == "--remote-debugging-port=9222 \"--x=a b\"");
    std::string error;
    CHECK(ampify::AppLaunchArguments("\"not closed", &error).empty());
    CHECK(!error.empty());
}
