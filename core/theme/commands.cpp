#include "commands.hpp"

#include "files.hpp"
#include "library.hpp"
#include "theme_settings.hpp"
#include "../settings.hpp"

namespace amk::theme {

namespace {

constexpr int kOk = 0;
constexpr int kRefused = 1;
constexpr int kWrongUse = 2;

// The options a command accepts: `--scheme nom`, `--scheme=nom`, `--polls N`. Positional arguments come back in order.
struct Parsed {
    std::vector<std::string> positional;
    std::string scheme;
    int polls = 0;
    bool ok = true;
    std::string problem;
};

Parsed ParseArguments(const std::vector<std::string>& args, size_t first) {
    Parsed parsed;
    for (size_t i = first; i < args.size(); i++) {
        const std::string& arg = args[i];
        auto value = [&](const std::string& name) -> std::optional<std::string> {
            if (arg == name) {
                if (i + 1 >= args.size()) return std::nullopt;
                return args[++i];
            }
            if (arg.rfind(name + "=", 0) == 0) return arg.substr(name.size() + 1);
            return std::nullopt;
        };

        if (arg.rfind("--scheme", 0) == 0) {
            std::optional<std::string> scheme = value("--scheme");
            if (!scheme || scheme->empty()) {
                parsed.ok = false;
                parsed.problem = "--scheme demande un nom de schéma";
                return parsed;
            }
            parsed.scheme = *scheme;
        } else if (arg.rfind("--polls", 0) == 0) {
            std::optional<std::string> polls = value("--polls");
            if (!polls || polls->empty() || polls->find_first_not_of("0123456789") != std::string::npos) {
                parsed.ok = false;
                parsed.problem = "--polls demande un nombre";
                return parsed;
            }
            parsed.polls = std::stoi(*polls);
        } else if (arg.rfind("--", 0) == 0) {
            parsed.ok = false;
            parsed.problem = "option inconnue : " + arg;
            return parsed;
        } else {
            parsed.positional.push_back(arg);
        }
    }
    return parsed;
}

std::string SchemeName(const std::string& scheme) {
    return scheme.empty() ? std::string("le premier") : scheme;
}

void PrintList(const KitLayout& kit, std::ostream& out) {
    std::vector<std::string> folders = ListThemeFolders(kit.Themes());
    if (folders.empty()) {
        out << "Aucun thème dans " << kit.Themes().string() << '\n';
        return;
    }

    Settings settings;
    settings.Load(kit.Settings().string());
    ThemeSettings current = ReadThemeSettings(settings);

    for (const std::string& folder : folders) {
        FolderFiles files(kit.Themes() / folder);
        Theme theme = LoadTheme(folder, (kit.Themes() / folder).generic_string(), files);
        out << (folder == current.active ? "* " : "  ") << folder;
        if (theme.name != folder) out << " (" << theme.name << ")";
        out << " : " << (theme.usable ? "utilisable" : "refusé");
        if (!theme.schemes.empty()) {
            out << ", schémas :";
            for (const std::string& scheme : theme.schemes) out << ' ' << scheme;
        }
        out << '\n';
    }
    out << "* : thème actif\n";
}

int SetTheme(const std::vector<std::string>& args, const KitLayout& kit, std::ostream& out, std::ostream& err) {
    Parsed parsed = ParseArguments(args, 2);
    if (!parsed.ok) {
        err << parsed.problem << '\n';
        return kWrongUse;
    }
    if (parsed.positional.size() != 1) {
        err << "usage : amk theme set <id|none> [--scheme nom]\n";
        return kWrongUse;
    }

    const std::string id = parsed.positional[0];
    const bool none = id == "none";
    ThemeSettings value;
    if (!none) {
        std::vector<std::string> folders = ListThemeFolders(kit.Themes());
        bool found = false;
        for (const std::string& folder : folders) found = found || folder == id;
        if (!found) {
            err << "thème « " << id << " » introuvable dans " << kit.Themes().string() << '\n';
            return kWrongUse;
        }

        FolderFiles files(kit.Themes() / id);
        LoadOptions options;
        options.scheme = parsed.scheme;
        Theme theme = LoadTheme(id, (kit.Themes() / id).generic_string(), files, options);
        if (!theme.usable) {
            PrintThemeReport(theme, err);
            err << "thème « " << id << " » refusé : réglages inchangés\n";
            return kRefused;
        }
        for (const std::string& problem : theme.problems) out << "Attention : " << problem << '\n';
        value.active = id;
    }
    value.scheme = parsed.scheme;

    std::filesystem::create_directories(kit.root);
    Settings settings;
    settings.Load(kit.Settings().string());
    ThemeSettings current = ReadThemeSettings(settings);
    value.liveReload = current.liveReload;
    value.inspect = current.inspect;
    value.parts = current.parts;
    WriteThemeSettings(settings, value);
    if (!settings.Save(kit.Settings().string())) {
        err << "impossible d'écrire " << kit.Settings().string() << '\n';
        return kRefused;
    }

    if (none) {
        out << "Aucun thème : les couleurs d'origine reviennent.\n";
    } else {
        out << "Thème « " << id << " » choisi, schéma " << SchemeName(parsed.scheme) << ".\n";
    }
    out << "Si Apple Music est ouvert, le changement s'applique dans les 3 secondes, sans redémarrage.\n";
    return kOk;
}

}  // namespace

int PrintThemeReport(const Theme& theme, std::ostream& out) {
    out << "Thème : " << theme.name << " (dossier " << theme.id << ")\n";
    out << "Schémas : ";
    for (const std::string& scheme : theme.schemes) out << scheme << ' ';
    out << "\nSchéma utilisé : " << (theme.scheme.empty() ? std::string("aucun") : theme.scheme) << '\n';
    out << "Couleurs : " << theme.resources.size() << ", règles : " << theme.assignments.size()
        << ", dictionnaire : " << (theme.resourceDictionary.empty() ? "non" : "oui") << '\n';

    for (const Assignment& assignment : theme.assignments) {
        // A template is a whole XAML document: print its size, not its text.
        std::string value = assignment.value.kind == ValueKind::Template
                                ? "modèle XAML (" + std::to_string(assignment.value.text.size()) + " octets)"
                                : assignment.value.text;
        out << "  ligne " << assignment.line << " : " << assignment.selector.text << " → " << assignment.property
            << " = " << value << (assignment.important ? " (!important)" : "") << '\n';
    }
    for (const std::string& problem : theme.problems) out << "Problème : " << problem << '\n';

    bool clean = theme.usable && theme.problems.empty();
    out << (theme.usable ? "Résultat : utilisable" : "Résultat : refusé") << '\n';
    return clean ? 0 : kRefused;
}

int RunThemeCommand(const std::vector<std::string>& args, const KitLayout& kit, std::ostream& out, std::ostream& err) {
    if (args.size() < 2 || args[0] != "theme") {
        err << "usage : amk theme list | amk theme set <id|none> [--scheme nom]\n";
        return kWrongUse;
    }
    if (args[1] == "list") {
        PrintList(kit, out);
        return kOk;
    }
    if (args[1] == "set") return SetTheme(args, kit, out, err);

    err << "commande inconnue : amk theme " << args[1] << '\n';
    return kWrongUse;
}

int RunWatch(const std::vector<std::string>& args, std::ostream& out, std::ostream& err,
             const std::function<void()>& pause) {
    if (args.empty() || args[0] != "watch") {
        err << "usage : amk watch <dossier-du-thème> [--scheme nom] [--polls N]\n";
        return kWrongUse;
    }
    Parsed parsed = ParseArguments(args, 1);
    if (!parsed.ok || parsed.positional.size() != 1) {
        err << (parsed.ok ? std::string("usage : amk watch <dossier-du-thème> [--scheme nom] [--polls N]")
                          : parsed.problem)
            << '\n';
        return kWrongUse;
    }

    std::filesystem::path folder = std::filesystem::absolute(parsed.positional[0]);
    std::error_code error;
    if (!std::filesystem::is_directory(folder, error)) {
        err << "dossier introuvable : " << folder.string() << '\n';
        return kWrongUse;
    }

    auto check = [&]() {
        FolderFiles files(folder);
        LoadOptions options;
        options.scheme = parsed.scheme;
        Theme theme = LoadTheme(folder.filename().string(), folder.generic_string(), files, options);
        out << "--- " << (theme.usable ? "vérifié" : "refusé") << " : " << theme.problems.size() << " problème(s)\n";
        return PrintThemeReport(theme, out);
    };

    ChangeWatcher watcher([&] { return FolderSignature(folder); });
    int status = check();
    watcher.Poll();  // the baseline

    for (int poll = 1; parsed.polls == 0 || poll <= parsed.polls; poll++) {
        pause();
        if (watcher.Poll()) {
            out << "Changement détecté dans " << folder.filename().string() << '\n';
            status = check();
        }
    }
    return status;
}

}  // namespace amk::theme
