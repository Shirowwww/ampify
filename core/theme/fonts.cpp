#include "fonts.hpp"

#include <algorithm>
#include <cctype>

#include "../json.hpp"

namespace amk::theme {

namespace {

// The WinUI 3 type ramp of Windows 11 (Microsoft's « Typography in Windows » guidance).
const std::vector<FontStyle> kDefaults = {
    {"caption", "Segoe UI Variable Small", "400", "12", false},
    {"body", "Segoe UI Variable Text", "400", "14", false},
    {"body-strong", "Segoe UI Variable Text", "600", "14", false},
    {"subtitle", "Segoe UI Variable Display", "600", "20", false},
    {"title", "Segoe UI Variable Display", "600", "28", false},
    {"title-large", "Segoe UI Variable Display", "600", "40", false},
    {"display", "Segoe UI Variable Display", "600", "68", false},
};

const FontStyle* FindDefault(const std::string& role) {
    for (const FontStyle& style : kDefaults) {
        if (style.role == role) return &style;
    }
    return nullptr;
}

double NumberOr(const std::string& text, double fallback) {
    try {
        return text.empty() ? fallback : std::stod(text);
    } catch (...) {
        return fallback;
    }
}

}  // namespace

const std::vector<FontStyle>& DefaultFontRoles() {
    return kDefaults;
}

std::string CanonicalFontRole(std::string_view role) {
    std::string lowered(role);
    for (char& c : lowered) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (lowered == "titre") return "title";
    if (lowered == "texte") return "body";
    if (lowered == "sous-titre") return "subtitle";
    if (lowered == "legende" || lowered == "l\xc3\xa9gende") return "caption";
    return lowered;
}

std::optional<FontStyle> ResolveFontRole(const std::vector<const Theme*>& layers, std::string_view role) {
    FontStyle style;
    style.role = CanonicalFontRole(role);
    bool known = false;

    auto take = [&](std::string FontRole::*part, std::string FontStyle::*target) {
        for (const Theme* layer : layers) {
            if (!layer) continue;
            auto it = layer->fonts.find(style.role);
            if (it == layer->fonts.end() || (it->second.*part).empty()) continue;
            style.*target = it->second.*part;
            style.fromTheme = true;
            known = true;
            return;
        }
    };
    take(&FontRole::family, &FontStyle::family);
    take(&FontRole::weight, &FontStyle::weight);
    take(&FontRole::size, &FontStyle::size);

    if (const FontStyle* fallback = FindDefault(style.role)) {
        known = true;
        if (style.family.empty()) style.family = fallback->family;
        if (style.weight.empty()) style.weight = fallback->weight;
        if (style.size.empty()) style.size = fallback->size;
    }
    if (!known) return std::nullopt;
    return style;
}

std::optional<FontStyle> ResolveFontRole(const Theme* theme, std::string_view role) {
    return ResolveFontRole(std::vector<const Theme*>{theme}, role);
}

std::vector<std::string> FontRoleNames(const Theme* theme) {
    std::vector<std::string> names;
    for (const FontStyle& style : kDefaults) names.push_back(style.role);
    if (!theme) return names;
    for (const auto& [role, value] : theme->fonts) {
        if (std::find(names.begin(), names.end(), role) == names.end()) names.push_back(role);
    }
    return names;
}

std::string FontRolesJson(const Theme* theme) {
    Json::Object roles;
    for (const std::string& name : FontRoleNames(theme)) {
        std::optional<FontStyle> style = ResolveFontRole(theme, name);
        if (!style) continue;
        Json::Object entry;
        entry.emplace_back("family", Json(style->family));
        entry.emplace_back("weight", Json(NumberOr(style->weight, 400)));
        entry.emplace_back("size", Json(NumberOr(style->size, 14)));
        entry.emplace_back("fromTheme", Json(style->fromTheme));
        roles.emplace_back(name, Json(std::move(entry)));
    }
    return Json(std::move(roles)).Dump();
}

}  // namespace amk::theme
