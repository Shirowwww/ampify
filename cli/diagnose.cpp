// `amk doctor`: one report on the state of the kit, for a person who reports a problem and for the tests.

#include <algorithm>
#include <fstream>
#include <system_error>
#include <vector>

#include "../core/safe_mode.hpp"
#include "../core/version_gate.hpp"
#include "element.hpp"
#include "internal.hpp"
#include "platform.hpp"

namespace amk::cli {

namespace {

constexpr size_t kJournalTail = 3;

// The archive and download tools that `amk install` and `amk update` need.
#ifdef _WIN32
const std::vector<std::string> kArchiveTool = {"tar.exe", "--version"};
const std::vector<std::string> kDownloadTool = {"curl.exe", "--version"};
#else
const std::vector<std::string> kArchiveTool = {"unzip", "-v"};
const std::vector<std::string> kDownloadTool = {"curl", "--version"};
#endif

// The last lines of a log file, without the carriage returns that Windows leaves.
std::vector<std::string> LastLines(const std::filesystem::path& file, size_t count) {
    std::ifstream in(file, std::ios::binary);
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
    }
    size_t begin = lines.size() > count ? lines.size() - count : 0;
    return std::vector<std::string>(lines.begin() + static_cast<std::ptrdiff_t>(begin), lines.end());
}

// Folders of the kit that have a name starting with '.': leftovers of an install or a dev reload that stopped midway.
std::vector<std::filesystem::path> Leftovers(const KitLayout& kit) {
    std::vector<std::filesystem::path> found;
    for (std::string_view kind : {"extension", "theme", "app", "snippet"}) {
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(kit.Folder(kind), ec)) {
            std::string name = Utf8(entry.path().filename());
            if (!name.empty() && name[0] == '.') found.push_back(entry.path());
        }
    }
    return found;
}

}  // namespace

int CmdDoctor(Context& ctx) {
    if (ctx.options.positional.size() != 1) return Usage(ctx, "doctor ne prend pas d'argument");

    int errors = 0;
    int warnings = 0;
    auto ok = [&](const std::string& text) { ctx.out << "[OK]        " << text << "\n"; };
    auto warn = [&](const std::string& text) {
        ctx.out << "[ATTENTION] " << text << "\n";
        warnings++;
    };
    auto fail = [&](const std::string& text) {
        ctx.out << "[ERREUR]    " << text << "\n";
        errors++;
    };

    std::error_code ec;
    if (!std::filesystem::is_directory(ctx.kit.Root(), ec)) {
        warn("dossier du kit absent : " + Utf8(ctx.kit.Root()) + " (amk new, amk install ou amk config le créeront)");
    } else {
        ok("dossier du kit : " + Utf8(ctx.kit.Root()));
    }

    Settings settings;
    std::string error;
    if (!LoadSettings(ctx, settings, error)) {
        fail("réglages illisibles : " + error);
    } else {
        ok("réglages lus (" + std::to_string(settings.Values().size()) + " clé(s))");
    }

    std::vector<InstalledElement> installed = ListInstalled(ctx.kit);
    size_t valid = 0;
    for (const InstalledElement& element : installed) {
        if (element.Valid()) {
            valid++;
            continue;
        }
        std::string reason = element.errors.empty() ? "manifeste illisible" : element.errors.front();
        warn("élément invalide " + element.kind + " " + element.id + " : " + reason);
    }
    ok(std::to_string(valid) + " élément(s) installé(s) et valide(s) sur " + std::to_string(installed.size()));

    for (const std::filesystem::path& leftover : Leftovers(ctx.kit)) {
        warn("reste d'une installation ou d'un développement interrompu : " + Utf8(leftover) + " (peut être supprimé)");
    }

    // active.txt must describe the current settings, or the kernel loads what was there before the last change.
    ActiveReport report = ComputeActive(ctx.kit, settings);
    for (const std::string& problem : report.problems) warn("activé mais non chargé : " + problem);
    std::string written;
    if (!ReadWholeFile(ctx.kit.Active(), written)) {
        warn("active.txt absent : lance amk apply");
    } else if (written != FormatActive(report)) {
        warn("active.txt ne correspond plus aux réglages : lance amk apply");
    } else {
        ok("active.txt à jour");
    }

    amk::SafeMode safeMode(ctx.kit.SafeModeState().string());
    if (safeMode.IsActive()) {
        warn("mode sans échec actif : le noyau est désactivé après deux lancements sans réponse");
    } else {
        ok("mode sans échec : inactif");
    }

    std::vector<std::filesystem::path> journals = JournalFiles(ctx.kit);
    if (journals.empty()) {
        ok("journal : aucun fichier, le noyau n'a pas encore tourné");
    } else {
        ok("journal : " + std::to_string(journals.size()) + " fichier(s)");
        for (const std::filesystem::path& journal : journals) {
            ctx.out << "            " << Utf8(journal.filename()) << "\n";
            for (const std::string& line : LastLines(journal, kJournalTail)) ctx.out << "              " << line << "\n";
        }
    }

    std::string versions;
    for (std::string_view version : kKnownAppVersions) versions += (versions.empty() ? "" : ", ") + std::string(version);
    ok("versions d'Apple Music testées : " + versions);
    ok(std::string("Apple Music ") +
       (platform::IsProcessRunning("AppleMusic.exe") ? "est ouvert" : "est fermé") +
       " (vérification du processus)");

    if (platform::ToolWorks(kArchiveTool)) {
        ok("outil d'archives disponible : " + kArchiveTool.front());
    } else {
        fail("outil d'archives introuvable : " + kArchiveTool.front() + " (installer et mettre à jour ne marchent pas)");
    }
    if (platform::ToolWorks(kDownloadTool)) {
        ok("outil de téléchargement disponible : " + kDownloadTool.front());
    } else {
        fail("outil de téléchargement introuvable : " + kDownloadTool.front() + " (le catalogue distant ne marche pas)");
    }

#ifdef _WIN32
    if (!std::filesystem::is_regular_file(platform::ExecutableDirectory() / "amk_launcher.exe", ec)) {
        warn("amk_launcher.exe absent à côté d'amk : amk launch ne pourra pas démarrer Apple Music avec le noyau");
    } else {
        ok("amk_launcher.exe présent");
    }
#endif

    ctx.out << "\nRésumé : " << errors << " erreur(s), " << warnings << " attention(s).\n";
    return errors > 0 ? 1 : 0;
}

}  // namespace amk::cli
