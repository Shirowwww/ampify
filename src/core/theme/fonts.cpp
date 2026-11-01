#include "fonts.hpp"

#include <algorithm>

namespace ampify::theme {

namespace {

const FontStyle* FindDefault(const std::string& role) {
    for (const FontStyle& style : DefaultFontRoles()) {
        if (style.role == role) return &style;
    }
    return nullptr;
}

}  // namespace

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
    for (const FontStyle& style : DefaultFontRoles()) names.push_back(style.role);
    if (!theme) return names;
    for (const auto& [role, value] : theme->fonts) {
        if (std::find(names.begin(), names.end(), role) == names.end()) names.push_back(role);
    }
    return names;
}

std::string FontRolesJson(const Theme* theme) {
    std::vector<FontStyle> roles;
    for (const std::string& name : FontRoleNames(theme)) {
        if (std::optional<FontStyle> style = ResolveFontRole(theme, name)) roles.push_back(std::move(*style));
    }
    return ampify::FontRolesJson(roles);
}

}  // namespace ampify::theme
