// ampify_theme_check: reads a theme folder the way the kernel does and prints what it would apply.
// It needs no Apple Music. Use it to check a theme before sending it, or in continuous integration.
//
//   ampify_theme_check <folder> [scheme]
//   ampify_theme_check --names <inspector.json>...
//
// The second form checks the stable names (.sidebar, .card...) against trees saved by the inspector: run it on the
// trees of a new Apple Music version to see which names its changes broke.
//
// Exit code: 0 when the theme is usable and has no problem (or every name was found), 1 when it is refused or has
// problems (or a name was found in no tree), 2 on bad use.

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "../src/core/theme/builtins.hpp"
#include "../src/core/theme/report.hpp"
#include "../src/core/theme/css.hpp"
#include "../src/core/theme/files.hpp"
#include "../src/core/theme/inspector.hpp"
#include "../src/core/theme/selector.hpp"
#include "../src/core/theme/theme.hpp"

namespace th = ampify::theme;

namespace {

int CountMatches(const th::Selector& selector, const std::vector<th::NodeInfo>& nodes) {
    int count = 0;
    for (int i = 0; i < static_cast<int>(nodes.size()); i++) count += th::Matches(selector, nodes, i) ? 1 : 0;
    return count;
}

int CheckNames(const std::vector<std::filesystem::path>& files) {
    std::vector<std::vector<th::NodeInfo>> trees;
    for (const std::filesystem::path& file : files) {
        std::ifstream in(file, std::ios::binary);
        std::stringstream text;
        text << in.rdbuf();
        std::string problem;
        std::optional<std::vector<th::NodeInfo>> tree = th::ReadInspectorTree(text.str(), &problem);
        if (!tree) {
            std::cerr << file.string() << ": " << problem << '\n';
            return 2;
        }
        trees.push_back(std::move(*tree));
    }

    std::cout << "Stable names checked against " << trees.size() << " tree(s):\n";
    std::vector<std::string> missing;
    for (const std::string& name : th::ClassNames()) {
        std::vector<std::string> forms = *th::ClassSelectors(name);
        std::vector<int> formElements(forms.size(), 0);
        int foundIn = 0;
        for (const std::vector<th::NodeInfo>& tree : trees) {
            bool found = false;
            for (size_t i = 0; i < forms.size(); i++) {
                std::optional<th::Selector> selector = th::ParseSelector(forms[i]);
                int count = selector ? CountMatches(*selector, tree) : 0;
                formElements[i] += count;
                found = found || count > 0;
            }
            foundIn += found ? 1 : 0;
        }
        int elements = 0;
        for (int count : formElements) elements += count;
        std::cout << "  ." << std::left << std::setw(26) << name << foundIn << "/" << trees.size() << " trees, "
                  << elements << " element(s)\n";
        for (size_t i = 0; i < forms.size(); i++) {
            if (formElements[i] == 0) std::cout << "      form found nowhere: " << forms[i] << '\n';
        }
        if (foundIn == 0) missing.push_back(name);
    }
    if (missing.empty()) return 0;
    std::cout << "Found in no tree (" << missing.size() << "):";
    for (const std::string& name : missing) std::cout << " ." << name;
    std::cout << '\n';
    return 1;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc >= 2 && std::string(argv[1]) == "--names") {
        if (argc < 3) {
            std::cerr << "usage: ampify_theme_check --names <inspector.json>...\n";
            return 2;
        }
        std::vector<std::filesystem::path> files;
        for (int i = 2; i < argc; i++) files.emplace_back(argv[i]);
        return CheckNames(files);
    }
    if (argc < 2 || argc > 3) {
        std::cerr << "usage: ampify_theme_check <theme-folder> [scheme]\n"
                     "       ampify_theme_check --names <inspector.json>...\n";
        return 2;
    }

    std::filesystem::path folder = std::filesystem::absolute(argv[1]);
    if (!std::filesystem::is_directory(folder)) {
        std::cerr << "folder not found: " << folder.string() << '\n';
        return 2;
    }

    th::LoadOptions options;
    if (argc == 3) options.scheme = argv[2];

    th::FolderFiles files(folder);
    th::Theme theme = th::LoadTheme(folder.filename().string(), folder.generic_string(), files, options);
    return th::PrintThemeReport(theme, std::cout);
}
