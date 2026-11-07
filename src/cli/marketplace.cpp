// Catalogue, installs, updates and removals of elements, and updates of the kit itself.
// Every download goes through the same checks: the address policy, the SHA-256 pin, the archive listing,
// the link check and the manifest check. Nothing from the network is run or installed before all of them pass.

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>

#include "../core/json.hpp"
#include "../core/kit_layout.hpp"
#include "apple_facts.hpp"
#include "../core/settings.hpp"
#include "../core/theme/files.hpp"
#include "../core/theme/theme.hpp"
#include "../core/ui_render/mods.hpp"
#include "archive.hpp"
#include "backup.hpp"
#include "catalog.hpp"
#include "element.hpp"
#include "internal.hpp"
#include "kit.hpp"
#include "messages.hpp"
#include "platform.hpp"
#include "release.hpp"
#include "release_facts.hpp"
#include "setting_keys.hpp"
#include "sha256.hpp"

namespace ampify::cli {

namespace {

constexpr std::string_view kRequiredKitFiles[] = {kCliFile, kLauncherFile, kKernelFile};
// A README is a page of text: anything larger is not one, and the detail page waits on it.
constexpr std::uintmax_t kMaxReadmeBytes = 256 * 1024;
constexpr int kReadmeSeconds = 15;

bool StartsWith(std::string_view text, std::string_view prefix) {
    return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}

// file:///C:/dev/x.zip becomes C:/dev/x.zip, and file:///home/me/x.zip becomes /home/me/x.zip.
std::string LocalPathOf(std::string_view url) {
    std::string_view rest = url.substr(7);  // after "file://"
    if (rest.size() >= 3 && rest[0] == '/' && std::isalpha(static_cast<unsigned char>(rest[1])) && rest[2] == ':') {
        rest.remove_prefix(1);
    }
    return std::string(rest);
}

}  // namespace

bool FetchTo(const std::string& url, const std::filesystem::path& destination, std::string& error, int maxSeconds) {
    if (StartsWith(url, "file://")) {
        std::error_code ec;
        std::filesystem::copy_file(Utf8ToPath(LocalPathOf(url)), destination,
                                   std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            error = "cannot copy: " + LocalPathOf(url);
            return false;
        }
        return true;
    }
    return platform::DownloadHttps(url, destination, error, maxSeconds);
}

namespace {

void RemoveQuietly(const std::filesystem::path& path) {
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
}

}  // namespace

bool LoadCatalog(Context& ctx, Catalog& catalog, bool& local) {
    Settings settings;
    settings.Load(ctx.kit.Settings().string());
    std::string source = ctx.options.catalog ? *ctx.options.catalog : settings.GetString("catalog.url", std::string(kDefaultCatalogUrl));

    std::vector<std::string> errors;
    std::string text;
    std::filesystem::path cached = ctx.kit.Cache() / "catalog.json";

    if (StartsWith(source, "https://")) {
        local = false;
        std::filesystem::path part = ctx.kit.Downloads() / "catalog.json.part";
        std::error_code ec;
        std::filesystem::create_directories(ctx.kit.Downloads(), ec);
        std::string error;
        if (!platform::DownloadHttps(source, part, error) || !ReadWholeFile(part, text)) {
            RemoveQuietly(part);
            if (!ReadWholeFile(cached, text)) {
                ctx.err << "ampify: catalog unreachable: " << error
                        << ". Check the internet connection, then try again.\n";
                return false;
            }
            ctx.err << "ampify: remote catalog unreachable, using the local copy\n";
        } else {
            RemoveQuietly(part);
            std::filesystem::create_directories(ctx.kit.Cache(), ec);
            std::string writeError;
            WriteTextFile(cached, text, writeError);
        }
    } else {
        local = true;
        std::string path = StartsWith(source, "file://") ? LocalPathOf(source) : source;
        if (!ReadWholeFile(Utf8ToPath(path), text)) {
            ctx.err << "ampify: catalog not found: " << path << "\n";
            return false;
        }
    }

    std::optional<Catalog> parsed = ParseCatalog(text, local, errors);
    if (!parsed) {
        ctx.err << "ampify: invalid catalog\n";
        for (const std::string& problem : errors) ctx.err << "  - " << problem << "\n";
        return false;
    }
    catalog = std::move(*parsed);
    if (!local) catalog.sourceUrl = source;
    return true;
}

namespace {

// The element that is installed under this identifier, whatever its kind.
std::vector<InstalledElement> InstalledNamed(const KitLayout& kit, std::string_view id) {
    std::vector<InstalledElement> found;
    for (InstalledElement& element : ListInstalled(kit)) {
        if (element.id == id) found.push_back(std::move(element));
    }
    return found;
}

std::string JoinPermissions(const std::vector<std::string>& permissions) {
    std::string joined;
    for (const std::string& permission : permissions) joined += (joined.empty() ? "" : ", ") + permission;
    return joined;
}

// Downloads, checks and puts one catalogue entry in place. Used by install and by update.
int InstallEntry(Context& ctx, const CatalogEntry& entry, bool localCatalog, bool isUpdate) {
    if (!IsAllowedDownloadUrl(entry.url, localCatalog)) return Failure(ctx, "address refused: " + entry.url);

    ctx.out << entry.name << " (" << entry.id << ") " << entry.version << ", by " << entry.author << "\n"
            << entry.description << "\n";
    std::string permissions = JoinPermissions(entry.permissions);
    ctx.out << (permissions.empty() ? std::string("No permission requested.")
                                    : "Requested permissions: " + permissions)
            << "\n";

    if (!ctx.options.yes) {
        ctx.out << (isUpdate ? "Update " : "Install ") << entry.id << "? [y/N] " << std::flush;
        std::string answer;
        std::getline(ctx.in, answer);
        if (!IsYes(answer)) {
            ctx.out << "Cancelled.\n";
            return 1;
        }
    }

    std::error_code ec;
    std::filesystem::create_directories(ctx.kit.Downloads(), ec);
    std::string stem = entry.id + "-" + entry.version;
    std::filesystem::path archive = ctx.kit.Downloads() / (stem + ".zip");
    std::filesystem::path staging = ctx.kit.Downloads() / (stem + ".staging");
    RemoveQuietly(staging);
    RemoveQuietly(archive);

    std::string error;
    ctx.out << "Downloading...\n" << std::flush;
    if (!FetchTo(entry.url, archive, error)) {
        RemoveQuietly(archive);
        return Failure(ctx, error);
    }

    std::optional<std::string> digest = Sha256File(archive);
    if (!digest || *digest != entry.sha256) {
        RemoveQuietly(archive);
        return Failure(ctx, "SHA-256 differs from the catalog's: archive refused");
    }

    std::vector<std::string> entries;
    if (!platform::ListZip(archive, entries, error)) {
        RemoveQuietly(archive);
        return Failure(ctx, error);
    }
    std::string problem = CheckArchiveListing(entries);
    if (!problem.empty()) {
        RemoveQuietly(archive);
        return Failure(ctx, problem);
    }

    // The element is extracted into a folder named after its id: the manifest must match its folder name.
    std::filesystem::path extracted = staging / entry.id;
    std::filesystem::create_directories(extracted, ec);
    if (!platform::ExtractZip(archive, extracted, error)) {
        RemoveQuietly(archive);
        RemoveQuietly(staging);
        return Failure(ctx, error);
    }
    RemoveQuietly(archive);

    if (ContainsLink(extracted)) {
        RemoveQuietly(staging);
        return Failure(ctx, "symbolic link not allowed in the archive");
    }
    FolderValidation validation = ValidateElementFolder(extracted);
    if (!validation.ok()) {
        RemoveQuietly(staging);
        ctx.err << "ampify: the archive does not contain a valid element:\n";
        for (const std::string& message : validation.errors) ctx.err << "  - " << message << "\n";
        return 1;
    }
    const Manifest& manifest = *validation.manifest;
    if (manifest.id != entry.id || manifest.version != entry.version || manifest.kind != entry.kind) {
        RemoveQuietly(staging);
        return Failure(ctx, "the manifest in the archive does not match the catalog");
    }

    // Swap the folders: the previous version is kept until the new one is in place.
    std::filesystem::path destination = ctx.kit.Element(ToString(entry.kind), entry.id);
    std::filesystem::path previous = ctx.kit.Folder(ToString(entry.kind)) / ("." + entry.id + ".previous");
    std::filesystem::create_directories(ctx.kit.Folder(ToString(entry.kind)), ec);
    RemoveQuietly(previous);
    if (std::filesystem::exists(destination, ec)) {
        std::filesystem::rename(destination, previous, ec);
        if (ec) {
            RemoveQuietly(staging);
            return Failure(ctx, "cannot replace: " + Utf8(destination));
        }
    }
    std::filesystem::rename(extracted, destination, ec);
    if (ec) {
        if (std::filesystem::exists(previous, ec)) std::filesystem::rename(previous, destination, ec);
        RemoveQuietly(staging);
        return Failure(ctx, "cannot install: " + Utf8(destination));
    }
    RemoveQuietly(staging);
    RemoveQuietly(previous);
    // A running kernel loads the new folder at once (installed disabled), or restarts the updated element.
    RequestReload(ctx, {isUpdate ? "restart " + ReloadTarget(ToString(entry.kind), entry.id) : std::string("rescan")});

    ctx.out << entry.id << " " << entry.version << (isUpdate ? " updated" : " installed") << ". ";
    if (entry.kind == ElementKind::Theme) {
        ctx.out << "Use it with ampify theme set " << entry.id << ".\n";
    } else {
        Settings settings;
        settings.Load(ctx.kit.Settings().string());
        if (IsEnabled(settings, ToString(entry.kind), entry.id)) {
            ctx.out << "It stays on.\n";
        } else {
            ctx.out << "It is off: run ampify " << ToString(entry.kind) << " enable " << entry.id << " to turn it on.\n";
        }
    }
    return 0;
}

int UpdateElement(Context& ctx, const std::string& id) {
    std::vector<InstalledElement> installed = InstalledNamed(ctx.kit, id);
    if (installed.empty()) return Failure(ctx, "element not installed: " + id + ". Use ampify install " + id);
    if (installed.size() > 1) return Failure(ctx, "several elements have the id " + id);

    Catalog catalog;
    bool local = false;
    if (!LoadCatalog(ctx, catalog, local)) return 1;
    const CatalogEntry* entry = FindCatalogEntry(catalog, id);
    if (entry == nullptr) return Failure(ctx, "element not in the catalog: " + id);
    if (ToString(entry->kind) != installed.front().kind) {
        return Failure(ctx, "the catalog lists " + id + " as " + std::string(ToString(entry->kind)) + ", not as " + installed.front().kind);
    }

    std::string installedVersion = installed.front().manifest ? installed.front().manifest->version : "0.0.0";
    if (CompareDottedVersions(entry->version, installedVersion) <= 0) {
        ctx.out << id << " is already up to date (version " << installedVersion << ").\n";
        return 0;
    }
    return InstallEntry(ctx, *entry, local, true);
}

}  // namespace

std::string DefaultReleaseUrl(std::string_view version) {
    // A given release, by its tag: `ampify install-kit 0.1.0` reads .../releases/tags/v0.1.0.
    if (version.empty()) return std::string(kReleaseApiUrl) + "/latest";
    return std::string(kReleaseApiUrl) + "/tags/v" + std::string(version);
}

std::optional<ReleaseInfo> FetchRelease(Context& ctx, const std::string& source, std::string& error, int maxSeconds) {
    if (!StartsWith(source, "file://") && !StartsWith(source, "https://")) {
        error = "release address refused: " + source;
        return std::nullopt;
    }
    std::error_code ec;
    std::filesystem::create_directories(ctx.kit.Downloads(), ec);
    std::filesystem::path releaseFile = platform::TemporaryFile("ampify-release");
    std::string text;
    if (!FetchTo(source, releaseFile, error, maxSeconds) || !ReadWholeFile(releaseFile, text)) {
        RemoveQuietly(releaseFile);
        if (error.empty()) error = "unreadable release";
        return std::nullopt;
    }
    RemoveQuietly(releaseFile);

    std::optional<ReleaseInfo> release = ParseRelease(text, error);
    if (!release) return std::nullopt;
    if (!IsReleaseVersion(release->version)) {
        error = "invalid release version: " + release->tag;
        return std::nullopt;
    }
    return release;
}

namespace {

// The first of file.old, file.1.old ... file.9.old that is free once the stale ones were removed.
std::filesystem::path OldFileName(const std::filesystem::path& file) {
    constexpr int kOldNames = 10;
    std::error_code ec;
    for (int i = 0; i < kOldNames; i++) {
        std::filesystem::path old = file;
        old += i == 0 ? ".old" : "." + std::to_string(i) + ".old";
        RemoveQuietly(old);
        if (!std::filesystem::exists(old, ec)) return old;
    }
    return file.string() + ".last.old";
}

}  // namespace

int InstallKitRelease(Context& ctx, const std::string& source, const std::string& wanted) {
    std::string error;
    std::optional<ReleaseInfo> release = FetchRelease(ctx, source, error);
    if (!release) return Failure(ctx, error);
    bool releaseLocal = StartsWith(source, "file://");
    std::error_code ec;
    std::filesystem::create_directories(ctx.kit.Updates(), ec);
    // The command to run once Apple Music is closed, to install what this run downloaded.
    std::string applyCommand = wanted.empty() ? "ampify update --apply" : "ampify install-kit " + wanted + " --apply";

    if (wanted.empty()) {
        if (CompareDottedVersions(release->version, kKitVersion) <= 0) {
            ctx.out << "ampify is up to date (version " << kKitVersion << ").\n";
            return 0;
        }
        if (ctx.options.check) {
            ctx.out << "Version " << release->version << " is available (installed: " << kKitVersion
                    << "). Run ampify update to download it.\n";
            return 0;
        }
    } else {
        if (release->version != wanted) {
            return Failure(ctx, "the release read is version " + release->version + ", not version " + wanted);
        }
        if (ctx.options.check) {
            ctx.out << "Version " << wanted << " is available (installed: " << kKitVersion
                    << "). Run ampify install-kit " << wanted << " to download it.\n";
            return 0;
        }
        if (CompareDottedVersions(wanted, kKitVersion) < 0) {
            ctx.out << "Warning: version " << wanted << " is older than the installed one (" << kKitVersion
                    << "). Your settings and elements stay in place.\n";
        }
    }

    std::string archiveName = std::string(kPackagePrefix) + release->version + std::string(kPackageSuffix);
    const std::string checksums(kChecksumsFile);
    auto archiveAddress = release->assets.find(archiveName);
    auto sumsAddress = release->assets.find(checksums);
    if (archiveAddress == release->assets.end() || sumsAddress == release->assets.end()) {
        return Failure(ctx, "release " + release->tag + " does not contain " + archiveName + " and " + checksums);
    }
    if (!IsAllowedDownloadUrl(archiveAddress->second, releaseLocal) ||
        !IsAllowedDownloadUrl(sumsAddress->second, releaseLocal)) {
        return Failure(ctx, "address refused in the release");
    }

    std::filesystem::path archive = ctx.kit.Updates() / archiveName;
    std::filesystem::path sumsFile = ctx.kit.Updates() / checksums;
    ctx.out << "Downloading version " << release->version << "...\n" << std::flush;
    if (!FetchTo(archiveAddress->second, archive, error) || !FetchTo(sumsAddress->second, sumsFile, error)) {
        RemoveQuietly(archive);
        return Failure(ctx, error);
    }

    std::string sumsText;
    ReadWholeFile(sumsFile, sumsText);
    std::map<std::string, std::string> sums = ParseChecksums(sumsText);
    RemoveQuietly(sumsFile);
    auto expected = sums.find(archiveName);
    std::optional<std::string> digest = Sha256File(archive);
    if (expected == sums.end() || !digest || *digest != expected->second) {
        RemoveQuietly(archive);
        return Failure(ctx, "wrong SHA-256 for the update archive: archive refused");
    }

    std::vector<std::string> entries;
    if (!platform::ListZip(archive, entries, error) || !CheckArchiveListing(entries).empty()) {
        RemoveQuietly(archive);
        return Failure(ctx, error.empty() ? CheckArchiveListing(entries) : error);
    }

    std::filesystem::path staged = ctx.kit.Updates() / release->version;
    RemoveQuietly(staged);
    std::filesystem::create_directories(staged, ec);
    if (!platform::ExtractZip(archive, staged, error)) {
        RemoveQuietly(archive);
        RemoveQuietly(staged);
        return Failure(ctx, error);
    }
    RemoveQuietly(archive);
    if (ContainsLink(staged)) {
        RemoveQuietly(staged);
        return Failure(ctx, "symbolic link not allowed in the update archive");
    }
    for (std::string_view required : kRequiredKitFiles) {
        if (!std::filesystem::is_regular_file(staged / required, ec)) {
            RemoveQuietly(staged);
            return Failure(ctx, "incomplete archive, missing file: " + std::string(required));
        }
    }

    if (!ctx.options.apply) {
        ctx.out << "Version " << release->version << " downloaded and checked in " << Utf8(staged)
                << ".\nClose Apple Music, then run: " << applyCommand << "\n";
        return 0;
    }

    if (platform::IsProcessRunning(std::string(kAppleMusicImage)) ||
        platform::IsProcessRunning(std::string(kLibraryAgentImage))) {
        return Failure(ctx, "close Apple Music before installing the update");
    }
    std::filesystem::path installDir = ctx.options.installDir ? Utf8ToPath(*ctx.options.installDir)
                                                              : platform::ExecutableDirectory();
    for (const auto& file : std::filesystem::directory_iterator(staged, ec)) {
        if (!file.is_regular_file(ec)) continue;
        std::filesystem::path destination = installDir / file.path().filename();
        if (std::filesystem::exists(destination, ec)) {
            // A running program can be renamed on Windows, not overwritten. Keep the old file as *.old, or *.1.old and
            // so on while an older *.old still runs (a launcher started before the last update).
            std::filesystem::path old = OldFileName(destination);
            std::filesystem::rename(destination, old, ec);
            if (ec) return Failure(ctx, "cannot replace " + Utf8(destination));
        }
        std::filesystem::copy_file(file.path(), destination, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) return Failure(ctx, "cannot copy to " + Utf8(destination));
    }
    // The Marketplace and Now Playing ship with the kit (src/core/kit_layout.hpp): an update replaces them too.
    for (const std::string& line : InstallBundledElements(staged, ctx.kit.Root())) {
        ctx.err << "ampify: not updated: " << line << "\n";
    }
    RemoveQuietly(staged);
    // The background launcher still runs the old file (renamed *.old): it restarts from the new one.
    platform::AutoStart autoStart = platform::AutoStartState();
    if ((autoStart.registered || autoStart.running) && platform::IsInstallDirectory(installDir)) {
        std::string error;
        if (!platform::RestartAutoStart(installDir / kLauncherFile, error)) {
            ctx.err << "ampify: the background launcher did not restart (" << error << "): run ampify apply\n";
        }
    }
    // Settings > Apps shows the version the installer wrote: it follows the update (only for the installed copy).
    if (platform::IsInstallDirectory(installDir)) platform::SetInstalledVersion(release->version);
    ctx.out << "ampify " << release->version << " installed in " << Utf8(installDir)
            << ". Open Apple Music again to use it.\n";
    return 0;
}

namespace {

Json TextList(const std::vector<std::string>& items) {
    Json::Array list;
    for (const std::string& item : items) list.emplace_back(item);
    return list;
}

Json InfoJson(const ElementInfo& info) {
    Json item = Json::Object{{"id", info.id},           {"name", info.name},
                             {"kind", std::string(ToString(info.kind))},       {"version", info.version},
                             {"author", info.author},   {"description", info.description},
                             {"permissions", TextList(info.permissions)},
                             {"tags", TextList(info.tags)}};
    if (!info.translations.empty()) {
        Json::Object languages;
        auto texts = [](const std::map<std::string, std::string>& map) {
            Json::Object object;
            for (const auto& [key, text] : map) object.emplace_back(key, text);
            return Json(std::move(object));
        };
        for (const auto& [code, text] : info.translations) {
            Json language = Json::Object{{"name", text.name}, {"description", text.description}};
            // Only an element with settings translates their labels and choices.
            if (!text.settings.empty()) language.Set("settings", texts(text.settings));
            if (!text.options.empty()) language.Set("options", texts(text.options));
            if (!text.groups.empty()) language.Set("groups", texts(text.groups));
            if (!text.settingDescriptions.empty()) language.Set("settingDescriptions", texts(text.settingDescriptions));
            languages.emplace_back(code, std::move(language));
        }
        item.Set("translations", Json(std::move(languages)));
    }
    return item;
}

// The settings an installed element declares, each with its value now, for the Marketplace's settings panel.
Json SettingsJson(const uirender::ModEntry& element) {
    Json::Array list;
    for (size_t index = 0; index < element.settings.size(); index++) {
        const SettingSpec& spec = element.settings[index];
        Json setting = Json::Object{{"key", spec.key},
                                    {"label", spec.label},
                                    {"type", std::string(ToString(spec.type))},
                                    {"value", element.values[index]}};
        if (!spec.options.empty()) setting.Set("options", TextList(spec.options));
        if (spec.min) setting.Set("min", *spec.min);
        if (spec.max) setting.Set("max", *spec.max);
        if (!spec.group.empty()) setting.Set("group", spec.group);
        if (!spec.description.empty()) setting.Set("description", spec.description);
        list.push_back(std::move(setting));
    }
    return list;
}

// `ampify market --json`: what the Marketplace page shows. The catalogue (empty with catalogOk false when it cannot be
// read; the reason goes to the error stream), every installed element with its state, and the active theme.
int PrintMarketJson(Context& ctx) {
    Catalog catalog;
    bool local = false;
    bool catalogOk = LoadCatalog(ctx, catalog, local);
    Settings settings;
    std::string error;
    LoadSettings(ctx, settings, error);

    Json::Array entries;
    // The page loads nothing from another address: a preview is passed on only from the catalog's host or the archive's.
    std::string catalogHost = UrlHost(catalog.sourceUrl);
    for (const CatalogEntry& entry : catalog.entries) {
        Json item = InfoJson(entry);
        if (entry.preview) {
            std::string host = UrlHost(*entry.preview);
            if (!host.empty() && (host == catalogHost || host == UrlHost(entry.url))) item.Set("preview", *entry.preview);
        }
        entries.push_back(std::move(item));
    }
    Json::Array installed;
    for (const InstalledElement& element : ListInstalled(ctx.kit)) {
        Json item = element.manifest ? InfoJson(*element.manifest) : Json(Json::Object{{"id", element.id}, {"kind", element.kind}});
        item.Set("valid", element.Valid());
        if (!element.Valid()) item.Set("errors", TextList(element.errors));  // the detail page says why it cannot load
        item.Set("enabled", IsEnabled(settings, element.kind, element.id));
        if (element.manifest && element.manifest->preview) {
            std::filesystem::path image =
                std::filesystem::absolute(ctx.kit.Element(element.kind, element.id) / *element.manifest->preview);
            std::error_code ec;
            if (std::filesystem::is_regular_file(image, ec)) item.Set("preview", Utf8(image));
        }
        if (std::optional<uirender::ModEntry> declared = uirender::ElementSettings(ctx.kit.Root(), element.kind, element.id)) {
            item.Set("settings", SettingsJson(*declared));
        }
        if (element.kind == "theme") {
            theme::FolderFiles files(ctx.kit.Root() / "themes" / element.id);
            item.Set("schemes", TextList(theme::ReadSchemes(files)));
            // The scheme the theme starts on while theme.scheme is empty, for the page's scheme menu.
            item.Set("defaultScheme", theme::ReadDefaultScheme(files));
        }
        installed.push_back(std::move(item));
    }
    Json result = Json::Object{{"catalogOk", catalogOk},
                               {"catalog", std::move(entries)},
                               {"installed", std::move(installed)},
                               {"theme", settings.GetString("theme.active", "")},
                               {"scheme", settings.GetString("theme.scheme", "")}};
    ctx.out << result.Dump() << "\n";
    return 0;
}

// The README published next to an archive, under its name: .../demo-1.0.0.zip gives .../demo-1.0.0.md.
std::optional<std::string> ReadmeUrlOf(const std::string& archiveUrl) {
    constexpr std::string_view kZip = ".zip";
    if (archiveUrl.size() <= kZip.size() || archiveUrl.compare(archiveUrl.size() - kZip.size(), kZip.size(), kZip) != 0) {
        return std::nullopt;
    }
    return archiveUrl.substr(0, archiveUrl.size() - kZip.size()) + ".md";
}

// Downloads the README of a catalog entry once per version, into the cache. False when it has none or the network
// fails: both read as "no README" on the page.
bool FetchReadme(Context& ctx, const CatalogEntry& entry, bool localCatalog, std::string& text) {
    std::optional<std::string> url = ReadmeUrlOf(entry.url);
    if (!url || !IsAllowedDownloadUrl(*url, localCatalog)) return false;
    std::filesystem::path cached = ctx.kit.Cache() / "readme" / (entry.id + "-" + entry.version + ".md");
    // A local catalog is a work copy: its README is read afresh each time.
    if (!localCatalog && ReadWholeFile(cached, text)) return true;
    std::error_code ec;
    std::filesystem::create_directories(cached.parent_path(), ec);
    std::filesystem::path part = cached;
    part += ".part";
    std::string error;
    bool fetched = FetchTo(*url, part, error, kReadmeSeconds) && std::filesystem::file_size(part, ec) <= kMaxReadmeBytes &&
                   !ec && ReadWholeFile(part, text);
    if (fetched) std::filesystem::rename(part, cached, ec);
    RemoveQuietly(part);
    return fetched;
}

// Without its carriage returns: the output stream adds its own line ends, and a README written on Windows would get
// each line end twice.
int PrintText(Context& ctx, std::string text) {
    std::erase(text, '\r');
    ctx.out << text;
    return 0;
}

// `ampify market readme <id>`: the element's README.md for the Marketplace's detail page. The installed copy's when it
// is the catalog's version (or the catalog does not list it), else the one published next to the catalog's archive.
// Prints nothing when there is none.
int PrintReadme(Context& ctx, const std::string& id) {
    Catalog catalog;
    bool local = false;
    bool catalogOk = LoadCatalog(ctx, catalog, local);
    const CatalogEntry* entry = nullptr;
    for (const CatalogEntry& candidate : catalog.entries) {
        if (candidate.id == id) entry = &candidate;
    }

    std::string text;
    std::string installedText;
    for (const InstalledElement& element : InstalledNamed(ctx.kit, id)) {
        if (!ReadWholeFile(ctx.kit.Element(element.kind, element.id) / "README.md", installedText)) continue;
        bool current = !entry || (element.manifest && element.manifest->version == entry->version);
        if (current) return PrintText(ctx, installedText);
    }
    if (catalogOk && entry && FetchReadme(ctx, *entry, local, text)) return PrintText(ctx, text);
    return PrintText(ctx, installedText);  // an older copy's README is better than none
}

}  // namespace

int CmdMarket(Context& ctx) {
    if (ctx.options.json) return PrintMarketJson(ctx);
    if (ctx.options.positional.size() >= 2 && ctx.options.positional[1] == "readme") {
        if (ctx.options.positional.size() != 3) return Usage(ctx, "market readme needs an element id");
        return PrintReadme(ctx, ctx.options.positional[2]);
    }
    Catalog catalog;
    bool local = false;
    if (!LoadCatalog(ctx, catalog, local)) return 1;
    if (catalog.entries.empty()) {
        ctx.out << "The catalog is empty for now.\n";
        return 0;
    }

    std::vector<InstalledElement> installed = ListInstalled(ctx.kit);
    ctx.out << Pad("ID", 24) << Pad("VERSION", 10) << Pad("TYPE", 10) << Pad("AUTHOR", 18) << "STATE\n";
    for (const CatalogEntry& entry : catalog.entries) {
        std::string state = "not installed";
        for (const InstalledElement& element : installed) {
            if (element.kind == ToString(entry.kind) && element.id == entry.id) {
                state = "installed " + (element.manifest ? element.manifest->version : std::string("?"));
            }
        }
        ctx.out << Pad(entry.id, 24) << Pad(entry.version, 10) << Pad(ToString(entry.kind), 10) << Pad(entry.author, 18)
                << state << ": " << entry.description << "\n";
    }
    return 0;
}

int CmdInstall(Context& ctx) {
    if (ctx.options.positional.size() != 2) return Usage(ctx, "install needs an element id");
    const std::string& id = ctx.options.positional[1];
    if (!IsElementId(id)) return Failure(ctx, Tr("invalidId", {{"id", id}}));

    Catalog catalog;
    bool local = false;
    if (!LoadCatalog(ctx, catalog, local)) return 1;
    const CatalogEntry* entry = FindCatalogEntry(catalog, id);
    if (entry == nullptr) return Failure(ctx, "element not in the catalog: " + id);
    return InstallEntry(ctx, *entry, local, false);
}

int CmdRemove(Context& ctx) {
    if (ctx.options.positional.size() != 2) return Usage(ctx, "remove needs an element id");
    const std::string& id = ctx.options.positional[1];
    if (!IsElementId(id)) return Failure(ctx, Tr("invalidId", {{"id", id}}));

    std::vector<InstalledElement> matches = InstalledNamed(ctx.kit, id);
    if (matches.empty()) {
        if (IsBuiltInExtension(id)) {
            return Failure(ctx, id + " is built into the kernel and cannot be removed, only turned off");
        }
        return Failure(ctx, "element not found: " + id);
    }
    if (matches.size() > 1) return Failure(ctx, "several elements have the id " + id);
    const std::string kind = matches.front().kind;

    std::string error;
    std::optional<std::string> snapshot = CreateBackup(ctx.kit, error);
    if (!snapshot) return Failure(ctx, error);

    RemoveQuietly(ctx.kit.Element(kind, id));
    if (std::filesystem::exists(ctx.kit.Element(kind, id))) return Failure(ctx, "cannot delete: " + id);

    Settings settings;
    if (!LoadSettings(ctx, settings, error)) return Failure(ctx, error);
    if (kind == "theme") {
        if (settings.GetString("theme.active", "") == id) settings.Set("theme.active", "");
    } else {
        settings.Remove(ElementRef{KindNamed(kind).value_or(ElementKind::Extension), id}.EnabledKey());
    }
    if (!SaveSettings(ctx, settings, error)) return Failure(ctx, error);
    RequestReload(ctx, {"stop " + ReloadTarget(kind, id), "rescan"});

    ctx.out << id << " removed. Backup made first: " << *snapshot << " (ampify restore --backup " << *snapshot
            << " to go back).\n";
    return 0;
}

int CmdInstallKit(Context& ctx) {
    const std::vector<std::string>& args = ctx.options.positional;
    if (args.size() != 2) return Usage(ctx, "install-kit needs a version of Ampify, for example: ampify install-kit 0.1.0");
    std::string version = args[1];
    if (!version.empty() && (version.front() == 'v' || version.front() == 'V')) version.erase(0, 1);
    if (!IsReleaseVersion(version)) return Failure(ctx, "invalid version: " + args[1] + " (expected: 0.1.0)");
    return InstallKitRelease(ctx, ctx.options.releaseUrl ? *ctx.options.releaseUrl : DefaultReleaseUrl(version), version);
}

int CmdUpdate(Context& ctx) {
    const std::vector<std::string>& args = ctx.options.positional;
    if (args.size() == 1 && ctx.options.autoUpdate) return CmdAutoUpdate(ctx);
    if (args.size() == 1) {
        return InstallKitRelease(ctx, ctx.options.releaseUrl ? *ctx.options.releaseUrl : DefaultReleaseUrl(""), "");
    }
    if (args.size() == 2) {
        if (!IsElementId(args[1])) return Failure(ctx, Tr("invalidId", {{"id", args[1]}}));
        return UpdateElement(ctx, args[1]);
    }
    return Usage(ctx, "update needs an element id, or nothing to update Ampify");
}

}  // namespace ampify::cli
