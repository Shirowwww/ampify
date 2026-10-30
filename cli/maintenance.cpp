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

#ifndef AMK_VERSION
#define AMK_VERSION "0.0.0-dev"
#endif

namespace amk::cli {

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

std::optional<std::filesystem::path> PathSetting(const Context& ctx, const std::string& key,
                                                 std::optional<std::filesystem::path> fallback) {
    Settings settings;
    settings.Load(ctx.kit.Settings().string());
    std::string value = settings.GetString(key, "");
    if (!value.empty()) return PathArgument(value);
    return fallback;
}

int CmdDevtools(Context& ctx) {
    const std::vector<std::string>& args = ctx.options.positional;
    if (args.size() > 2) return Usage(ctx, "devtools prend on, off, ou rien (pour on)");
    std::string action = args.size() == 2 ? args[1] : "on";
    if (action != "on" && action != "off") return Usage(ctx, "devtools prend on ou off, pas " + action);

    Settings settings;
    std::string error;
    if (!LoadSettings(ctx, settings, error)) return Failure(ctx, error);
    settings.SetBool("theme.inspect", action == "on");
    if (!SaveSettings(ctx, settings, error)) return Failure(ctx, error);

    if (action == "off") {
        ctx.out << "Inspecteur désactivé (theme.inspect = 0).\n";
        return 0;
    }
    ctx.out << "Inspecteur activé (theme.inspect = 1).\n"
            << "Au prochain passage du noyau dans Apple Music (lancé avec amk launch), l'arbre de l'interface sera écrit\n"
            << "dans " << Utf8(InspectorFile(ctx.kit)) << ", puis le noyau remettra theme.inspect à 0.\n"
            << "Affiche-le avec : amk inspect --tree\n"
            << "Il faut un noyau avec le moteur de thèmes XAML (M8). Sans lui, aucun fichier n'est écrit.\n";
    return 0;
}

int CmdInspectTree(Context& ctx) {
    if (ctx.options.positional.size() != 1) return Usage(ctx, "inspect --tree ne prend pas d'argument");
    std::filesystem::path file = InspectorFile(ctx.kit);
    std::string text;
    if (!ReadWholeFile(file, text)) {
        return Failure(ctx, "aucun arbre d'interface pour l'instant (" + Utf8(file) +
                                " absent). Lance amk devtools, puis Apple Music avec amk launch.");
    }
    ctx.out << "Arbre de l'interface écrit par le noyau (" << Utf8(file) << ") :\n" << text;
    if (!text.empty() && text.back() != '\n') ctx.out << "\n";
    return 0;
}

int CmdConfigDir(Context& ctx) {
    if (ctx.options.positional.size() != 1) return Usage(ctx, "config --dir ne prend pas d'argument");
    std::error_code ec;
    std::filesystem::create_directories(ctx.kit.Root(), ec);
    ctx.out << Utf8(ctx.kit.Root()) << "\n";
    std::vector<std::string> command = platform::OpenFolderCommand(ctx.kit.Root());
    if (ctx.options.dryRun) {
        ctx.out << "Commande :";
        for (const std::string& part : command) ctx.out << " " << part;
        ctx.out << "\n";
        return 0;
    }
    if (!platform::StartDetached(command)) return Failure(ctx, "ouverture du dossier impossible");
    return 0;
}

int CmdVersions(Context& ctx) {
    const std::vector<std::string>& args = ctx.options.positional;
    std::string action = args.size() > 1 ? args[1] : "list";
    if (args.size() > 2) return Usage(ctx, "versions prend list ou update");
    std::filesystem::path listFile = ctx.kit.Root() / std::string(kVersionListFile);

    if (action == "list") {
        std::vector<std::string> extra = LoadVersionList(listFile);
        ctx.out << "Versions d'Apple Music acceptées par le noyau :\n";
        for (std::string_view version : kKnownAppVersions) {
            ctx.out << "  " << Pad(version, 20) << "intégrée au noyau\n";
        }
        for (const std::string& version : extra) {
            if (IsKnownAppVersion(version)) continue;
            ctx.out << "  " << Pad(version, 20) << "versions.txt (amk versions update)\n";
        }
        std::error_code ec;
        if (std::filesystem::exists(listFile, ec) && extra.empty()) {
            ctx.out << "Attention : " << Utf8(listFile) << " est illisible ou vide, il est ignoré.\n";
        }

        ctx.out << "À chaque lancement, " << kPlayerDllName << " doit aussi exporter :";
        for (std::string_view name : kRequiredPlayerExports) ctx.out << " " << name;
        ctx.out << "\n";

        std::optional<std::filesystem::path> folder = platform::AppPackageFolder();
        if (!folder) {
            ctx.out << "Apple Music installé : introuvable sur ce système.\n";
            return 0;
        }
        std::string installed = AppleMusicVersion(Utf8(folder->filename()));
        if (installed.empty()) {
            ctx.out << "Apple Music installé : version illisible (" << Utf8(*folder) << ").\n";
            return 0;
        }
        bool known = IsKnownAppVersion(installed, KnownAppVersionsWith(extra));
        ctx.out << "Apple Music installé : " << installed
                << (known ? " (testée)\n" : " (non testée : le noyau reste inactif. Essaie amk versions update)\n");
        return 0;
    }

    if (action != "update") return Usage(ctx, "action inconnue pour versions : " + action);

    Catalog catalog;
    bool local = false;
    if (!LoadCatalog(ctx, catalog, local)) return 1;
    if (catalog.versionsUrl.empty()) return Failure(ctx, "le catalogue ne publie pas de liste de versions");
    if (!IsAllowedDownloadUrl(catalog.versionsUrl, local)) return Failure(ctx, "adresse refusée : " + catalog.versionsUrl);

    std::error_code ec;
    std::filesystem::create_directories(ctx.kit.Downloads(), ec);
    std::filesystem::path part = ctx.kit.Downloads() / "versions.json.part";
    std::string error;
    ctx.out << "Téléchargement de la liste des versions testées...\n" << std::flush;
    std::string text;
    bool fetched = FetchTo(catalog.versionsUrl, part, error) && ReadWholeFile(part, text);
    std::optional<std::string> digest = fetched ? Sha256File(part) : std::nullopt;
    std::filesystem::remove(part, ec);
    if (!fetched) return Failure(ctx, error.empty() ? "liste des versions illisible" : error);
    if (!digest || *digest != catalog.versionsSha256) {
        return Failure(ctx, "empreinte SHA-256 différente de celle du catalogue : liste refusée");
    }

    std::vector<std::string> problems;
    std::optional<VersionsCatalog> versions = ParseVersionsCatalog(text, problems);
    if (!versions) {
        ctx.err << "amk : liste des versions invalide\n";
        for (const std::string& problem : problems) ctx.err << "  - " << problem << "\n";
        return 1;
    }
    std::vector<std::string> rejected;
    std::vector<std::string> usable = UsableVersions(*versions, kRequiredPlayerExports, rejected);
    for (const std::string& version : rejected) ctx.out << "  ignorée    " << version << "\n";

    if (!WriteTextFile(listFile,
                       FormatVersionList(usable, "Écrit par amk versions update. Relance cette commande pour le mettre à jour."),
                       error)) {
        return Failure(ctx, error);
    }
    for (const std::string& version : usable) ctx.out << "  testée     " << version << "\n";
    ctx.out << usable.size() << " version(s) écrite(s) dans " << Utf8(listFile)
            << ". Le noyau les accepte au prochain lancement d'Apple Music (amk launch), si "
            << kPlayerDllName << " a toujours les exports attendus.\n";
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
    if (!IsValidVersion(cache.latest) || CompareReleaseVersions(cache.latest, AMK_VERSION) <= 0) return;
    ctx.err << "amk : la version " << cache.latest << " du kit est disponible (installée : " << AMK_VERSION
            << "). Lance amk update pour la télécharger.\n";
}

}  // namespace amk::cli
