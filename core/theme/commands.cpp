#include "commands.hpp"

#include "files.hpp"
#include "library.hpp"
#include "theme_settings.hpp"
#include "../settings.hpp"

namespace ampify::theme {

namespace {

constexpr int kOk = 0;
constexpr int kRefused = 1;
constexpr int kWrongUse = 2;

// The options a command accepts: `--scheme name`, `--scheme=name`, `--polls N`. Positional arguments come back in order.
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
                parsed.problem = "--scheme needs a scheme name";
                return parsed;
            }
            parsed.scheme = *scheme;
        } else if (arg.rfind("--polls", 0) == 0) {
            std::optional<std::string> polls = value("--polls");
            if (!polls || polls->empty() || polls->find_first_not_of("0123456789") != std::string::npos) {
                parsed.ok = false;
                parsed.problem = "--polls needs a number";
                return parsed;
            }
            parsed.polls = std::stoi(*polls);
        } else if (arg.rfind("--", 0) == 0) {
            parsed.ok = false;
            parsed.problem = "unknown option: " + arg;
            return parsed;
        } else {
            parsed.positional.push_back(arg);
        }
    }
    return parsed;
}

std::string SchemeName(const std::string& scheme) {
    return scheme.empty() ? std::string("the first one") : scheme;
}

void PrintList(const KitLayout& kit, std::ostream& out) {
    std::vector<std::string> folders = ListThemeFolders(kit.Themes());
    if (folders.empty()) {
        out << "No themes in " << kit.Themes().string() << '\n';
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
        out << ": " << (theme.usable ? "usable" : "refused");
        if (!theme.schemes.empty()) {
            out << ", schemes:";
            for (const std::string& scheme : theme.schemes) out << ' ' << scheme;
        }
        out << '\n';
    }
    out << "* : active theme\n";
}

int SetTheme(const std::vector<std::string>& args, const KitLayout& kit, std::ostream& out, std::ostream& err) {
    Parsed parsed = ParseArguments(args, 2);
    if (!parsed.ok) {
        err << parsed.problem << '\n';
        return kWrongUse;
    }
    if (parsed.positional.size() != 1) {
        err << "usage: ampify theme set <id|none> [--scheme name]\n";
        return kWrongUse;
    }

    const std::string id = parsed.positional[0];
    const bool none = id == "none";
    std::string scheme = parsed.scheme;  // the one the theme gets: --scheme, else its default
    ThemeSettings value;
    if (!none) {
        std::vector<std::string> folders = ListThemeFolders(kit.Themes());
        bool found = false;
        for (const std::string& folder : folders) found = found || folder == id;
        if (!found) {
            err << "theme \"" << id << "\" not found in " << kit.Themes().string() << '\n';
            return kWrongUse;
        }

        FolderFiles files(kit.Themes() / id);
        if (scheme.empty()) scheme = ReadDefaultScheme(files);
        LoadOptions options;
        options.scheme = scheme;
        Theme theme = LoadTheme(id, (kit.Themes() / id).generic_string(), files, options);
        if (!theme.usable) {
            PrintThemeReport(theme, err);
            err << "theme \"" << id << "\" refused: settings unchanged\n";
            return kRefused;
        }
        for (const std::string& problem : theme.problems) out << "Warning: " << problem << '\n';
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
    value.schedule = current.schedule;
    WriteThemeSettings(settings, value);
    if (!settings.Save(kit.Settings().string())) {
        err << "cannot write " << kit.Settings().string() << '\n';
        return kRefused;
    }

    if (none) {
        out << "No theme: the original colors are back.\n";
    } else {
        out << "Theme \"" << id << "\" chosen, scheme " << SchemeName(scheme) << ".\n";
    }
    out << "If Apple Music is open, the change applies within 3 seconds, without a restart.\n";
    return kOk;
}

}  // namespace

int PrintThemeReport(const Theme& theme, std::ostream& out) {
    out << "Theme: " << theme.name << " (folder " << theme.id << ")\n";
    out << "Schemes: ";
    for (const std::string& scheme : theme.schemes) out << scheme << ' ';
    out << "\nScheme used: " << (theme.scheme.empty() ? std::string("none") : theme.scheme) << '\n';
    out << "Colors: " << theme.resources.size() << ", rules: " << theme.assignments.size()
        << ", dictionary: " << (theme.resourceDictionary.empty() ? "no" : "yes") << '\n';

    for (const Assignment& assignment : theme.assignments) {
        // A template is a whole XAML document: print its size, not its text.
        std::string value = assignment.value.kind == ValueKind::Template
                                ? "XAML template (" + std::to_string(assignment.value.text.size()) + " bytes)"
                                : ShownText(assignment.property, assignment.value);
        out << "  line " << assignment.line << ": " << assignment.selector.text << " → " << assignment.property
            << " = " << value << (assignment.important ? " (!important)" : "") << '\n';
    }
    for (const std::string& problem : theme.problems) out << "Problem: " << problem << '\n';

    bool clean = theme.usable && theme.problems.empty();
    out << (theme.usable ? "Result: usable" : "Result: refused") << '\n';
    return clean ? 0 : kRefused;
}

int RunThemeCommand(const std::vector<std::string>& args, const KitLayout& kit, std::ostream& out, std::ostream& err) {
    if (args.size() < 2 || args[0] != "theme") {
        err << "usage: ampify theme list | ampify theme set <id|none> [--scheme name]\n";
        return kWrongUse;
    }
    if (args[1] == "list") {
        PrintList(kit, out);
        return kOk;
    }
    if (args[1] == "set") return SetTheme(args, kit, out, err);

    err << "unknown command: ampify theme " << args[1] << '\n';
    return kWrongUse;
}

int RunWatch(const std::vector<std::string>& args, std::ostream& out, std::ostream& err,
             const std::function<void()>& pause) {
    if (args.empty() || args[0] != "watch") {
        err << "usage: ampify watch <theme-folder> [--scheme name] [--polls N]\n";
        return kWrongUse;
    }
    Parsed parsed = ParseArguments(args, 1);
    if (!parsed.ok || parsed.positional.size() != 1) {
        err << (parsed.ok ? std::string("usage: ampify watch <theme-folder> [--scheme name] [--polls N]")
                          : parsed.problem)
            << '\n';
        return kWrongUse;
    }

    std::filesystem::path folder = std::filesystem::absolute(parsed.positional[0]);
    std::error_code error;
    if (!std::filesystem::is_directory(folder, error)) {
        err << "folder not found: " << folder.string() << '\n';
        return kWrongUse;
    }

    auto check = [&]() {
        FolderFiles files(folder);
        LoadOptions options;
        options.scheme = parsed.scheme.empty() ? ReadDefaultScheme(files) : parsed.scheme;
        Theme theme = LoadTheme(folder.filename().string(), folder.generic_string(), files, options);
        out << "--- " << (theme.usable ? "checked" : "refused") << ": " << theme.problems.size() << " problem(s)\n";
        return PrintThemeReport(theme, out);
    };

    ChangeWatcher watcher([&] { return FolderSignature(folder); });
    int status = check();
    watcher.Poll();  // the baseline

    for (int poll = 1; parsed.polls == 0 || poll <= parsed.polls; poll++) {
        pause();
        if (watcher.Poll()) {
            out << "Change detected in " << folder.filename().string() << '\n';
            status = check();
        }
    }
    return status;
}

}  // namespace ampify::theme
