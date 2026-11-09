#include "glass.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "../text_util.hpp"
#include "animation.hpp"
#include "color.hpp"

namespace ampify::theme {

namespace {

// Without the blur, the tint is laid this much more opaque, so the text on the pane keeps its contrast.
constexpr double kEcoOpacityGain = 0.25;

// "#AARRGGBB;opacity" (ValueKind::Acrylic) as one plain colour.
std::optional<Value> PlainColourOf(const std::string& acrylic) {
    std::size_t split = acrylic.find(';');
    if (split == std::string::npos) return std::nullopt;
    std::optional<Argb> tint = ParseColor(std::string_view(acrylic).substr(0, split));
    if (!tint) return std::nullopt;
    double opacity = std::strtod(acrylic.c_str() + split + 1, nullptr);
    double alpha = tint->a * std::clamp(opacity + kEcoOpacityGain, 0.0, 1.0);
    tint->a = static_cast<std::uint8_t>(std::lround(alpha));
    return Value{ValueKind::Color, ToXamlColor(*tint)};
}

// The colour matrix of a ValueKind::Blur text ("amount,top,bottom,m1,...,m20"), nothing when it has none.
std::optional<std::string> ColourPartOf(const std::string& blur) {
    constexpr int kAmountAndEdges = 3;
    size_t start = 0;
    for (int i = 0; i < kAmountAndEdges; i++) {
        const size_t comma = blur.find(',', start);
        if (comma == std::string::npos) return std::nullopt;
        start = comma + 1;
    }
    return blur.substr(start);
}

// The animations of the value that end; nothing left when every one of them runs forever.
std::optional<std::string> WithoutEndless(const std::string& text) {
    std::optional<std::vector<Animation>> animations = DecodeAnimations(text);
    if (!animations) return text;
    std::erase_if(*animations, [](const Animation& animation) { return animation.iterations == 0; });
    if (animations->empty()) return std::nullopt;
    return EncodeAnimations(*animations);
}

// A transition ("transform:200;width:1000") without its width; nothing left when the width was all of it.
std::optional<std::string> WithoutWidth(const std::string& text) {
    std::string kept;
    std::string_view rest = text;
    while (!rest.empty()) {
        size_t end = rest.find(';');
        std::string_view item = rest.substr(0, end);
        rest = end == std::string_view::npos ? std::string_view{} : rest.substr(end + 1);
        if (item.empty() || item.starts_with("width:")) continue;
        kept += (kept.empty() ? "" : ";") + std::string(item);
    }
    if (kept.empty()) return std::nullopt;
    return kept;
}

}  // namespace

namespace {
std::atomic<GlassLevel> g_currentLevel{GlassLevel::Full};
}  // namespace

void SetCurrentGlassLevel(GlassLevel level) {
    g_currentLevel.store(level, std::memory_order_relaxed);
}

GlassLevel CurrentGlassLevel() {
    return g_currentLevel.load(std::memory_order_relaxed);
}

int ArtworkFadeMs(GlassLevel level, bool animationsOn) {
    return animationsOn && level != GlassLevel::Eco ? kArtworkFadeMs : 0;
}

GlassLevel GlassLevelOf(std::string_view value) {
    std::string name = ToLowerAscii(TrimSpace(value));
    if (name == "balanced") return GlassLevel::Balanced;
    if (name == "eco") return GlassLevel::Eco;
    return GlassLevel::Full;
}

WindowMaterial MaterialAt(GlassLevel level, WindowMaterial material) {
    // Without the window's own blur, Mica is the plain tint of the desktop.
    return level == GlassLevel::Eco && material == WindowMaterial::Acrylic ? WindowMaterial::Mica : material;
}

void ApplyGlassLevel(Theme& theme, GlassLevel level) {
    theme.glass = level;
    if (level == GlassLevel::Full) return;
    std::vector<Assignment> kept;
    kept.reserve(theme.assignments.size());
    for (Assignment& assignment : theme.assignments) {
        Value& value = assignment.value;
        // "0" is `box-shadow: none`: it removes depth, so it stays.
        if (value.kind == ValueKind::Shadow && value.text != "0") continue;
        if (value.kind == ValueKind::Animation && !value.text.empty()) {
            std::optional<std::string> ending = WithoutEndless(value.text);
            if (!ending) continue;
            value.text = std::move(*ending);
        }
        // A width that glides lays out a frame at a time for as long as a song plays, as an endless animation does.
        if (value.kind == ValueKind::Transition && !value.text.empty()) {
            std::optional<std::string> still = WithoutWidth(value.text);
            if (!still) continue;
            value.text = std::move(*still);
        }
        // A blur costs the graphics card a pass each frame its area changes; "0" removes one, so it stays. A colour
        // filter (the matrix after the amount and edges) costs one cheap pass and stays, unblurred.
        if (level == GlassLevel::Eco && value.kind == ValueKind::Blur && std::atof(value.text.c_str()) != 0) {
            std::optional<std::string> colours = ColourPartOf(value.text);
            if (!colours) continue;
            value.text = "0,0,0," + *colours;
        }
        if (level == GlassLevel::Eco && value.kind == ValueKind::Acrylic) {
            if (std::optional<Value> plain = PlainColourOf(value.text)) value = std::move(*plain);
        }
        kept.push_back(std::move(assignment));
    }
    theme.assignments = std::move(kept);
}

}  // namespace ampify::theme
