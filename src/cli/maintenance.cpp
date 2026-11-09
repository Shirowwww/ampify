// Upkeep commands: the interface inspector (devtools), the list of tested Apple Music builds (versions), the kit
// folder in the file manager (config --dir), and the one-line notice about a new kit release (check_kit_update).

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iterator>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <thread>

#include "../core/apple_music.hpp"
#include "../core/build_signature.hpp"
#include "../core/i18n.hpp"
#include "../core/settings_catalog.hpp"
#include "../core/ui_render/apple_tweaks.hpp"
#include "../core/version_gate.hpp"
#include "../core/version_list.hpp"
#include "internal.hpp"
#include "platform.hpp"
#include "release_facts.hpp"
#include "sha256.hpp"

namespace ampify::cli {

namespace {

constexpr std::int64_t kUpdateCheckPeriodSeconds = 24 * 60 * 60;
constexpr int kUpdateCheckTimeoutSeconds = 10;
constexpr int kInspectorWaitSeconds = 10;

constexpr int kPickDelaySeconds = 4;  // time to put the pointer on the element before its place is read

std::filesystem::path InspectorFile(const KitLayout& kit) { return kit.Journal() / "inspector.txt"; }
std::filesystem::path PickedFile(const KitLayout& kit) { return kit.Journal() / "picked.txt"; }
std::filesystem::path PickRequestFile(const KitLayout& kit) { return kit.Root() / "pick.txt"; }

std::int64_t NowSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

// cache\kit-update.txt: "checked=<seconds since 1970>" and "latest=<version>", one per line.
struct UpdateCache {
    std::int64_t checked = -1;
    std::string latest;
};

UpdateCache ReadUpdateCache(const std::filesystem::path& file) {
    UpdateCache cache;
    std::string text;
    if (!ReadWholeFile(file, text)) return cache;
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("checked=", 0) == 0) {
            try {
                cache.checked = std::stoll(line.substr(8));
            } catch (...) {
                cache.checked = -1;
            }
        } else if (line.rfind("latest=", 0) == 0) {
            cache.latest = line.substr(7);
        }
    }
    return cache;
}

std::optional<std::filesystem::file_time_type> WriteTime(const std::filesystem::path& file) {
    std::error_code ec;
    auto time = std::filesystem::last_write_time(file, ec);
    if (ec) return std::nullopt;
    return time;
}

// The running kernel reads settings.txt at each pass (about 3 s) and only in the default Ampify folder, so the tree
// comes back live when Apple Music is open there. Elsewhere there is nothing to wait for.
// `before` is the file's time read before the request that makes Apple Music write it: read later, an answer that came in
// between would look like no answer at all (starting tasklist alone takes longer than a pass of the kernel's).
bool WaitForInspectorTree(Context& ctx, const std::filesystem::path& file,
                          const std::optional<std::filesystem::file_time_type>& before,
                          const char* what = "the interface tree") {
    if (ctx.kit.Root() != platform::DefaultKitDirectory() || !platform::IsProcessRunning(std::string(kAppleMusicImage))) return false;
    ctx.out << "Waiting for Apple Music to write " << what << "...\n" << std::flush;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(kInspectorWaitSeconds);
    while (std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        std::optional<std::filesystem::file_time_type> now = WriteTime(file);
        if (now && (!before || *now > *before)) return true;
    }
    return false;
}

// `devtools pick`: after a countdown, the pointer's place goes to the kernel (pick.txt), which names the element under it
// and the ones above it (logs\picked.txt). The place is read here, so the pointer does not have to stay still while the
// kernel's pass comes round.
int PickUnderPointer(Context& ctx) {
    if (ctx.options.dryRun) {
        ctx.out << "The pointer's place would be read in " << kPickDelaySeconds << " seconds. --dry-run: nothing was written.\n";
        return 0;
    }
    if (ctx.kit.Root() != platform::DefaultKitDirectory() || !platform::IsProcessRunning(std::string(kAppleMusicImage))) {
        return Failure(ctx, "Apple Music must be open, with Ampify, to pick an element");
    }
    ctx.out << "Put the pointer on the element in Apple Music: its place is read in " << kPickDelaySeconds
            << " seconds.\n" << std::flush;
    std::this_thread::sleep_for(std::chrono::seconds(kPickDelaySeconds));
    int x = 0;
    int y = 0;
    if (!platform::CursorPosition(x, y)) return Failure(ctx, "the pointer's place can only be read on Windows");

    std::filesystem::path request = PickRequestFile(ctx.kit);
    std::filesystem::path picked = PickedFile(ctx.kit);
    std::optional<std::filesystem::file_time_type> before = WriteTime(picked);
    {
        std::ofstream out(request, std::ios::binary | std::ios::trunc);
        out << x << " " << y << " " << NowSeconds() << "\n";
        if (!out) return Failure(ctx, "cannot write " + Utf8(request));
    }
    if (!WaitForInspectorTree(ctx, picked, before, "the element under the pointer")) {
        ctx.out << "Apple Music did not answer in time: run the command again with Ampify loaded.\n";
        return 1;
    }
    std::string text;
    if (!ReadWholeFile(picked, text)) return Failure(ctx, "cannot read " + Utf8(picked));
    ctx.out << text;
    if (!text.empty() && text.back() != '\n') ctx.out << "\n";
    return 0;
}

}  // namespace

int CmdDevtools(Context& ctx) {
    const std::vector<std::string>& args = ctx.options.positional;
    if (args.size() > 2) return Usage(ctx, "devtools takes on, off, pick, or nothing (for on)");
    std::string action = args.size() == 2 ? args[1] : "on";
    if (action == "pick") return PickUnderPointer(ctx);
    if (action != "on" && action != "off") return Usage(ctx, "devtools takes on, off or pick, not " + action);
    if (ctx.options.dryRun) {
        ctx.out << "Inspector would be turned " << action << " (theme.inspect = " << (action == "on" ? 1 : 0)
                << "). --dry-run: nothing was written.\n";
        return 0;
    }

    std::filesystem::path file = InspectorFile(ctx.kit);
    std::optional<std::filesystem::file_time_type> before = WriteTime(file);
    Settings settings;
    std::string error;
    if (!LoadSettings(ctx, settings, error)) return Failure(ctx, error);
    settings.SetBool(kInspectKey, action == "on");
    if (!SaveSettings(ctx, settings, error)) return Failure(ctx, error);

    if (action == "off") {
        ctx.out << "Inspector turned off (theme.inspect = 0).\n";
        return 0;
    }
    ctx.out << "Inspector turned on (theme.inspect = 1).\n";
    if (WaitForInspectorTree(ctx, file, before)) {
        ctx.out << "Interface tree written to " << Utf8(file) << ". Show it with: ampify inspect --tree\n";
        return 0;
    }
    ctx.out << "The kernel in Apple Music writes the interface tree within a few seconds, or when Apple Music next\n"
            << "starts, to " << Utf8(file) << ", then sets theme.inspect back to 0.\n"
            << "Show it with: ampify inspect --tree\n";
    return 0;
}

int CmdInspectTree(Context& ctx) {
    if (ctx.options.positional.size() != 1) return Usage(ctx, "inspect --tree takes no argument");
    std::filesystem::path file = InspectorFile(ctx.kit);
    std::string text;
    if (!ReadWholeFile(file, text)) {
        return Failure(ctx, "no interface tree yet (" + Utf8(file) +
                                " is missing). Run ampify devtools, then restart Apple Music.");
    }
    ctx.out << "Interface tree written by the kernel (" << Utf8(file) << "):\n" << text;
    if (!text.empty() && text.back() != '\n') ctx.out << "\n";
    return 0;
}

int CmdConfigDir(Context& ctx) {
    if (ctx.options.positional.size() != 1) return Usage(ctx, "config --dir takes no argument");
    std::error_code ec;
    std::filesystem::create_directories(ctx.kit.Root(), ec);
    ctx.out << Utf8(ctx.kit.Root()) << "\n";
    std::vector<std::string> command = platform::OpenFolderCommand(ctx.kit.Root());
    if (ctx.options.dryRun) {
        ctx.out << "Command:";
        for (const std::string& part : command) ctx.out << " " << part;
        ctx.out << "\n";
        return 0;
    }
    if (!platform::StartDetached(command)) return Failure(ctx, "cannot open the folder");
    return 0;
}

int CmdVersions(Context& ctx) {
    const std::vector<std::string>& args = ctx.options.positional;
    std::string action = args.size() > 1 ? args[1] : "list";
    if (args.size() > 2) return Usage(ctx, "versions takes list or update");
    std::filesystem::path listFile = ctx.kit.Root() / std::string(kVersionListFile);

    if (action == "list") {
        std::vector<std::string> extra = LoadVersionList(listFile);
        ctx.out << "Apple Music versions tested (every build of their family is accepted):\n";
        for (std::string_view version : kKnownAppVersions) {
            ctx.out << "  " << Pad(version, 20) << "built into the kernel\n";
        }
        for (const std::string& version : extra) {
            if (std::find(std::begin(kKnownAppVersions), std::end(kKnownAppVersions), version) !=
                std::end(kKnownAppVersions)) continue;
            ctx.out << "  " << Pad(version, 20) << "versions.txt (ampify versions update)\n";
        }
        std::error_code ec;
        if (std::filesystem::exists(listFile, ec) && extra.empty()) {
            ctx.out << "Warning: " << Utf8(listFile) << " is unreadable or empty, it is ignored.\n";
        }

        ctx.out << "At each start, " << kPlayerDllName << " must also export:";
        for (std::string_view name : kRequiredPlayerExports) ctx.out << " " << name;
        ctx.out << "\n";

        std::optional<std::filesystem::path> folder = platform::AppPackageFolder();
        if (!folder) {
            ctx.out << "Installed Apple Music: not found on this system.\n";
            return 0;
        }
        std::string installed = AppleMusicVersion(Utf8(folder->filename()));
        if (installed.empty()) {
            ctx.out << "Installed Apple Music: unreadable version (" << Utf8(*folder) << ").\n";
            return 0;
        }
        bool known = IsKnownAppVersion(installed, KnownAppVersionsWith(extra));
        ctx.out << "Installed Apple Music: " << installed
                << (known ? " (family " + std::string(AppVersionFamily(installed)) + " tested)\n"
                          : std::string(" (family not tested: the kernel stays off. Try ampify versions update)\n"));
        return 0;
    }

    if (action != "update") return Usage(ctx, "unknown action for versions: " + action);

    Catalog catalog;
    bool local = false;
    if (!LoadCatalog(ctx, catalog, local)) return 1;
    if (catalog.versionsUrl.empty()) return Failure(ctx, "the catalog does not publish a list of versions");
    if (!IsAllowedDownloadUrl(catalog.versionsUrl, local)) return Failure(ctx, "address refused: " + catalog.versionsUrl);

    std::error_code ec;
    std::filesystem::create_directories(ctx.kit.Downloads(), ec);
    std::filesystem::path part = ctx.kit.Downloads() / "versions.json.part";
    std::string error;
    ctx.out << "Downloading the list of tested versions...\n" << std::flush;
    std::string text;
    bool fetched = FetchTo(catalog.versionsUrl, part, error) && ReadWholeFile(part, text);
    std::optional<std::string> digest = fetched ? Sha256File(part) : std::nullopt;
    std::filesystem::remove(part, ec);
    if (!fetched) return Failure(ctx, error.empty() ? "unreadable list of versions" : error);
    if (!digest || *digest != catalog.versionsSha256) {
        return Failure(ctx, "SHA-256 differs from the catalog's: list refused");
    }

    std::vector<std::string> problems;
    std::optional<VersionsCatalog> versions = ParseVersionsCatalog(text, problems);
    if (!versions) {
        ctx.err << "ampify: invalid list of versions\n";
        for (const std::string& problem : problems) ctx.err << "  - " << problem << "\n";
        return 1;
    }
    std::vector<std::string> rejected;
    std::vector<std::string> usable = UsableVersions(*versions, kRequiredPlayerExports, rejected);
    for (const std::string& version : rejected) ctx.out << "  ignored    " << version << "\n";

    if (!WriteTextFile(listFile,
                       FormatVersionList(usable, "Written by ampify versions update. Run that command again to update it."),
                       error)) {
        return Failure(ctx, error);
    }
    for (const std::string& version : usable) ctx.out << "  tested     " << version << "\n";
    ctx.out << usable.size() << " version(s) written to " << Utf8(listFile)
            << ". The kernel accepts them the next time Apple Music starts, if "
            << kPlayerDllName << " still has the expected exports.\n";
    return 0;
}

void NotifyKitUpdate(Context& ctx) {
    Settings settings;
    if (!settings.Load(ctx.kit.Settings().string()) || !ToggleOf(settings, kCheckKitUpdateKey)) return;

    std::filesystem::path file = ctx.kit.Cache() / "kit-update.txt";
    UpdateCache cache = ReadUpdateCache(file);
    std::int64_t now = NowSeconds();
    if (cache.checked < 0 || now < cache.checked || now - cache.checked >= kUpdateCheckPeriodSeconds) {
        std::string source = ctx.options.releaseUrl ? *ctx.options.releaseUrl : DefaultReleaseUrl("");
        std::string error;
        std::optional<ReleaseInfo> release = FetchRelease(ctx, source, error, kUpdateCheckTimeoutSeconds);
        // A failed check is written too, so an offline PC does not wait for the network on every command.
        cache.checked = now;
        cache.latest = release ? release->version : "";
        std::error_code ec;
        std::filesystem::create_directories(ctx.kit.Cache(), ec);
        std::string writeError;
        WriteTextFile(file, "checked=" + std::to_string(now) + "\nlatest=" + cache.latest + "\n", writeError);
    }
    if (!IsReleaseVersion(cache.latest) || CompareDottedVersions(cache.latest, kKitVersion) <= 0) return;
    ctx.err << "ampify: version " << cache.latest << " of Ampify is available (installed: " << kKitVersion
            << "). Run ampify update to download it.\n";
}

namespace {

// The texts of the notices follow the `language` setting, or else the Windows display language.
void UseUserLanguage(const Settings& settings) {
    std::string language = settings.GetString("language", "");
    i18n::SetLanguage(i18n::LanguageCode(i18n::PreferredLocale(language, platform::UserLocale())));
}

// Whether ampify update --auto may look for a release now: kit_update allows it, this is the installed copy (a build
// folder or an extracted zip is never replaced behind the user's back), and no look was made today.
bool MayLookForUpdate(Context& ctx, uirender::KitUpdate mode, const std::filesystem::path& cacheFile) {
    if (mode == uirender::KitUpdate::Off) {
        ctx.out << "kit_update = off: no update looked for.\n";
        return false;
    }
    if (!ctx.options.installDir && !platform::IsInstallDirectory(platform::ExecutableDirectory())) {
        ctx.out << "Not the installed copy of Ampify: no update looked for.\n";
        return false;
    }
    UpdateCache cache = ReadUpdateCache(cacheFile);
    std::int64_t now = NowSeconds();
    return cache.checked < 0 || now < cache.checked || now - cache.checked >= kUpdateCheckPeriodSeconds;
}

// Apple's library agent often stays open, with the kernel, long after Apple Music closed; Apple Music starts it again
// when it opens. The installer ends it the same way (src/setup/setup.cpp). False while Apple Music itself is open.
bool ReadyToInstall(Context& ctx) {
    if (platform::IsProcessRunning(std::string(kAppleMusicImage))) return false;
    const std::string agent(kLibraryAgentImage);
    if (platform::IsProcessRunning(agent)) {
        ctx.out << agent << " stayed open after Apple Music: ended before the update.\n";
        platform::EndProcesses(agent);
    }
    return true;
}

// cache\update-offer.txt, with kit_update = ask: the release last offered, when ("at=", seconds since 1970), and
// whether the person clicked its notification ("accepted=1").
struct Offer {
    std::string version;
    std::int64_t at = -1;
    bool accepted = false;
};

constexpr std::int64_t kOfferQuietSeconds = 7 * 24 * 60 * 60;  // a release left alone is offered again a week later
constexpr unsigned long kOfferWaitMs = 2 * 60 * 1000;           // a click from the notification centre still counts

Offer ReadOffer(const std::filesystem::path& file) {
    Offer offer;
    std::string text;
    if (!ReadWholeFile(file, text)) return offer;
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("version=", 0) == 0) offer.version = line.substr(8);
        if (line.rfind("accepted=", 0) == 0) offer.accepted = line.substr(9) == "1";
        if (line.rfind("at=", 0) == 0) {
            try {
                offer.at = std::stoll(line.substr(3));
            } catch (const std::logic_error&) {  // std::stoll: invalid_argument, out_of_range
                offer.at = -1;
            }
        }
    }
    return offer;
}

// kit_update = ask. The release is offered in a notification, never in a window that waits for an answer: Ampify and
// Apple Music stay usable, and a notification left alone comes back only a week later. True when it may be installed
// now: the person clicked this notification, or an earlier one for the same release.
bool OfferTaken(Context& ctx, const std::string& version) {
    std::filesystem::path file = ctx.kit.Cache() / "update-offer.txt";
    Offer offer = ReadOffer(file);
    if (offer.version == version && offer.accepted) return true;
    std::int64_t now = NowSeconds();
    if (offer.version == version && offer.at >= 0 && now >= offer.at && now - offer.at < kOfferQuietSeconds) {
        ctx.out << "Version " << version << " was offered less than a week ago and left alone: not offered again yet.\n";
        return false;
    }
    std::optional<bool> answer = platform::OfferNotice(i18n::Text("update.askTitle", {{"version", version}}),
                                                       i18n::Text("update.askText", {{"installed", kKitVersion}}),
                                                       kOfferWaitMs);
    if (!answer) {
        ctx.out << "Version " << version << " could not be offered (no notification area yet): offered on the next run.\n";
        return false;
    }
    bool clicked = *answer;
    std::error_code ec;
    std::filesystem::create_directories(ctx.kit.Cache(), ec);
    std::string error;
    if (!WriteTextFile(file, "version=" + version + "\nat=" + std::to_string(now) + "\naccepted=" + (clicked ? "1" : "0") + "\n",
                       error) &&
        !clicked) {
        ctx.out << "Version " << version << " offered in a notification and left alone, but that could not be kept ("
                << error << "): offered again on the next run.\n";
        return false;
    }
    ctx.out << "Version " << version
            << (clicked ? " taken from its notification.\n" : " offered in a notification and left alone: offered again in a week.\n");
    return clicked;
}

}  // namespace

int CmdAutoUpdate(Context& ctx) {
    Settings settings;
    settings.Load(ctx.kit.Settings().string());
    uirender::KitUpdate mode = uirender::ReadKitUpdate(settings);
    std::filesystem::path file = ctx.kit.Cache() / "auto-update.txt";
    if (!MayLookForUpdate(ctx, mode, file)) return 0;

    std::string source = ctx.options.releaseUrl ? *ctx.options.releaseUrl : DefaultReleaseUrl("");
    std::string error;
    std::optional<ReleaseInfo> release = FetchRelease(ctx, source, error, kUpdateCheckTimeoutSeconds);
    if (!release) return Failure(ctx, error);  // not written: offline now, looked for again on the next run
    std::error_code ec;
    std::filesystem::create_directories(ctx.kit.Cache(), ec);
    std::string writeError;
    WriteTextFile(file, "checked=" + std::to_string(NowSeconds()) + "\nlatest=" + release->version + "\n", writeError);
    if (CompareDottedVersions(release->version, kKitVersion) <= 0) {
        ctx.out << "ampify is up to date (version " << kKitVersion << ").\n";
        return 0;
    }

    UseUserLanguage(settings);
    if (mode == uirender::KitUpdate::Ask && !OfferTaken(ctx, release->version)) return 0;
    Options installing = ctx.options;
    installing.apply = true;
    Context install{installing, ctx.kit, ctx.in, ctx.out, ctx.err};
    int code = ReadyToInstall(ctx) ? InstallKitRelease(install, source, "")
                                   : Failure(ctx, "Apple Music is open: the update waits for it to close");
    if (code != 0) {
        std::filesystem::remove(file, ec);  // Apple Music opened meanwhile, or a download failed: the next run tries again
        return code;
    }
    platform::ShowNotice(i18n::Text("update.doneTitle"), i18n::Text("update.doneText", {{"version", release->version}}));
    return 0;
}

namespace {

bool OnTheList(Context& ctx, const std::string& version) {
    std::vector<std::string> extra = LoadVersionList(ctx.kit.Root() / std::string(kVersionListFile));
    return IsKnownAppVersion(version, KnownAppVersionsWith(extra));
}

// Looks for a newer list of tested versions, with no output, and says whether `version` came onto the list just now:
// a version that was already on it (so Ampify is off for another reason) gains nothing from a restart.
bool VersionListGained(Context& ctx, const std::string& version) {
    if (OnTheList(ctx, version)) return false;
    std::ostringstream out;
    std::ostringstream err;
    Options updating = ctx.options;
    updating.positional = {"versions", "update"};
    Context quiet{updating, ctx.kit, ctx.in, out, err};
    CmdVersions(quiet);
    return OnTheList(ctx, version);
}

}  // namespace

int CmdNotice(Context& ctx) {
    const std::vector<std::string>& args = ctx.options.positional;
    bool safeMode = args.size() == 2 && args[1] == "safe-mode";
    bool version = args.size() == 3 && args[1] == "version" && !args[2].empty();
    bool exports = args.size() == 3 && args[1] == "exports" && !args[2].empty();
    if (!safeMode && !version && !exports) {
        return Usage(ctx, "notice takes safe-mode, or version or exports and an Apple Music version");
    }

    Settings settings;
    settings.Load(ctx.kit.Settings().string());
    UseUserLanguage(settings);
    std::string key = "off.safe";
    // A dry run asks nothing of the network: it shows the text of an untested version.
    if (exports) key = "off.exports";
    if (version) key = ctx.options.dryRun || !VersionListGained(ctx, args[2]) ? "off.version" : "off.listed";
    std::string title = i18n::Text(key + "Title");
    std::string text = i18n::Text(key + "Text", {{"version", version ? args[2] : ""}});
    if (ctx.options.dryRun) {
        ctx.out << title << "\n" << text << "\n";
        return 0;
    }
    platform::ShowNotice(title, text);
    return 0;
}

}  // namespace ampify::cli
