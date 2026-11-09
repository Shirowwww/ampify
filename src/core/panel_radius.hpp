#pragma once

// The corner radius the applied theme and snippets give their panels, their --panel-radius, which Ampify's own panels
// (Ampify.ui.panel) follow: their frame takes it, and nothing inside takes a larger one, so square corners (the
// Corners snippet) reach the song panel too. Published by the theme service, read by the interface's.

#include <atomic>
#include <cmath>
#include <limits>
#include <map>
#include <optional>
#include <string>

namespace ampify {

inline std::atomic<double>& PublishedPanelRadiusSlot() {
    static std::atomic<double> radius{std::numeric_limits<double>::quiet_NaN()};
    return radius;
}

// The number at the start of a resolved variable ("16", "16px", "0"); nothing for any other text.
inline std::optional<double> PanelRadiusOf(const std::map<std::string, std::string>& variables) {
    auto found = variables.find("panel-radius");
    if (found == variables.end()) return std::nullopt;
    try {
        size_t used = 0;
        double value = std::stod(found->second, &used);
        if (used == 0 || !std::isfinite(value) || value < 0) return std::nullopt;
        return value;
    } catch (...) {
        return std::nullopt;
    }
}

inline void PublishPanelRadius(std::optional<double> radius) {
    PublishedPanelRadiusSlot().store(radius ? *radius : std::numeric_limits<double>::quiet_NaN());
}

inline std::optional<double> PublishedPanelRadius() {
    double radius = PublishedPanelRadiusSlot().load();
    if (std::isnan(radius)) return std::nullopt;
    return radius;
}

}  // namespace ampify
