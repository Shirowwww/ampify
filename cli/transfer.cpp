// Profiles of settings, and export and import of the kit. The files of the app itself are never part of them.

#include <algorithm>
#include <optional>
#include <system_error>

#include "archive.hpp"
#include "backup.hpp"
#include "element.hpp"
#include "internal.hpp"
#include "platform.hpp"
#include "zip_writer.hpp"

namespace amk::cli {

namespace {

constexpr std::string_view kKinds[] = {"extension", "theme", "app", "snippet"};

// The first entry of every export. Import refuses an archive without it, so a random zip is never unpacked.
constexpr std::string_view kExportMarker = "AMK-EXPORT";
constexpr std::string_view kExportFormat = "format 1\n";

// Names an import accepts: the marker, settings.txt, and the four element folders. Anything else is refused.
bool IsExportEntry(const std::string& entry) {
    if (entry == kExportMarker || entry == "settings.txt") return true;
    for (std::string_view kind : kKinds) {
        std::string prefix = std::string(FolderOfKind(kind)) + "/";
        if (entry.compare(0, prefix.size(), prefix) == 0) return true;
    }
    return false;
}

// Profile names use the same rules as element ids: they become folder names.
bool CheckProfileName(Context& ctx, const std::string& name) {
    if (IsValidId(name)) return true;
    ctx.err << "amk : nom de profil invalide : " << name << " (minuscules, chiffres et tirets, 2 à 40 caractères)\n";
    return false;
}

int ProfileList(Context& ctx) {
    std::vector<std::string> names;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(ctx.kit.Profiles(), ec)) {
        if (!entry.is_directory(ec)) continue;
        if (!std::filesystem::is_regular_file(entry.path() / "settings.txt", ec)) continue;
        names.push_back(Utf8(entry.path().filename()));
    }
    std::sort(names.begin(), names.end());
    if (names.empty()) {
        ctx.out << "Aucun profil. Crée-en un avec amk profile save <nom>.\n";
        return 0;
    }
    for (const std::string& name : names) ctx.out << name << "\n";
    return 0;
}

int ProfileSave(Context& ctx, const std::string& name) {
    std::string text;
    if (!ReadWholeFile(ctx.kit.Settings(), text)) text.clear();  // no settings yet: the profile starts empty
    std::string error;
    if (!WriteTextFile(ctx.kit.Profiles() / name / "settings.txt", text, error)) return Failure(ctx, error);
    ctx.out << "Profil " << name << " enregistré. Il ne contient que les réglages, pas les éléments.\n";
    return 0;
}

int ProfileUse(Context& ctx, const std::string& name) {
    std::string text;
    if (!ReadWholeFile(ctx.kit.Profiles() / name / "settings.txt", text)) return Failure(ctx, "profil introuvable : " + name);

    // The settings being replaced are saved first, so the switch can be undone with amk restore --backup.
    std::string error;
    std::optional<std::string> snapshot = CreateBackup(ctx.kit, error);
    if (!snapshot) return Failure(ctx, error);
    if (!WriteTextFile(ctx.kit.Settings(), text, error)) return Failure(ctx, error);
    ctx.out << "Profil " << name << " chargé. État précédent sauvegardé : " << *snapshot << ". Lance amk apply.\n";
    return 0;
}

int ProfileDelete(Context& ctx, const std::string& name) {
    std::error_code ec;
    std::filesystem::path folder = ctx.kit.Profiles() / name;
    if (!std::filesystem::is_directory(folder, ec)) return Failure(ctx, "profil introuvable : " + name);
    std::filesystem::remove_all(folder, ec);
    if (ec) return Failure(ctx, "suppression impossible : " + name);
    ctx.out << "Profil " << name << " supprimé. Les réglages actuels ne sont pas modifiés.\n";
    return 0;
}

}  // namespace

int CmdProfile(Context& ctx) {
    const std::vector<std::string>& args = ctx.options.positional;
    if (args.size() < 2) return Usage(ctx, "profile attend list, save, use ou delete");
    const std::string& action = args[1];

    if (action == "list") {
        if (args.size() != 2) return Usage(ctx, "profile list ne prend pas d'argument");
        return ProfileList(ctx);
    }
    if (action != "save" && action != "use" && action != "delete") {
        return Usage(ctx, "action inconnue pour profile : " + action);
    }
    if (args.size() != 3) return Usage(ctx, "profile " + action + " attend un nom de profil");
    if (!CheckProfileName(ctx, args[2])) return 1;

    if (action == "save") return ProfileSave(ctx, args[2]);
    if (action == "use") return ProfileUse(ctx, args[2]);
    return ProfileDelete(ctx, args[2]);
}

int CmdExport(Context& ctx) {
    const std::vector<std::string>& args = ctx.options.positional;
    if (args.size() != 2) return Usage(ctx, "export attend le fichier .zip à écrire");

    std::filesystem::path archive = PathArgument(args[1]);
    std::error_code ec;
    if (std::filesystem::exists(archive, ec)) return Failure(ctx, "le fichier existe déjà : " + Utf8(archive));

    std::vector<ZipSource> files;
    files.push_back({std::string(kExportMarker), {}, std::string(kExportFormat)});
    if (std::filesystem::is_regular_file(ctx.kit.Settings(), ec)) files.push_back({"settings.txt", ctx.kit.Settings(), {}});
    for (std::string_view kind : kKinds) {
        std::vector<ZipSource> part = FilesUnder(ctx.kit.Folder(kind), std::string(FolderOfKind(kind)));
        files.insert(files.end(), part.begin(), part.end());
    }

    std::string error;
    if (!WriteZip(archive, files, error)) return Failure(ctx, error);
    ctx.out << "Export écrit : " << Utf8(archive) << " (" << files.size() << " fichier(s)). "
            << "Les fichiers de l'app ne sont pas inclus.\n";
    return 0;
}

int CmdImport(Context& ctx) {
    const std::vector<std::string>& args = ctx.options.positional;
    if (args.size() != 2) return Usage(ctx, "import attend le fichier .zip d'un export");

    std::filesystem::path archive = PathArgument(args[1]);
    std::error_code ec;
    if (!std::filesystem::is_regular_file(archive, ec)) return Failure(ctx, "fichier introuvable : " + args[1]);

    std::vector<std::string> entries;
    std::string error;
    if (!platform::ListZip(archive, entries, error)) return Failure(ctx, error);
    std::string refusal = CheckArchiveListing(entries);
    if (!refusal.empty()) return Failure(ctx, refusal);
    if (std::find(entries.begin(), entries.end(), std::string(kExportMarker)) == entries.end()) {
        return Failure(ctx, "ce fichier n'est pas un export d'amk");
    }
    for (const std::string& entry : entries) {
        if (!IsExportEntry(entry)) return Failure(ctx, "entrée non prévue dans un export : " + entry);
    }

    // The archive is unpacked into a scratch folder and checked there. The kit is not touched until everything passes.
    std::filesystem::path staging = platform::TemporaryFile("amk-import");
    std::filesystem::create_directories(staging, ec);
    if (!platform::ExtractZip(archive, staging, error)) {
        std::filesystem::remove_all(staging, ec);
        return Failure(ctx, error);
    }

    struct Planned {
        std::string kind;
        std::string id;
        std::string version;
        std::filesystem::path source;
    };
    std::vector<Planned> planned;
    std::vector<std::string> problems;
    for (std::string_view kind : kKinds) {
        std::filesystem::path folder = staging / FolderOfKind(kind);
        if (!std::filesystem::is_directory(folder, ec)) continue;
        for (const auto& entry : std::filesystem::directory_iterator(folder, ec)) {
            if (!entry.is_directory(ec)) continue;
            std::string name = Utf8(entry.path().filename());
            if (name.empty() || name[0] == '.') continue;

            FolderValidation check = ValidateElementFolder(entry.path());
            if (!check.ok() || !check.errors.empty() || !check.manifest) {
                for (const std::string& problem : check.errors) problems.push_back(name + " : " + problem);
                continue;
            }
            if (check.manifest->kind != kind) {
                problems.push_back(name + " : le type ne correspond pas au dossier " + std::string(FolderOfKind(kind)));
                continue;
            }
            planned.push_back({std::string(kind), check.manifest->id, check.manifest->version, entry.path()});
        }
    }

    const bool hasSettings = std::filesystem::is_regular_file(staging / "settings.txt", ec);
    if (hasSettings) {
        Settings probe;
        if (!probe.Load((staging / "settings.txt").string())) problems.push_back("settings.txt illisible");
    }
    if (!problems.empty()) {
        std::filesystem::remove_all(staging, ec);
        ctx.err << "Import refusé, rien n'a été écrit :\n";
        for (const std::string& problem : problems) ctx.err << "  - " << problem << "\n";
        return 1;
    }

    ctx.out << "Contenu de l'export :\n";
    for (const Planned& item : planned) ctx.out << "  " << item.kind << " " << item.id << " " << item.version << "\n";
    if (hasSettings) ctx.out << "  réglages (remplacent settings.txt)\n";
    if (planned.empty() && !hasSettings) ctx.out << "  rien\n";

    if (ctx.options.dryRun) {
        std::filesystem::remove_all(staging, ec);
        ctx.out << "Simulation : rien n'a été écrit.\n";
        return 0;
    }
    if (!ctx.options.yes) {
        ctx.out << "Remplacer ces éléments et ces réglages ? (oui/non) " << std::flush;
        std::string answer;
        std::getline(ctx.in, answer);
        if (!IsYes(answer)) {
            std::filesystem::remove_all(staging, ec);
            ctx.out << "Import annulé.\n";
            return 0;
        }
    }

    std::optional<std::string> snapshot = CreateBackup(ctx.kit, error);
    if (!snapshot) {
        std::filesystem::remove_all(staging, ec);
        return Failure(ctx, error);
    }
    for (const Planned& item : planned) {
        std::filesystem::remove_all(ctx.kit.Element(item.kind, item.id), ec);
        std::filesystem::create_directories(ctx.kit.Folder(item.kind), ec);
        if (!CopyElementFolder(item.source, ctx.kit.Element(item.kind, item.id), error)) {
            std::filesystem::remove_all(staging, ec);
            return Failure(ctx, error + " (état précédent sauvegardé : " + *snapshot + ")");
        }
    }
    if (hasSettings) {
        std::string text;
        if (!ReadWholeFile(staging / "settings.txt", text) || !WriteTextFile(ctx.kit.Settings(), text, error)) {
            std::filesystem::remove_all(staging, ec);
            return Failure(ctx, "réglages non importés (état précédent sauvegardé : " + *snapshot + ")");
        }
    }
    std::filesystem::remove_all(staging, ec);

    ctx.out << "Import terminé. État précédent sauvegardé : " << *snapshot << ". Lance amk apply pour l'appliquer.\n";
    return 0;
}

}  // namespace amk::cli
