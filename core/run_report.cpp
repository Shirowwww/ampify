#include "run_report.hpp"

#include <algorithm>
#include <string>
#include <vector>

#include "apple_music.hpp"
#include "kit_layout.hpp"
#include "text_util.hpp"

namespace amk {

namespace {

bool Contains(std::string_view text, std::string_view part) {
    return text.find(part) != std::string_view::npos;
}

// Calls `visit` on each line, without its line break.
template <typename Visit>
void ForEachLine(std::string_view text, Visit visit) {
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string_view::npos) end = text.size();
        std::string_view line = text.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        visit(line);
        start = end + 1;
    }
}

// The number written just before " thread(s) repris" on a line, or nothing.
std::optional<int> ResumedCount(std::string_view line) {
    size_t cut = line.find(" thread(s) repris");
    if (cut == std::string_view::npos) return std::nullopt;

    size_t first = cut;
    while (first > 0 && line[first - 1] >= '0' && line[first - 1] <= '9') first--;
    if (first == cut) return std::nullopt;
    return std::stoi(std::string(line.substr(first, cut - first)));
}

// One line of the checklist. `status` is "OK", "NON" (failed), "?" (to look at) or "info" (for information only).
struct Row {
    std::string status;
    std::string label;
    std::string detail;
};

std::string FormatRow(const Row& row) {
    std::string marker = "[" + row.status + "]";
    marker.resize(std::max<size_t>(marker.size(), 7), ' ');
    return marker + row.label + " : " + row.detail + "\n";
}

}  // namespace

RunFacts ReadRunFacts(std::string_view launcherLog, std::string_view appleMusicLog, std::string_view libraryAgentLog) {
    RunFacts facts;

    ForEachLine(launcherLog, [&](std::string_view line) {
        constexpr std::string_view kPackagePrefix = "Paquet trouvé : ";
        if (line.find(kPackagePrefix) != std::string_view::npos) {
            std::string_view rest = line.substr(line.find(kPackagePrefix) + kPackagePrefix.size());
            facts.packageName = std::string(rest.substr(0, rest.find(' ')));
        }
        if (Contains(line, "Apple Music démarré")) facts.launched = true;
        if (Contains(line, "testée : noyau injecté")) facts.versionTested = true;
        // The M3 launcher holds the first process ("N thread(s) mis en pause"); older builds asked Windows instead.
        if (Contains(line, "thread(s) mis en pause") || Contains(line, "Lancement suspendu demandé")) {
            facts.suspendedLaunch = true;
        }
        if (std::optional<int> resumed = ResumedCount(line)) facts.resumedThreads = resumed;
        // "AppleMusic.exe (4120) : noyau chargé pendant la pause", "... après la reprise", or the older form without the id.
        if (Contains(line, "AppleMusic.exe") && Contains(line, ": noyau chargé")) facts.kernelInAppleMusic = true;
        if (Contains(line, "AMPLibraryAgent.exe (") && Contains(line, ": noyau chargé")) {
            facts.kernelInLibraryAgent = true;
        }
        if (Contains(line, "mode sans échec actif")) facts.safeModeActive = true;
        if (Contains(line, "Apple Music fermé")) facts.closed = true;
    });

    ForEachLine(appleMusicLog, [&](std::string_view line) {
        if (Contains(line, "Noyau chargé dans AppleMusic.exe")) facts.kernelInAppleMusic = true;
        if (Contains(line, "testée") && !Contains(line, "non testée")) facts.versionTested = true;
        if (Contains(line, "Application saine après 20 s")) facts.healthy = true;
        if (Contains(line, "Fenêtre bloquée")) facts.hungWindowWarnings++;
    });

    ForEachLine(libraryAgentLog, [&](std::string_view line) {
        if (Contains(line, "Noyau chargé dans AMPLibraryAgent.exe")) facts.kernelInLibraryAgent = true;
    });

    // A package name that is not Apple Music's is not a run of Apple Music.
    if (AppleMusicVersion(facts.packageName).empty()) facts.packageName.clear();
    return facts;
}

RunVerdict JudgeRun(const RunFacts& facts, const RunEnvironment& env) {
    if (!env.installOk || facts.packageName.empty() || !facts.launched) return RunVerdict::NotRun;

    // A suspended start that resumed no thread means nothing was suspended: the report asks to look at it.
    // A start that was not suspended at all is allowed, and reported as a note.
    bool suspendedOk = !facts.suspendedLaunch || facts.resumedThreads.value_or(0) > 0;
    bool clean = facts.versionTested && facts.kernelInAppleMusic && facts.kernelInLibraryAgent && facts.healthy &&
                 facts.hungWindowWarnings == 0 && !facts.safeModeActive && suspendedOk;
    return clean ? RunVerdict::Passed : RunVerdict::NeedsReview;
}

std::string BuildReport(const RunFacts& facts, const RunEnvironment& env) {
    std::string version = AppleMusicVersion(facts.packageName);
    RunVerdict verdict = JudgeRun(facts, env);

    std::string text;
    text += "Rapport du test Apple Music Mod Kit (M5)\n";
    text += "Date : " + env.date + "\n";
    text += "Windows : " + env.windows + "\n";
    text += "Apple Music : " + (version.empty() ? std::string("non trouvé") : version) + "\n";
    text += "Kit : " + std::string(kKitVersion) + "\n\n";

    switch (verdict) {
        case RunVerdict::Passed:
            text += "RÉSULTAT : RÉUSSI\n";
            break;
        case RunVerdict::NeedsReview:
            text += "RÉSULTAT : À REVOIR (voir les lignes [NON] et [?])\n";
            break;
        case RunVerdict::NotRun:
            text += "RÉSULTAT : NON EXÉCUTÉ (Apple Music n'a pas été lancé par le kit)\n";
            break;
    }
    text += "\nVérifications\n";

    std::vector<Row> rows;
    rows.push_back({env.installOk ? "OK" : "NON", "Kit installé", env.installDetail});
    rows.push_back({version.empty() ? "NON" : "OK", "Apple Music trouvé",
                    version.empty() ? std::string("introuvable") : "version " + version});
    rows.push_back({facts.launched ? "OK" : "NON", "Apple Music lancé par le kit", facts.launched ? "oui" : "non"});
    rows.push_back({facts.versionTested ? "OK" : "NON", "Version testée, noyau autorisé",
                    facts.versionTested ? "oui" : "non, le noyau reste inactif"});

    if (!facts.launched) {
        rows.push_back({"info", "Lancement suspendu", "sans objet, Apple Music n'a pas été lancé"});
    } else if (!facts.suspendedLaunch) {
        rows.push_back({"?", "Lancement suspendu", "non, le noyau est injecté après le démarrage"});
    } else if (facts.resumedThreads.value_or(0) > 0) {
        rows.push_back({"OK", "Lancement suspendu", std::to_string(*facts.resumedThreads) + " thread(s) repris"});
    } else {
        rows.push_back({"?", "Lancement suspendu", "0 thread repris : rien n'était suspendu"});
    }

    rows.push_back({facts.kernelInAppleMusic ? "OK" : "NON", "Noyau dans AppleMusic.exe",
                    facts.kernelInAppleMusic ? "chargé" : "non vu"});
    rows.push_back({facts.kernelInLibraryAgent ? "OK" : "NON", "Noyau dans AMPLibraryAgent.exe",
                    facts.kernelInLibraryAgent ? "chargé" : "non vu (lancez un titre pour le faire démarrer)"});
    rows.push_back({facts.healthy ? "OK" : "NON", "Application saine après 20 s", facts.healthy ? "oui" : "non"});
    rows.push_back({facts.hungWindowWarnings == 0 ? "OK" : "?", "Fenêtre bloquée",
                    facts.hungWindowWarnings == 0 ? "non" : std::to_string(facts.hungWindowWarnings) + " fois"});
    rows.push_back({facts.safeModeActive ? "?" : "OK", "Mode sans échec",
                    facts.safeModeActive ? "actif : le noyau a été désactivé" : "inactif"});
    rows.push_back({"info", "Trace ETW", env.traceStatus});
    rows.push_back({"info", "Événements Windows", env.eventsStatus});

    for (const Row& row : rows) text += FormatRow(row);

    text += "\nLes chemins de votre PC sont anonymes : votre nom d'utilisateur est remplacé par « utilisateur ».\n";
    text += "Le fichier trace.etl, s'il existe, n'est pas anonymisé.\n";
    return text;
}

std::string HideUserName(std::string_view text, std::string_view userName) {
    std::string lowered = ToLowerAscii(text);
    std::string name = ToLowerAscii(userName);
    if (name.empty()) return std::string(text);

    auto isSeparator = [](char c) { return c == '\\' || c == '/'; };
    std::string result;
    size_t copied = 0;
    // A path is "<separator>users<separator><name>", then a separator, a line break, a quote, a space or the end.
    for (size_t i = 0; i + 7 + name.size() <= lowered.size(); i++) {
        if (!isSeparator(lowered[i]) || lowered.compare(i + 1, 5, "users") != 0 || !isSeparator(lowered[i + 6])) continue;

        size_t start = i + 7;
        if (lowered.compare(start, name.size(), name) != 0) continue;

        size_t end = start + name.size();
        bool ends = end == lowered.size() || isSeparator(lowered[end]) || lowered[end] == '\r' ||
                    lowered[end] == '\n' || lowered[end] == '"' || lowered[end] == ' ';
        if (!ends) continue;

        result += std::string(text.substr(copied, start - copied)) + "utilisateur";
        copied = end;
        i = end - 1;
    }
    result += std::string(text.substr(copied));
    return result;
}

}  // namespace amk
