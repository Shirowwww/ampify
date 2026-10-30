// Tests of the portable pieces behind the Spicetify parity rows of the command line: the list of extra tested
// Apple Music builds (versions.txt and catalog/versions.json), the version gate that reads it, and the splitting
// and quoting of app_launch_flags.

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "../cli/catalog.hpp"
#include "../core/build_signature.hpp"
#include "../core/injection_policy.hpp"
#include "../core/launch_flags.hpp"
#include "../core/version_gate.hpp"
#include "../core/version_list.hpp"
#include "test.hpp"

namespace {

namespace fs = std::filesystem;

fs::path Scratch(const std::string& name) {
    fs::path dir = fs::temp_directory_path() / "amk-parity-tests" / name;
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

void Write(const fs::path& file, const std::string& text) {
    std::ofstream out(file, std::ios::binary);
    out << text;
}

amk::BuildSignature GoodSignature() {
    amk::BuildSignature signature;
    signature.readable = true;
    return signature;
}

const std::string kAllExports =
    R"(["AVCFPlayerCopyCurrentItem", "AVCFPlayerItemCopyCurrentStableVariantID", "AVCFPlayerItemCopyTracks",)"
    R"( "AVCFPlayerItemTrackCopyAssetTrack", "AVCFPlayerSetVolume"])";

}  // namespace

TEST_CASE(VersionList_PackageVersionText) {
    CHECK(amk::IsPackageVersionText("1.1540.23042.0"));
    CHECK(!amk::IsPackageVersionText("1.1540.23042"));
    CHECK(!amk::IsPackageVersionText("1.1540.23042.0.1"));
    CHECK(!amk::IsPackageVersionText("1..2.3"));
    CHECK(!amk::IsPackageVersionText("1.2.3.x"));
    CHECK(!amk::IsPackageVersionText("1.2.3.1234567890"));
    CHECK(!amk::IsPackageVersionText(""));
}

TEST_CASE(VersionList_ParsesHeaderCommentsAndDuplicates) {
    auto list = amk::ParseVersionList("\xEF\xBB\xBF" "amk-versions 1\r\n# commentaire\n\n1.1600.1.0\n1.1600.1.0\n 1.1700.2.0 \n");
    CHECK(list.has_value());
    CHECK(list && list->size() == 2);
    CHECK(list && (*list)[0] == "1.1600.1.0" && (*list)[1] == "1.1700.2.0");

    auto empty = amk::ParseVersionList("amk-versions 1\n");
    CHECK(empty && empty->empty());
}

TEST_CASE(VersionList_MalformedTextIsRefusedAsAWhole) {
    CHECK(!amk::ParseVersionList(""));                                   // no header
    CHECK(!amk::ParseVersionList("1.1600.1.0\n"));                       // no header
    CHECK(!amk::ParseVersionList("amk-versions 2\n1.1600.1.0\n"));       // unknown format
    CHECK(!amk::ParseVersionList("amk-versions 1\n1.1600.1.0\nfaux\n")); // one bad line spoils the file
    CHECK(!amk::ParseVersionList("amk-versions 1\n1.1600.1.0 # note\n"));
}

TEST_CASE(VersionList_FormatReadsBack) {
    std::vector<std::string> versions{"1.1600.1.0", "1.1700.2.0"};
    std::string text = amk::FormatVersionList(versions, "écrit par un test");
    auto back = amk::ParseVersionList(text);
    CHECK(back && *back == versions);
}

TEST_CASE(VersionGate_BuiltInOnlyWhenThereIsNoFile) {
    fs::path dir = Scratch("builtin-only");
    std::vector<std::string> extra = amk::LoadVersionList(dir / "versions.txt");
    CHECK(extra.empty());
    std::vector<std::string_view> known = amk::KnownAppVersionsWith(extra);
    CHECK(known.size() == std::size(amk::kKnownAppVersions));
    CHECK(amk::IsKnownAppVersion("1.1540.23042.0", known));
    CHECK(!amk::IsKnownAppVersion("1.1600.1.0", known));
}

TEST_CASE(VersionGate_FileAddsAVersion) {
    fs::path dir = Scratch("adds");
    Write(dir / "versions.txt", "amk-versions 1\n1.1600.1.0\n1.1540.23042.0\n");
    std::vector<std::string> extra = amk::LoadVersionList(dir / "versions.txt");
    std::vector<std::string_view> known = amk::KnownAppVersionsWith(extra);
    CHECK(known.size() == std::size(amk::kKnownAppVersions) + 1);  // the built-in one is not listed twice
    CHECK(amk::IsKnownAppVersion("1.1600.1.0", known));
    CHECK(amk::IsKnownAppVersion("1.1540.23042.0", known));
    CHECK(!amk::IsKnownAppVersion("1.1600.1.1", known));

    amk::PolicyVerdict launch = amk::DecideInjection("1.1600.1.0", false, GoodSignature(), known);
    CHECK(launch.allowed);
    amk::PolicyVerdict kernel = amk::DecideKernel(amk::ProcessRole::AppleMusic, "1.1600.1.0", GoodSignature(), known);
    CHECK(kernel.allowed);
    // Without the file, the same build stays unknown and the kernel idle.
    CHECK(!amk::DecideKernel(amk::ProcessRole::AppleMusic, "1.1600.1.0", GoodSignature()).allowed);
}

TEST_CASE(VersionGate_MalformedFileIsIgnored) {
    fs::path dir = Scratch("malformed");
    Write(dir / "versions.txt", "amk-versions 1\n1.1600.1.0\nn'importe quoi\n");
    std::vector<std::string> extra = amk::LoadVersionList(dir / "versions.txt");
    CHECK(extra.empty());
    std::vector<std::string_view> known = amk::KnownAppVersionsWith(extra);
    CHECK(!amk::IsKnownAppVersion("1.1600.1.0", known));
    CHECK(!amk::DecideInjection("1.1600.1.0", false, GoodSignature(), known).allowed);

    Write(dir / "versions.txt", std::string(70 * 1024, '1'));  // too big to be a version list
    CHECK(amk::LoadVersionList(dir / "versions.txt").empty());
}

TEST_CASE(VersionGate_SignatureIsStillRequired) {
    fs::path dir = Scratch("signature");
    Write(dir / "versions.txt", "amk-versions 1\n1.1600.1.0\n");
    std::vector<std::string> extra = amk::LoadVersionList(dir / "versions.txt");
    std::vector<std::string_view> known = amk::KnownAppVersionsWith(extra);

    amk::BuildSignature missing = GoodSignature();
    missing.missing.push_back("AVCFPlayerSetVolume");
    CHECK(!amk::DecideInjection("1.1600.1.0", false, missing, known).allowed);
    CHECK(!amk::DecideKernel(amk::ProcessRole::LibraryAgent, "1.1600.1.0", missing, known).allowed);

    amk::BuildSignature unreadable;
    CHECK(!amk::DecideInjection("1.1600.1.0", false, unreadable, known).allowed);
    CHECK(!amk::DecideInjection("1.1600.1.0", true, GoodSignature(), known).allowed);  // safe mode still wins
}

TEST_CASE(VersionsCatalog_ParsesAndKeepsOnlyFullySignedVersions) {
    std::string text = R"({"versionsFormat": 1, "playerDll": "AVFoundationCF.dll", "versions": [)"
                       R"({"version": "1.1600.1.0", "tested": "2026-10-30", "exports": )" + kAllExports + R"(},)"
                       R"({"version": "1.1700.2.0", "tested": "2026-10-30", "exports": ["AVCFPlayerSetVolume"]}]})";
    std::vector<std::string> errors;
    std::optional<amk::VersionsCatalog> catalog = amk::ParseVersionsCatalog(text, errors);
    CHECK(catalog.has_value());
    CHECK(errors.empty());

    std::vector<std::string> rejected;
    std::vector<std::string> usable = amk::UsableVersions(*catalog, amk::kRequiredPlayerExports, rejected);
    CHECK(usable.size() == 1 && usable[0] == "1.1600.1.0");
    CHECK(rejected.size() == 1 && rejected[0].find("1.1700.2.0") == 0);
}

TEST_CASE(VersionsCatalog_RefusesBadDocuments) {
    std::vector<std::string> errors;
    CHECK(!amk::ParseVersionsCatalog("[]", errors));
    errors.clear();
    CHECK(!amk::ParseVersionsCatalog(R"({"versionsFormat": 2, "playerDll": "AVFoundationCF.dll", "versions": []})", errors));
    errors.clear();
    CHECK(!amk::ParseVersionsCatalog(R"({"versionsFormat": 1, "playerDll": "Other.dll", "versions": []})", errors));
    errors.clear();
    CHECK(!amk::ParseVersionsCatalog(
        R"({"versionsFormat": 1, "playerDll": "AVFoundationCF.dll", "versions": [{"version": "1.2.3", "exports": ["A"]}]})",
        errors));
    errors.clear();
    CHECK(!amk::ParseVersionsCatalog(R"({"versionsFormat": 1, "playerDll": "AVFoundationCF.dll", "versions": [)"
                                     R"({"version": "1.2.3.4", "exports": ["A"]}, {"version": "1.2.3.4", "exports": ["A"]}]})",
                                     errors));
    errors.clear();
    CHECK(!amk::ParseVersionsCatalog(R"({"versionsFormat": 1, "playerDll": "AVFoundationCF.dll", "versions": [)"
                                     R"({"version": "1.2.3.4", "exports": ["A B"]}]})",
                                     errors));
}

TEST_CASE(VersionsCatalog_RepositoryFileIsUsable) {
    std::ifstream in(fs::path(AMK_SOURCE_DIR) / "catalog" / "versions.json", std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::vector<std::string> errors;
    std::optional<amk::VersionsCatalog> catalog = amk::ParseVersionsCatalog(text, errors);
    CHECK(catalog.has_value());
    std::vector<std::string> rejected;
    CHECK(catalog && !amk::UsableVersions(*catalog, amk::kRequiredPlayerExports, rejected).empty());
    CHECK(rejected.empty());
}

TEST_CASE(Catalog_VersionsPinIsReadAndChecked) {
    std::string sha(64, 'a');
    std::vector<std::string> errors;
    auto catalog = amk::ParseCatalog(R"({"catalogVersion": 1, "versions": {"url": "https://e.com/versions.json", "sha256": ")" +
                                         sha + R"("}, "extensions": []})",
                                     false, errors);
    CHECK(catalog && catalog->versionsUrl == "https://e.com/versions.json" && catalog->versionsSha256 == sha);

    errors.clear();
    CHECK(!amk::ParseCatalog(R"({"catalogVersion": 1, "versions": {"url": "http://e.com/v.json", "sha256": ")" + sha +
                                 R"("}, "extensions": []})",
                             false, errors));
    errors.clear();
    CHECK(!amk::ParseCatalog(R"({"catalogVersion": 1, "versions": {"url": "https://e.com/v.json"}, "extensions": []})",
                             false, errors));
}

TEST_CASE(LaunchFlags_SplitsOnSpacesAndKeepsQuotedGroups) {
    auto flags = amk::SplitLaunchFlags("  --a=1\t\"--b=deux mots\" --c=\"x y\" ");
    CHECK(flags && flags->size() == 3);
    CHECK(flags && (*flags)[0] == "--a=1" && (*flags)[1] == "--b=deux mots" && (*flags)[2] == "--c=x y");
    CHECK(amk::SplitLaunchFlags("") && amk::SplitLaunchFlags("")->empty());
    CHECK(amk::SplitLaunchFlags("\"\"") && amk::SplitLaunchFlags("\"\"")->size() == 1);
}

TEST_CASE(LaunchFlags_RefusesWhatCannotBeAFlag) {
    std::string error;
    CHECK(!amk::SplitLaunchFlags("--a \"ouvert", &error));
    CHECK(error.find("guillemet") != std::string::npos);
    CHECK(!amk::SplitLaunchFlags("--a\n--b"));
    CHECK(!amk::SplitLaunchFlags(std::string(amk::kMaxLaunchFlagsLength + 1, 'a')));
    std::string many;
    for (size_t i = 0; i <= amk::kMaxLaunchFlags; i++) many += "-x ";
    CHECK(!amk::SplitLaunchFlags(many));
}

TEST_CASE(LaunchFlags_JoinUsesTheWindowsQuotingRules) {
    CHECK(amk::JoinWindowsArguments({"--a=1", "--b"}) == "--a=1 --b");
    CHECK(amk::JoinWindowsArguments({"deux mots"}) == "\"deux mots\"");
    CHECK(amk::JoinWindowsArguments({""}) == "\"\"");
    CHECK(amk::JoinWindowsArguments({"a\"b"}) == "\"a\\\"b\"");
    CHECK(amk::JoinWindowsArguments({"C:\\dossier avec espace\\"}) == "\"C:\\dossier avec espace\\\\\"");
    CHECK(amk::JoinWindowsArguments({"C:\\sans\\espace\\"}) == "C:\\sans\\espace\\");

    CHECK(amk::AppLaunchArguments("--remote-debugging-port=9222 \"--x=a b\"") == "--remote-debugging-port=9222 \"--x=a b\"");
    std::string error;
    CHECK(amk::AppLaunchArguments("\"pas fermé", &error).empty());
    CHECK(!error.empty());
}
