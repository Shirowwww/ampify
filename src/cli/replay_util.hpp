#pragma once

// What the files of the offline bench's commands share (theme_replay.cpp, theme_diff.cpp, replay_wireframe.cpp,
// replay_command.cpp): paths in UTF-8, whole files, and the pieces of the reports' JSON.

#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

#include "../core/json.hpp"
#include "../core/theme/inspector.hpp"
#include "../core/theme/properties.hpp"
#include "../core/theme/replay.hpp"

namespace ampify::theme::replay {

namespace fs = std::filesystem;

inline std::string Utf8(const fs::path& path) {
    std::u8string text = path.generic_u8string();
    return std::string(text.begin(), text.end());
}

inline fs::path PathOf(std::string_view text) {
    return fs::path(std::u8string(text.begin(), text.end()));
}

inline std::optional<std::string> ReadText(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::nullopt;
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}

inline bool WriteText(const fs::path& file, const std::string& text) {
    std::ofstream out(file, std::ios::binary);
    out << text;
    return static_cast<bool>(out);
}

inline std::string Label(const ReplayBackend::Node& node) {
    return node.name.empty() ? node.type : node.type + "#" + node.name;
}

inline Json Number(double value) {
    return Json(value);
}

// The text a recorded value shows, as the inspector writes it.
inline std::string ShownOf(std::string_view property, const std::optional<Value>& value) {
    if (!value) return {};
    if (value->kind == ValueKind::Opaque) return *value == kNoOwnValue ? std::string(kNoOwnValueText) : "present";
    if (value->kind == ValueKind::Template) return "template";
    return ShownText(property, *value);
}

// The id a folder takes in the kit: its name, as the worker reads themes\<id>.
inline std::string FolderId(const fs::path& folder) {
    fs::path normal = fs::absolute(folder).lexically_normal();
    std::string id = Utf8(normal.filename());
    return id.empty() ? Utf8(normal.parent_path().filename()) : id;
}

inline std::string Lines(const Json::Array& items) {
    if (items.empty()) return "[]";
    std::string text = "[";
    for (size_t i = 0; i < items.size(); i++) text += (i ? ",\n    " : "\n    ") + items[i].Dump();
    return text + "\n  ]";
}

inline std::string Str(const Json& item, std::string_view key) {
    const Json* member = item.Find(key);
    if (!member) return {};
    if (const std::string* text = member->AsString()) return *text;
    return member->Dump();
}

inline const Json::Array& ArrayOf(const Json& report, std::string_view key) {
    static const Json::Array kNone;
    const Json* member = report.Find(key);
    return member && member->AsArray() ? *member->AsArray() : kNone;
}

// The wireframe of the tree as the replay left it: boxes when the dump has them, a list otherwise
// (replay_wireframe.cpp).
std::string Wireframe(ReplayBackend& tree, const std::string& title);

}  // namespace ampify::theme::replay
