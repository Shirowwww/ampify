// Catalogue, installs, updates and removals of elements, and updates of the kit itself.
// Every download goes through the same checks: the address policy, the SHA-256 pin, the archive listing,
// the link check and the manifest check. Nothing from the network is run or installed before all of them pass.

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>

#include "../core/settings.hpp"
#include "archive.hpp"
#include "backup.hpp"
#include "catalog.hpp"
#include "element.hpp"
#include "internal.hpp"
#include "kit.hpp"
#include "platform.hpp"
#include "release.hpp"
#include "sha256.hpp"

#ifndef AMK_VERSION
#define AMK_VERSION "0.0.0-dev"
#endif

namespace amk::cli {

namespace {

constexpr const char* kDefaultCatalog =
    "https://raw.githubusercontent.com/Shirowwww/apple-music-mod-kit/main/catalog/catalog.json";
constexpr const char* kDefaultRelease =
    "https://api.github.com/repos/Shirowwww/apple-music-mod-kit/releases/latest";
constexpr const char* kKitArchiveSuffix = "-windows-x64.zip";
constexpr const char* kChecksums = "SHA256SUMS.txt";
constexpr const char* kRequiredKitFiles[] = {"amk.exe", "amk_launcher.exe", "amk_kernel.dll"};

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

// Downloads an https address with curl, or copies a file:// address. The caller has checked the address policy.
bool FetchTo(const std::string& url, const std::filesystem::path& destination, std::string& error) {
    if (StartsWith(url, "file://")) {
        std::error_code ec;
        std::filesystem::copy_file(PathArgument(LocalPathOf(url)), destination,
                                   std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            error = "copie impossible : " + LocalPathOf(url);
            return false;
        }
        return true;
    }
    return platform::DownloadHttps(url, destination, error);
}

void RemoveQuietly(const std::filesystem::path& path) {
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
}

// Reads the catalogue given by --catalog, by catalog.url in the settings, or by default. A remote catalogue that
// cannot be downloaded falls back to the last copy that worked.
bool LoadCatalog(Context& ctx, Catalog& catalog, bool& local) {
    Settings settings;
    settings.Load(ctx.kit.Settings().string());
    std::string source = ctx.options.catalog ? *ctx.options.catalog : settings.GetString("catalog.url", kDefaultCatalog);

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
                ctx.err << "amk : catalogue inaccessible : " << error << "\n";
                return false;
            }
            ctx.err << "amk : catalogue distant inaccessible, copie locale utilisée\n";
        } else {
            RemoveQuietly(part);
            std::filesystem::create_directories(ctx.kit.Cache(), ec);
            std::string writeError;
            WriteTextFile(cached, text, writeError);
        }
    } else {
        local = true;
        std::string path = StartsWith(source, "file://") ? LocalPathOf(source) : source;
        if (!ReadWholeFile(PathArgument(path), text)) {
            ctx.err << "amk : catalogue introuvable : " << path << "\n";
            return false;
        }
    }

    std::optional<Catalog> parsed = ParseCatalog(text, local, errors);
    if (!parsed) {
        ctx.err << "amk : catalogue invalide\n";
        for (const std::string& problem : errors) ctx.err << "  - " << problem << "\n";
        return false;
    }
    catalog = std::move(*parsed);
    return true;
}

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
    if (!IsAllowedDownloadUrl(entry.url, localCatalog)) return Failure(ctx, "adresse refusée : " + entry.url);

    ctx.out << entry.name << " (" << entry.id << ") " << entry.version << ", par " << entry.author << "\n"
            << entry.description << "\n";
    std::string permissions = JoinPermissions(entry.permissions);
    ctx.out << (permissions.empty() ? std::string("Aucune permission demandée.")
                                    : "Permissions demandées : " + permissions)
            << "\n";

    if (!ctx.options.yes) {
        ctx.out << (isUpdate ? "Mettre à jour " : "Installer ") << entry.id << " ? [o/N] " << std::flush;
        std::string answer;
        std::getline(ctx.in, answer);
        if (!IsYes(answer)) {
            ctx.out << "Annulé.\n";
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
    ctx.out << "Téléchargement...\n" << std::flush;
    if (!FetchTo(entry.url, archive, error)) {
        RemoveQuietly(archive);
        return Failure(ctx, error);
    }

    std::optional<std::string> digest = Sha256File(archive);
    if (!digest || *digest != entry.sha256) {
        RemoveQuietly(archive);
        return Failure(ctx, "empreinte SHA-256 différente de celle du catalogue : archive refusée");
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
        return Failure(ctx, "lien symbolique interdit dans l'archive");
    }
    FolderValidation validation = ValidateElementFolder(extracted);
    if (!validation.ok()) {
        RemoveQuietly(staging);
        ctx.err << "amk : l'archive ne contient pas un élément valide :\n";
        for (const std::string& message : validation.errors) ctx.err << "  - " << message << "\n";
        return 1;
    }
    const ElementManifest& manifest = *validation.manifest;
    if (manifest.id != entry.id || manifest.version != entry.version || manifest.kind != entry.kind) {
        RemoveQuietly(staging);
        return Failure(ctx, "le manifeste de l'archive ne correspond pas au catalogue");
    }

    // Swap the folders: the previous version is kept until the new one is in place.
    std::filesystem::path destination = ctx.kit.Element(entry.kind, entry.id);
    std::filesystem::path previous = ctx.kit.Folder(entry.kind) / ("." + entry.id + ".previous");
    std::filesystem::create_directories(ctx.kit.Folder(entry.kind), ec);
    RemoveQuietly(previous);
    if (std::filesystem::exists(destination, ec)) {
        std::filesystem::rename(destination, previous, ec);
        if (ec) {
            RemoveQuietly(staging);
            return Failure(ctx, "remplacement impossible : " + Utf8(destination));
        }
    }
    std::filesystem::rename(extracted, destination, ec);
    if (ec) {
        if (std::filesystem::exists(previous, ec)) std::filesystem::rename(previous, destination, ec);
        RemoveQuietly(staging);
        return Failure(ctx, "installation impossible : " + Utf8(destination));
    }
    RemoveQuietly(staging);
    RemoveQuietly(previous);

    ctx.out << entry.id << " " << entry.version << (isUpdate ? " mis à jour" : " installé") << ". ";
    if (entry.kind == "theme") {
        ctx.out << "Choisis-le avec amk theme set " << entry.id << ".\n";
    } else {
        ctx.out << "Il est désactivé : amk " << entry.kind << " enable " << entry.id << " pour l'activer.\n";
    }
    return 0;
}

int UpdateElement(Context& ctx, const std::string& id) {
    std::vector<InstalledElement> installed = InstalledNamed(ctx.kit, id);
    if (installed.empty()) return Failure(ctx, "élément non installé : " + id + ". Utilise amk install " + id);
    if (installed.size() > 1) return Failure(ctx, "plusieurs éléments portent l'identifiant " + id);

    Catalog catalog;
    bool local = false;
    if (!LoadCatalog(ctx, catalog, local)) return 1;
    const CatalogEntry* entry = FindCatalogEntry(catalog, id);
    if (entry == nullptr) return Failure(ctx, "élément absent du catalogue : " + id);
    if (entry->kind != installed.front().kind) {
        return Failure(ctx, "le catalogue range " + id + " comme " + entry->kind + ", pas comme " + installed.front().kind);
    }

    std::string installedVersion = installed.front().manifest ? installed.front().manifest->version : "0.0.0";
    if (CompareReleaseVersions(entry->version, installedVersion) <= 0) {
        ctx.out << id << " est déjà à jour (version " << installedVersion << ").\n";
        return 0;
    }
    return InstallEntry(ctx, *entry, local, true);
}

int UpdateKit(Context& ctx) {
    std::string source = ctx.options.releaseUrl ? *ctx.options.releaseUrl : kDefaultRelease;
    bool releaseLocal = StartsWith(source, "file://");
    if (!releaseLocal && !StartsWith(source, "https://")) return Failure(ctx, "adresse de release refusée : " + source);

    std::error_code ec;
    std::filesystem::create_directories(ctx.kit.Updates(), ec);
    std::string error;
    std::filesystem::path releaseFile = ctx.kit.Downloads() / "release.json";
    std::filesystem::create_directories(ctx.kit.Downloads(), ec);
    std::string text;
    if (!FetchTo(source, releaseFile, error) || !ReadWholeFile(releaseFile, text)) {
        RemoveQuietly(releaseFile);
        return Failure(ctx, error.empty() ? "release illisible" : error);
    }
    RemoveQuietly(releaseFile);

    std::optional<ReleaseInfo> release = ParseRelease(text, error);
    if (!release) return Failure(ctx, error);
    if (!IsValidVersion(release->version)) return Failure(ctx, "version de release invalide : " + release->tag);

    if (CompareReleaseVersions(release->version, AMK_VERSION) <= 0) {
        ctx.out << "amk est à jour (version " << AMK_VERSION << ").\n";
        return 0;
    }
    if (ctx.options.check) {
        ctx.out << "Version " << release->version << " disponible (installée : " << AMK_VERSION
                << "). Lance amk update pour la télécharger.\n";
        return 0;
    }

    std::string archiveName = "amk-kit-" + release->version + kKitArchiveSuffix;
    auto archiveAddress = release->assets.find(archiveName);
    auto sumsAddress = release->assets.find(kChecksums);
    if (archiveAddress == release->assets.end() || sumsAddress == release->assets.end()) {
        return Failure(ctx, "la release " + release->tag + " ne contient pas " + archiveName + " et " + kChecksums);
    }
    if (!IsAllowedDownloadUrl(archiveAddress->second, releaseLocal) ||
        !IsAllowedDownloadUrl(sumsAddress->second, releaseLocal)) {
        return Failure(ctx, "adresse refusée dans la release");
    }

    std::filesystem::path archive = ctx.kit.Updates() / archiveName;
    std::filesystem::path sumsFile = ctx.kit.Updates() / kChecksums;
    ctx.out << "Téléchargement de la version " << release->version << "...\n" << std::flush;
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
        return Failure(ctx, "empreinte SHA-256 de l'archive de mise à jour incorrecte : archive refusée");
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
        return Failure(ctx, "lien symbolique interdit dans l'archive de mise à jour");
    }
    for (const char* required : kRequiredKitFiles) {
        if (!std::filesystem::is_regular_file(staged / required, ec)) {
            RemoveQuietly(staged);
            return Failure(ctx, std::string("archive incomplète, fichier manquant : ") + required);
        }
    }

    if (!ctx.options.apply) {
        ctx.out << "Version " << release->version << " téléchargée et vérifiée dans " << Utf8(staged)
                << ".\nFerme Apple Music, puis lance : amk update --apply\n";
        return 0;
    }

    if (platform::IsProcessRunning("AppleMusic.exe") || platform::IsProcessRunning("AMPLibraryAgent.exe")) {
        return Failure(ctx, "ferme Apple Music avant d'installer la mise à jour");
    }
    std::filesystem::path installDir = ctx.options.installDir ? PathArgument(*ctx.options.installDir)
                                                              : platform::ExecutableDirectory();
    for (const auto& file : std::filesystem::directory_iterator(staged, ec)) {
        if (!file.is_regular_file(ec)) continue;
        std::filesystem::path destination = installDir / file.path().filename();
        if (std::filesystem::exists(destination, ec)) {
            // A running program can be renamed on Windows, not overwritten. Keep the old file as *.old.
            std::filesystem::path old = destination;
            old += ".old";
            RemoveQuietly(old);
            std::filesystem::rename(destination, old, ec);
            if (ec) return Failure(ctx, "impossible de remplacer " + Utf8(destination));
        }
        std::filesystem::copy_file(file.path(), destination, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) return Failure(ctx, "copie impossible vers " + Utf8(destination));
    }
    RemoveQuietly(staged);
    ctx.out << "amk " << release->version << " installé dans " << Utf8(installDir)
            << ". Relance Apple Music avec amk launch.\n";
    return 0;
}

}  // namespace

int CmdMarket(Context& ctx) {
    Catalog catalog;
    bool local = false;
    if (!LoadCatalog(ctx, catalog, local)) return 1;
    if (catalog.entries.empty()) {
        ctx.out << "Le catalogue est vide pour l'instant.\n";
        return 0;
    }

    std::vector<InstalledElement> installed = ListInstalled(ctx.kit);
    ctx.out << Pad("IDENTIFIANT", 24) << Pad("VERSION", 10) << Pad("TYPE", 10) << Pad("AUTEUR", 18) << "ÉTAT\n";
    for (const CatalogEntry& entry : catalog.entries) {
        std::string state = "non installé";
        for (const InstalledElement& element : installed) {
            if (element.kind == entry.kind && element.id == entry.id) {
                state = "installé " + (element.manifest ? element.manifest->version : std::string("?"));
            }
        }
        ctx.out << Pad(entry.id, 24) << Pad(entry.version, 10) << Pad(entry.kind, 10) << Pad(entry.author, 18)
                << state << " : " << entry.description << "\n";
    }
    return 0;
}

int CmdInstall(Context& ctx) {
    if (ctx.options.positional.size() != 2) return Usage(ctx, "install attend un identifiant d'élément");
    const std::string& id = ctx.options.positional[1];
    if (!IsValidId(id)) return Failure(ctx, "identifiant invalide : " + id);

    Catalog catalog;
    bool local = false;
    if (!LoadCatalog(ctx, catalog, local)) return 1;
    const CatalogEntry* entry = FindCatalogEntry(catalog, id);
    if (entry == nullptr) return Failure(ctx, "élément absent du catalogue : " + id);
    return InstallEntry(ctx, *entry, local, false);
}

int CmdRemove(Context& ctx) {
    if (ctx.options.positional.size() != 2) return Usage(ctx, "remove attend un identifiant d'élément");
    const std::string& id = ctx.options.positional[1];
    if (!IsValidId(id)) return Failure(ctx, "identifiant invalide : " + id);

    std::vector<InstalledElement> matches = InstalledNamed(ctx.kit, id);
    if (matches.empty()) {
        if (std::find(std::begin(kBuiltinExtensions), std::end(kBuiltinExtensions), std::string_view(id)) !=
            std::end(kBuiltinExtensions)) {
            return Failure(ctx, id + " est intégré au noyau et ne se retire pas, il se désactive");
        }
        return Failure(ctx, "élément introuvable : " + id);
    }
    if (matches.size() > 1) return Failure(ctx, "plusieurs éléments portent l'identifiant " + id);
    const std::string kind = matches.front().kind;

    std::string error;
    std::optional<std::string> snapshot = CreateBackup(ctx.kit, error);
    if (!snapshot) return Failure(ctx, error);

    RemoveQuietly(ctx.kit.Element(kind, id));
    if (std::filesystem::exists(ctx.kit.Element(kind, id))) return Failure(ctx, "suppression impossible : " + id);

    Settings settings;
    if (!LoadSettings(ctx, settings, error)) return Failure(ctx, error);
    if (kind == "theme") {
        if (settings.GetString("theme.active", "") == id) settings.Set("theme.active", "");
    } else {
        settings.Remove(EnabledKey(kind, id));
    }
    if (!SaveSettings(ctx, settings, error)) return Failure(ctx, error);

    ctx.out << id << " retiré. Sauvegarde faite avant : " << *snapshot << " (amk restore --backup " << *snapshot
            << " pour revenir en arrière).\n";
    return 0;
}

int CmdUpdate(Context& ctx) {
    const std::vector<std::string>& args = ctx.options.positional;
    if (args.size() == 1) return UpdateKit(ctx);
    if (args.size() == 2) {
        if (!IsValidId(args[1])) return Failure(ctx, "identifiant invalide : " + args[1]);
        return UpdateElement(ctx, args[1]);
    }
    return Usage(ctx, "update attend un identifiant d'élément, ou rien pour mettre le kit à jour");
}

}  // namespace amk::cli
