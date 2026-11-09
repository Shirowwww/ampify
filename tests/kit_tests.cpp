#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "../src/core/kit_layout.hpp"
#include "../src/core/natives/kit.hpp"
#include "support.hpp"
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

TEST_CASE(KitLayout_AnUpdateLeavesOldCopiesAndTheTextFilesOfItsPackage) {
    // What `ampify update --apply` leaves in the install folder, and the uninstaller then removes.
    CHECK(ampify::IsUpdateLeftover("ampify.exe.old"));
    CHECK(ampify::IsUpdateLeftover("ampify_launcher.exe.1.old"));
    CHECK(ampify::IsUpdateLeftover("ampify_kernel.dll.last.old"));
    CHECK(ampify::IsUpdateLeftover("AMPIFY_SETUP.EXE.OLD"));
    CHECK(ampify::IsUpdateLeftover("BUILD_INFO.txt"));
    CHECK(ampify::IsUpdateLeftover("README.txt"));
    CHECK(ampify::IsUpdateLeftover("SHA256SUMS.txt"));
    // Nothing else of the folder: not the programs, not a file of the user's.
    CHECK(!ampify::IsUpdateLeftover("ampify.exe"));
    CHECK(!ampify::IsUpdateLeftover("ampify_kernel.dll"));
    CHECK(!ampify::IsUpdateLeftover("notes.txt"));
    CHECK(!ampify::IsUpdateLeftover("old"));
    CHECK(!ampify::IsUpdateLeftover(".old"));
    CHECK(!ampify::IsUpdateLeftover("folder.old.txt"));
    CHECK(!ampify::IsUpdateLeftover(""));
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
    CHECK(ampify::IsBundledElement("extensions", "song-panel"));
    // A community element, or a bundled id in another folder, is not one of them.
    CHECK(!ampify::IsBundledElement("apps", "lyrics"));
    CHECK(!ampify::IsBundledElement("extensions", "marketplace"));
    CHECK(!ampify::IsBundledElement("themes", "now-playing"));
}

TEST_CASE(KitLayout_OnlyTheMarketplaceIsFirstParty) {
    CHECK(ampify::IsFirstParty("marketplace"));
    CHECK(!ampify::IsFirstParty("now-playing"));
    CHECK(!ampify::IsFirstParty("atmos-leveler"));
    CHECK(!ampify::IsFirstParty("community-thing"));
}

TEST_CASE(KitLayout_TheAtmosLevelerIsBuiltInStartsOffAndShipsNoFolder) {
    CHECK(ampify::IsBuiltInExtension("atmos-leveler"));
    CHECK(!ampify::IsBuiltInExtension("now-playing"));
    CHECK(!ampify::IsBuiltInExtension("marketplace"));
    CHECK(!ampify::IsBundledElement("extensions", "atmos-leveler"));
    CHECK(!ampify::IsOnByDefault("extensions", "atmos-leveler"));
    CHECK(ampify::BundledElements().size() == 4);
}

TEST_CASE(KitLayout_TheBundledElementsShipTurnedOn) {
    CHECK(ampify::IsOnByDefault("apps", "marketplace"));
    CHECK(ampify::IsOnByDefault("extensions", "now-playing"));
    CHECK(ampify::IsOnByDefault("extensions", "playback-guard"));  // off until 1.3.0
    CHECK(ampify::IsOnByDefault("extensions", "song-panel"));
    CHECK(!ampify::IsOnByDefault("extensions", "community-thing"));
}

TEST_CASE(KitLayout_InstallReplacesTheBundledElementsWithThePackagedOnes) {
    ampify::test::ScratchDir root("bundled-install");
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
    ampify::test::ScratchDir root("bundled-missing");
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
TEST_CASE(KitCommands_TheMarketplaceCanInstallRemoveAndChoose) {
    CHECK(ampify::IsKitCommandAllowed({"install", "spinning-artwork"}));
    CHECK(ampify::IsKitCommandAllowed({"remove", "spinning-artwork", "--yes"}));
    CHECK(ampify::IsKitCommandAllowed({"theme", "set", "glass", "--scheme", "Dark"}));
    CHECK(ampify::IsKitCommandAllowed({"list", "--json"}));
    CHECK(ampify::IsKitCommandAllowed({"market"}));
}

TEST_CASE(KitCommands_AnUpdateNeedsAnElementId) {
    // `update` alone would update Ampify itself: a page may only update one element.
    CHECK(!ampify::IsKitCommandAllowed({"update"}));
    CHECK(!ampify::IsKitCommandAllowed({"update", "--apply"}));
    CHECK(ampify::IsKitCommandAllowed({"update", "glass"}));
}

TEST_CASE(KitCommands_OnlyKnownCommandsPass) {
    CHECK(!ampify::IsKitCommandAllowed({}));
    CHECK(!ampify::IsKitCommandAllowed({"restore"}));
    CHECK(!ampify::IsKitCommandAllowed({"backup"}));
    CHECK(!ampify::IsKitCommandAllowed({"Install", "glass"}));
}

TEST_CASE(KitCommands_KitFolderOptionsStayOutOfAPage) {
    CHECK(!ampify::IsKitCommandAllowed({"list", "--kit-dir", "C:\Other"}));
    CHECK(!ampify::IsKitCommandAllowed({"install", "glass", "--catalog", "https://example.com/c.json"}));
    CHECK(!ampify::IsKitCommandAllowed({"list", "--install-dir=C:\Other"}));
}

TEST_CASE(KitCommands_SizeLimitsAreEnforced) {
    CHECK(ampify::IsKitCommandAllowed({"list", "a", "b", "c", "d", "e", "f", "g"}));       // 8 arguments: the most
    CHECK(!ampify::IsKitCommandAllowed({"list", "a", "b", "c", "d", "e", "f", "g", "h"}));  // 9: refused
    CHECK(!ampify::IsKitCommandAllowed({"list", ""}));                                   // empty argument
    CHECK(ampify::IsKitCommandAllowed({"list", std::string(200, 'x')}));
    CHECK(!ampify::IsKitCommandAllowed({"list", std::string(201, 'x')}));
}
