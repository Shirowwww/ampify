// Upkeep commands: the interface inspector (devtools), the list of tested Apple Music builds (versions), the kit
// folder in the file manager (config --dir), and the one-line notice about a new kit release (check_kit_update).

#include <chrono>
#include <cstdint>
#include <iterator>

#include "../core/apple_music.hpp"
#include "../core/build_signature.hpp"
#include "../core/version_gate.hpp"
#include "../core/version_list.hpp"
#include "internal.hpp"
#include "platform.hpp"
#include "sha256.hpp"

#ifndef AMPIFY_VERSION
#define AMPIFY_VERSION "0.0.0-dev"
#endif

namespace ampify::cli {

namespace {

constexpr std::int64_t kUpdateCheckPeriodSeconds = 24 * 60 * 60;
constexpr int kUpdateCheckTimeoutSeconds = 10;

std::filesystem::path InspectorFile(const KitLayout& kit) { return kit.Journal() / "inspector.txt"; }

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

}  // namespace

int CmdDevtools(Context& ctx) {
    const std::vector<std::string>& args = ctx.options.positional;
    if (args.size() > 2) return Usage(ctx, "devtools takes on, off, or nothing (for on)");
    std::string action = args.size() == 2 ? args[1] : "on";
    if (action != "on" && action != "off") return Usage(ctx, "devtools takes on or off, not " + action);

    Settings settings;
    std::string error;
    if (!LoadSettings(ctx, settings, error)) return Failure(ctx, error);
    settings.SetBool("theme.inspect", action == "on");
    if (!SaveSettings(ctx, settings, error)) return Failure(ctx, error);

    if (action == "off") {
        ctx.out << "Inspector turned off (theme.inspect = 0).\n";
        return 0;
    }
    ctx.out << "Inspector turned on (theme.inspect = 1).\n"
            << "The next time the kernel runs in Apple Music (at its next start), the interface tree is written\n"
            << "to " << Utf8(InspectorFile(ctx.kit)) << ", then the kernel sets theme.inspect back to 0.\n"
            << "Show it with: ampify inspect --tree\n"
            << "This needs a kernel with the XAML theme engine. Without it, no file is written.\n";
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
        ctx.out << "Apple Music versions accepted by the kernel:\n";
        for (std::string_view version : kKnownAppVersions) {
            ctx.out << "  " << Pad(version, 20) << "built into the kernel\n";
        }
        for (const std::string& version : extra) {
            if (IsKnownAppVersion(version)) continue;
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
                << (known ? " (tested)\n" : " (not tested: the kernel stays off. Try ampify versions update)\n");
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
    if (!settings.Load(ctx.kit.Settings().string()) || !settings.GetBool("check_kit_update", false)) return;

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
    if (!IsValidVersion(cache.latest) || CompareReleaseVersions(cache.latest, AMPIFY_VERSION) <= 0) return;
    ctx.err << "ampify: version " << cache.latest << " of Ampify is available (installed: " << AMPIFY_VERSION
            << "). Run ampify update to download it.\n";
}

}  // namespace ampify::cli
