#include "theme_commands.hpp"

#include <fstream>
#include <iomanip>
#include <sstream>

#include "../core/theme/builtins.hpp"
#include "../core/theme/files.hpp"
#include "../core/theme/inspector.hpp"
#include "../core/theme/library.hpp"
#include "../core/theme/selector.hpp"

namespace ampify::theme {

namespace {

constexpr int kOk = 0;
constexpr int kMissing = 1;
constexpr int kWrongUse = 2;

int CountMatches(const Selector& selector, const std::vector<NodeInfo>& nodes) {
    int count = 0;
    for (int i = 0; i < static_cast<int>(nodes.size()); i++) count += Matches(selector, nodes, i) ? 1 : 0;
    return count;
}

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

}  // namespace

int RunNames(const std::vector<std::filesystem::path>& files, std::ostream& out, std::ostream& err) {
    std::vector<std::vector<NodeInfo>> trees;
    for (const std::filesystem::path& file : files) {
        std::ifstream in(file, std::ios::binary);
        std::stringstream text;
        text << in.rdbuf();
        std::string problem;
        std::optional<std::vector<NodeInfo>> tree = ReadInspectorTree(text.str(), &problem);
        if (!tree) {
            err << file.string() << ": " << problem << '\n';
            return kWrongUse;
        }
        trees.push_back(std::move(*tree));
    }

    out << "Stable names checked against " << trees.size() << " tree(s):\n";
    std::vector<std::string> missing;
    for (const std::string& name : ClassNames()) {
        std::vector<std::string> forms = *ClassSelectors(name);
        std::vector<int> formElements(forms.size(), 0);
        int foundIn = 0;
        for (const std::vector<NodeInfo>& tree : trees) {
            bool found = false;
            for (size_t i = 0; i < forms.size(); i++) {
                std::optional<Selector> selector = ParseSelector(forms[i]);
                int count = selector ? CountMatches(*selector, tree) : 0;
                formElements[i] += count;
                found = found || count > 0;
            }
            foundIn += found ? 1 : 0;
        }
        int elements = 0;
        for (int count : formElements) elements += count;
        out << "  ." << std::left << std::setw(26) << name << foundIn << "/" << trees.size() << " trees, " << elements
            << " element(s)\n";
        for (size_t i = 0; i < forms.size(); i++) {
            if (formElements[i] == 0) out << "      form found nowhere: " << forms[i] << '\n';
        }
        if (foundIn == 0) missing.push_back(name);
    }
    if (missing.empty()) return kOk;
    out << "Found in no tree (" << missing.size() << "):";
    for (const std::string& name : missing) out << " ." << name;
    out << '\n';
    return kMissing;
}

int RunWatch(const std::vector<std::string>& args, std::ostream& out, std::ostream& err,
             const std::function<void()>& pause) {
    if (args.empty() || args[0] != "watch") {
        err << "usage: ampify theme watch <theme-folder> [--scheme name] [--max-cycles N]\n";
        return kWrongUse;
    }
    Parsed parsed = ParseArguments(args, 1);
    if (!parsed.ok || parsed.positional.size() != 1) {
        err << (parsed.ok ? std::string("usage: ampify theme watch <theme-folder> [--scheme name] [--max-cycles N]")
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
