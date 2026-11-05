#include "glass.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
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

// The animations of the value that end; nothing left when every one of them runs forever.
std::optional<std::string> WithoutEndless(const std::string& text) {
    std::optional<std::vector<Animation>> animations = DecodeAnimations(text);
    if (!animations) return text;
    std::erase_if(*animations, [](const Animation& animation) { return animation.iterations == 0; });
    if (animations->empty()) return std::nullopt;
    return EncodeAnimations(*animations);
}

}  // namespace

GlassLevel GlassLevelOf(std::string_view value) {
    std::string name = ToLowerAscii(TrimSpace(value));
    if (name == "balanced") return GlassLevel::Balanced;
    if (name == "eco") return GlassLevel::Eco;
    return GlassLevel::Full;
}

void ApplyGlassLevel(Theme& theme, GlassLevel level) {
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
        if (level == GlassLevel::Eco && value.kind == ValueKind::Acrylic) {
            if (std::optional<Value> plain = PlainColourOf(value.text)) value = std::move(*plain);
        }
        kept.push_back(std::move(assignment));
    }
    theme.assignments = std::move(kept);
}

}  // namespace ampify::theme
