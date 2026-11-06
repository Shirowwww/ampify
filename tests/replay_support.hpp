#pragma once

// What the offline bench's tests share: the fixture, scratch folders, small themes and reading a report.
// tests/data/replay/transport-bar.json is the play bar of the CI bench's Apple Music (no account signed in), cut out of
// a scrubbed inspector.json.

#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <string_view>

#include "../src/core/json.hpp"
#include "../src/core/theme/replay.hpp"

namespace replay_support {

namespace fs = std::filesystem;

inline std::string ReadFile(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}

inline void WriteFile(const fs::path& file, const std::string& text) {
    fs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << text;
}

inline fs::path Fixture() {
    return fs::path(AMPIFY_SOURCE_DIR) / "tests" / "data" / "replay" / "transport-bar.json";
}

// A fresh folder for one test.
inline fs::path Scratch(const std::string& name) {
    fs::path dir = fs::temp_directory_path() / "ampify-replay-tests" / name;
    std::error_code error;
    fs::remove_all(dir, error);
    fs::create_directories(dir);
    return dir;
}

// A theme folder `id` under `dir` holding `css` as user.css, and `manifest` as manifest.json when given.
inline fs::path ThemeFolder(const fs::path& dir, const std::string& id, const std::string& css,
                            const std::string& manifest = {}) {
    fs::path folder = dir / id;
    WriteFile(folder / "user.css", css);
    if (!manifest.empty()) WriteFile(folder / "manifest.json", manifest);
    return folder;
}

inline int IndexNamed(const ampify::theme::ReplayBackend& tree, const std::string& name) {
    for (int i = 0; i < static_cast<int>(tree.Nodes().size()); i++) {
        if (tree.Nodes()[i].name == name) return i;
    }
    return -1;
}

inline const ampify::Json* Member(const ampify::Json& json, std::string_view key) {
    static const ampify::Json kNull;
    const ampify::Json* member = json.Find(key);
    return member ? member : &kNull;
}

inline const ampify::Json::Array& Items(const ampify::Json& json, std::string_view key) {
    static const ampify::Json::Array kNone;
    const ampify::Json::Array* list = Member(json, key)->AsArray();
    return list ? *list : kNone;
}

inline std::string Text(const ampify::Json* json) {
    return json && json->AsString() ? *json->AsString() : std::string();
}

inline double NumberOf(const ampify::Json* json) {
    return json && json->AsNumber() ? *json->AsNumber() : -1;
}

}  // namespace replay_support
