#include "compile.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <sstream>
#include <string_view>
#include <system_error>
#include <vector>

#include "../core/json.hpp"
#include "internal.hpp"

namespace ampify::cli {

namespace {

constexpr std::string_view kSourceExtensions[] = {".ts", ".tsx", ".jsx", ".mts"};
// What a source imports goes into main.js too, and so does what tells esbuild how to read it.
constexpr std::string_view kBundledExtensions[] = {".js", ".mjs", ".cjs", ".json"};

// A path as UTF-8 text, in C++17 and in C++20 (where u8string is its own type).
std::string Text(const std::filesystem::path& path) {
    auto text = path.u8string();
    return std::string(text.begin(), text.end());
}
constexpr int kParentLevels = 4;  // where node_modules is looked for above the element's folder

// A file whose change makes main.js out of date: the sources, and the plain modules and data they may import.
bool ChangesTheBuild(const std::filesystem::path& path) {
    std::string name = ToLowerAscii(Text(path.filename()));
    if (name.size() > 5 && name.compare(name.size() - 5, 5, ".d.ts") == 0) return false;  // declarations, not code
    if (name == "manifest.json") return false;  // Ampify's own file: esbuild never reads it
    std::string extension = ToLowerAscii(Text(path.extension()));
    auto among = [&](const auto& extensions) {
        return std::find(std::begin(extensions), std::end(extensions), extension) != std::end(extensions);
    };
    return among(kSourceExtensions) || among(kBundledExtensions);
}

// The "main" of manifest.json, or nullopt when the folder has no script. Only a name the manifest's rule takes, a .js
// file at the root: the build never writes outside the folder.
std::optional<std::string> MainName(const std::filesystem::path& folder) {
    std::optional<std::string> text = ampify::ReadWholeFile(folder / "manifest.json");
    if (!text) return std::nullopt;
    std::optional<std::string> main = ParseManifest(*text, "", kKitApiMajor, nullptr).partial.main;
    return main && IsMainFileName(*main) ? main : std::nullopt;
}

struct Sources {
    std::filesystem::path entry;  // the source that has main's name: main.ts for main.js
    // The latest change among every source of the folder. Not `{}`: that is the clock's epoch, which libstdc++ puts in 2174.
    std::filesystem::file_time_type newest = std::filesystem::file_time_type::min();
};

std::optional<Sources> FindSources(const std::filesystem::path& folder, const std::filesystem::path& main) {
    Sources found;
    std::error_code ec;
    for (std::string_view extension : kSourceExtensions) {
        std::filesystem::path candidate = main;
        candidate.replace_extension(std::string(extension));
        if (std::filesystem::is_regular_file(candidate, ec)) {
            found.entry = candidate;
            break;
        }
    }
    if (found.entry.empty()) return std::nullopt;

    auto it = std::filesystem::recursive_directory_iterator(folder, ec);
    for (; !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        std::string leaf = Text(it->path().filename());
        if (it->is_directory(ec)) {
            if (leaf == "node_modules" || (!leaf.empty() && leaf[0] == '.')) it.disable_recursion_pending();
            continue;
        }
        if (!ChangesTheBuild(it->path())) continue;
        auto time = std::filesystem::last_write_time(it->path(), ec);
        if (!ec && time > found.newest) found.newest = time;
    }
    return found;
}

// On Windows `npm install -g` leaves an esbuild.cmd on the PATH, which CreateProcess does not start: node runs the
// package's own script from npm's global folder instead. Elsewhere the command on the PATH is enough.
std::optional<std::filesystem::path> GlobalEsbuildScript() {
#ifdef _WIN32
    wchar_t* appData = nullptr;
    size_t length = 0;
    if (_wdupenv_s(&appData, &length, L"APPDATA") != 0 || appData == nullptr) return std::nullopt;
    std::filesystem::path script = std::filesystem::path(appData) / "npm" / "node_modules" / "esbuild" / "bin" / "esbuild";
    std::free(appData);
    std::error_code ec;
    if (std::filesystem::is_regular_file(script, ec)) return script;
#endif
    return std::nullopt;
}

// How to start esbuild: the author's local install through node, then npm's global one on Windows, else the command on
// the PATH.
std::vector<std::string> EsbuildCommand(const std::filesystem::path& folder) {
    std::error_code ec;
    std::filesystem::path dir = std::filesystem::absolute(folder, ec);
    for (int level = 0; level <= kParentLevels && !dir.empty(); level++, dir = dir.parent_path()) {
        std::filesystem::path script = dir / "node_modules" / "esbuild" / "bin" / "esbuild";
        if (std::filesystem::is_regular_file(script, ec)) return {"node", Text(script)};
        if (dir == dir.parent_path()) break;
    }
    if (std::optional<std::filesystem::path> global = GlobalEsbuildScript()) return {"node", Text(*global)};
    return {"esbuild"};
}

}  // namespace

CompileOutcome CompileSources(const std::filesystem::path& folder, std::string& message, const RunProgram& run) {
    std::optional<std::string> name = MainName(folder);
    if (!name) return CompileOutcome::NoSource;
    std::filesystem::path main = folder / std::filesystem::path(*name);
    std::optional<Sources> sources = FindSources(folder, main);
    if (!sources) return CompileOutcome::NoSource;

    std::error_code ec;
    if (std::filesystem::is_regular_file(main, ec)) {
        auto built = std::filesystem::last_write_time(main, ec);
        if (!ec && built >= sources->newest) return CompileOutcome::UpToDate;
    }

    std::vector<std::string> command = EsbuildCommand(folder);
    for (std::string argument : {Text(sources->entry), std::string("--bundle"), std::string("--format=esm"),
                                 std::string("--target=es2020"), std::string("--jsx=transform"),
                                 std::string("--jsx-factory=Ampify.React.createElement"),
                                 std::string("--jsx-fragment=Ampify.React.Fragment"), std::string("--log-level=warning"),
                                 "--outfile=" + Text(main)}) {
        command.push_back(std::move(argument));
    }
    int code = run(command);
    if (code == -1) {
        message = "esbuild is not installed: run \"npm install --save-dev esbuild\" in " + Text(folder) +
                  " (Ampify turns main.ts, main.tsx and main.jsx into main.js with it)";
        return CompileOutcome::Failed;
    }
    if (code != 0) {
        message = "esbuild failed on " + Text(sources->entry) + " (code " + std::to_string(code) + "), see above";
        return CompileOutcome::Failed;
    }
    message = Text(sources->entry.filename()) + " compiled to " + Text(main.filename());
    return CompileOutcome::Compiled;
}

}  // namespace ampify::cli
