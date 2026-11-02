#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "../src/core/kit_layout.hpp"
#include "test.hpp"

namespace {

namespace fs = std::filesystem;

std::string ReadAll(const fs::path& file) {
    std::ifstream in(file);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

}  // namespace

TEST_CASE(KitLayout_InstalledFilesAreTheFourTheInstallerCopies) {
    std::vector<std::string_view> files = ampify::InstalledFiles();
    CHECK(files.size() == 4);
    CHECK(files[0] == "ampify_launcher.exe");
    CHECK(files[1] == "ampify_kernel.dll");
    CHECK(files[2] == "ampify_setup.exe");
    CHECK(files[3] == "ampify.exe");  // beside the kernel: the Marketplace page runs it (Ampify.kit.run)
}

TEST_CASE(KitLayout_OnlyTheDataFolderCanBeRemovedWholesale) {
    CHECK(ampify::IsKitDataFolder("C:\\Users\\Sam\\Ampify"));
    CHECK(ampify::IsKitDataFolder("C:\\Users\\Sam\\Ampify\\"));
    CHECK(ampify::IsKitDataFolder("C:/Users/User/ampify"));

    // The parent folder, a subfolder, or a folder with a similar name must never pass.
    CHECK(!ampify::IsKitDataFolder("C:\\Users\\Sam"));
    CHECK(!ampify::IsKitDataFolder("C:\\Users\\Sam\\Ampify\\logs"));
    CHECK(!ampify::IsKitDataFolder("C:\\Users\\Sam\\AmpifyOld"));
    CHECK(!ampify::IsKitDataFolder("C:\\"));
    CHECK(!ampify::IsKitDataFolder(""));
}

TEST_CASE(KitLayout_TheMarketplaceAndNowPlayingShipWithTheKit) {
    CHECK(ampify::IsBundledElement("apps", "marketplace"));
    CHECK(ampify::IsBundledElement("extensions", "now-playing"));
    CHECK(ampify::IsBundledElement("extensions", "playback-guard"));
    // A community element, or a bundled id in another folder, is not one of them.
    CHECK(!ampify::IsBundledElement("apps", "lyrics"));
    CHECK(!ampify::IsBundledElement("extensions", "marketplace"));
    CHECK(!ampify::IsBundledElement("themes", "now-playing"));
}

TEST_CASE(KitLayout_ThePlaybackGuardShipsTurnedOff) {
    CHECK(ampify::IsOnByDefault("apps", "marketplace"));
    CHECK(ampify::IsOnByDefault("extensions", "now-playing"));
    CHECK(!ampify::IsOnByDefault("extensions", "playback-guard"));
    CHECK(!ampify::IsOnByDefault("extensions", "community-thing"));
}

TEST_CASE(KitLayout_InstallReplacesTheBundledElementsWithThePackagedOnes) {
    fs::path root = fs::temp_directory_path() / "ampify-tests" / "bundled-install";
    fs::remove_all(root);
    fs::path package = root / "package";
    fs::path kit = root / "kit";
    for (std::string_view element : ampify::BundledElements()) {
        fs::create_directories(package / "examples" / fs::path(element));
        std::ofstream(package / "examples" / fs::path(element) / "manifest.json") << "{}";
        std::ofstream(package / "examples" / fs::path(element) / "main.js") << "new";
    }
    // An older Marketplace: its main.js differs, and it has a file the new one dropped.
    fs::create_directories(kit / "apps" / "marketplace");
    std::ofstream(kit / "apps" / "marketplace" / "main.js") << "old";
    std::ofstream(kit / "apps" / "marketplace" / "dropped.js") << "old";

    CHECK(ampify::InstallBundledElements(package, kit).empty());
    CHECK(ReadAll(kit / "apps" / "marketplace" / "main.js") == "new");
    CHECK(!fs::exists(kit / "apps" / "marketplace" / "dropped.js"));
    CHECK(ReadAll(kit / "extensions" / "now-playing" / "main.js") == "new");  // a first install gets it too
}

TEST_CASE(KitLayout_ABundledElementMissingFromThePackageIsReportedAndKept) {
    fs::path root = fs::temp_directory_path() / "ampify-tests" / "bundled-missing";
    fs::remove_all(root);
    fs::create_directories(root / "package");  // a build folder: no examples
    fs::create_directories(root / "kit" / "apps" / "marketplace");
    std::ofstream(root / "kit" / "apps" / "marketplace" / "main.js") << "old";

    std::vector<std::string> failures = ampify::InstallBundledElements(root / "package", root / "kit");
    CHECK(failures.size() == ampify::BundledElements().size());
    CHECK(!failures.empty() && failures[0] == "apps/marketplace: not in the package");
    CHECK(ReadAll(root / "kit" / "apps" / "marketplace" / "main.js") == "old");
}

TEST_CASE(KitLayout_TheInstallFolderIsAddedToThePathOnce) {
    CHECK(ampify::PathWithFolder("", "C:\\A") == "C:\\A");
    CHECK(ampify::PathWithFolder("C:\\Windows;C:\\Tools", "C:\\A") == "C:\\Windows;C:\\Tools;C:\\A");
    // Already there, whatever the case or a trailing separator: the Path is left as it was.
    CHECK(ampify::PathWithFolder("C:\\Windows;c:\\a\\;C:\\Tools", "C:\\A") == "C:\\Windows;c:\\a\\;C:\\Tools");
    // Empty entries are not kept.
    CHECK(ampify::PathWithFolder("C:\\Windows;;", "C:\\A") == "C:\\Windows;C:\\A");
}

TEST_CASE(KitLayout_TheInstallFolderLeavesThePathAndNothingElse) {
    CHECK(ampify::PathWithoutFolder("C:\\Windows;C:\\A;C:\\Tools", "C:\\A") == "C:\\Windows;C:\\Tools");
    CHECK(ampify::PathWithoutFolder("C:\\a\\;C:\\Windows;C:\\A", "C:\\A") == "C:\\Windows");
    CHECK(ampify::PathWithoutFolder("C:\\Windows;C:\\AB", "C:\\A") == "C:\\Windows;C:\\AB");
    CHECK(ampify::PathWithoutFolder("C:\\A", "C:\\A").empty());
}