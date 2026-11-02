#include "font_roles.hpp"

#include <cctype>
#include <mutex>

#include "json.hpp"
#include "text_util.hpp"

namespace ampify {

namespace {

struct Published {
    std::mutex lock;
    std::string json;  // empty: nothing published, the defaults apply
};

Published& Board() {
    static Published board;
    return board;
}

double NumberOr(const std::string& text, double fallback) {
    try {
        return text.empty() ? fallback : std::stod(text);
    } catch (...) {
        return fallback;
    }
}

}  // namespace

std::string CanonicalFontRole(std::string_view role) {
    std::string lowered = ToLowerAscii(role);
    if (lowered == "text") return "body";
    return lowered;
}

const std::vector<FontStyle>& DefaultFontRoles() {
    // Microsoft's "Typography in Windows" guidance.
    static const std::vector<FontStyle> roles = {
        {"caption", "Segoe UI Variable Small", "400", "12", false},
        {"body", "Segoe UI Variable Text", "400", "14", false},
        {"body-strong", "Segoe UI Variable Text", "600", "14", false},
        {"subtitle", "Segoe UI Variable Display", "600", "20", false},
        {"title", "Segoe UI Variable Display", "600", "28", false},
        {"title-large", "Segoe UI Variable Display", "600", "40", false},
        {"display", "Segoe UI Variable Display", "600", "68", false},
    };
    return roles;
}

std::string FontRolesJson(const std::vector<FontStyle>& roles) {
    Json::Object out;
    for (const FontStyle& style : roles) {
        Json::Object entry;
        entry.emplace_back("family", Json(style.family));
        entry.emplace_back("weight", Json(NumberOr(style.weight, 400)));
        entry.emplace_back("size", Json(NumberOr(style.size, 14)));
        entry.emplace_back("fromTheme", Json(style.fromTheme));
        out.emplace_back(style.role, Json(std::move(entry)));
    }
    return Json(std::move(out)).Dump();
}

void PublishFontRoles(std::string json) {
    Published& board = Board();
    std::lock_guard<std::mutex> hold(board.lock);
    board.json = std::move(json);
}

std::string PublishedFontRoles() {
    Published& board = Board();
    {
        std::lock_guard<std::mutex> hold(board.lock);
        if (!board.json.empty()) return board.json;
    }
    static const std::string defaults = FontRolesJson(DefaultFontRoles());
    return defaults;
}

}  // namespace ampify
