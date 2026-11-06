#include "replay_contrast.hpp"

#include <algorithm>
#include <cstdlib>
#include <map>
#include <set>
#include <utility>

#include "color.hpp"
#include "inspector.hpp"
#include "properties.hpp"

namespace ampify::theme {

namespace {

constexpr double kNeeded = 4.5;       // WCAG AA, body text
constexpr double kNeededLarge = 3.0;  // large text and icons
constexpr double kLargeSize = 24;
constexpr double kLargeBoldSize = 18.66;
constexpr double kBold = 700;

bool IsText(const ReplayBackend::Node& node) {
    return node.type == "TextBlock" || node.type == "FontIcon" || node.type == "SymbolIcon" || node.type == "Run";
}

std::optional<Argb> ColourOf(const std::optional<Value>& value) {
    if (!value || value->kind != ValueKind::Color) return std::nullopt;
    return ParseColor(value->text);
}

double NumberOf(const std::optional<Value>& value, double fallback) {
    if (!value) return fallback;
    char* end = nullptr;
    double number = std::strtod(value->text.c_str(), &end);
    return end == value->text.c_str() ? fallback : number;
}

std::string Shown(std::string_view property, const std::optional<Value>& value) {
    if (!value) return {};
    if (value->kind == ValueKind::Opaque) return *value == kNoOwnValue ? std::string(kNoOwnValueText) : "present";
    if (value->kind == ValueKind::Template) return "template";
    return ShownText(property, *value);
}

std::string Hex(Argb colour) {
    return "#" + ToXamlColor(colour).substr(3);
}

// The elements and properties whose value the theme changed: the value now is not the one before the first write.
std::set<std::pair<int, std::string>> ChangedValues(ReplayBackend& tree) {
    std::map<std::pair<int, std::string>, std::string> before;
    for (const ReplayBackend::Change& change : tree.Changes()) {
        if (change.accepted && change.node >= 0) before.try_emplace({change.node, change.property}, change.before);
    }
    std::set<std::pair<int, std::string>> changed;
    for (const auto& [key, was] : before) {
        if (Shown(key.second, tree.Shown(key.first, key.second)) != was) changed.insert(key);
    }
    return changed;
}

double NeededBy(ReplayBackend& tree, int node) {
    const ReplayBackend::Node& text = tree.Nodes()[node];
    auto isIcon = [](const ReplayBackend::Node& element) {
        return element.icon || element.type == "FontIcon" || element.type == "SymbolIcon";
    };
    // A FontIcon draws its glyph with a TextBlock of its own: an icon too.
    if (isIcon(text) || (text.parent >= 0 && isIcon(tree.Nodes()[text.parent]))) return kNeededLarge;
    const double size = NumberOf(tree.Shown(node, "FontSize"), 14);
    const double weight = NumberOf(tree.Shown(node, "FontWeight"), 400);
    return size >= kLargeSize || (size >= kLargeBoldSize && weight >= kBold) ? kNeededLarge : kNeeded;
}

// Collapsed, or faded out, on the element or a parent.
bool Hidden(ReplayBackend& tree, const std::vector<int>& chain) {
    for (int at : chain) {
        if (!tree.Nodes()[at].reachable) continue;
        std::optional<Value> visibility = tree.Shown(at, "Visibility");
        if ((visibility && visibility->text == "Collapsed") || NumberOf(tree.Shown(at, "Opacity"), 1) <= 0) return true;
    }
    return false;
}

}  // namespace

const char* VerdictName(TextContrast::Verdict verdict) {
    switch (verdict) {
        case TextContrast::Verdict::Fine: return "fine";
        case TextContrast::Verdict::Low: return "low";
        case TextContrast::Verdict::NotJudged: return "not judged";
        case TextContrast::Verdict::Unknown: return "unknown";
    }
    return "";
}

std::vector<TextContrast> JudgeContrast(ReplayBackend& tree) {
    const std::set<std::pair<int, std::string>> changed = ChangedValues(tree);
    auto mine = [&](int node, const char* property) { return changed.count({node, property}) > 0; };
    const auto& nodes = tree.Nodes();
    std::vector<TextContrast> judged;
    for (int i = 0; i < static_cast<int>(nodes.size()); i++) {
        const ReplayBackend::Node& node = nodes[i];
        std::optional<Argb> ink = node.reachable && IsText(node) ? ColourOf(tree.Shown(i, "Foreground")) : std::nullopt;
        if (!ink || (node.box && (node.box->width <= 0 || node.box->height <= 0))) continue;
        std::vector<int> chain;  // the text, then each parent up to the root
        for (int at = i; at >= 0; at = nodes[at].parent) chain.push_back(at);
        if (Hidden(tree, chain)) continue;

        TextContrast result;
        result.node = i;
        result.text = Hex(*ink);
        bool theme = mine(i, "Foreground");
        bool otherMode = !theme && tree.ModeDiffers(i);
        size_t backdrop = chain.size();
        for (size_t j = 0; j < chain.size(); j++) {
            const int at = chain[j];
            std::optional<Value> background = nodes[at].reachable ? tree.Shown(at, "Background") : std::nullopt;
            if (!background) continue;
            std::optional<Argb> colour = ColourOf(background);
            if (!colour) {
                if (*background != kNoOwnValue) result.passedOver++;
                continue;
            }
            if (colour->a == 0) continue;
            const bool own = mine(at, "Background");
            theme = theme || own;
            otherMode = otherMode || (!own && tree.ModeDiffers(at));
            if (colour->a == 0xFF) {
                backdrop = j;
                break;
            }
        }
        if (!theme) continue;  // Apple's colours on both sides: Apple's design, judged by Apple

        result.needed = NeededBy(tree, i);
        if (otherMode) {
            result.verdict = TextContrast::Verdict::NotJudged;
        } else if (backdrop == chain.size()) {
            result.verdict = TextContrast::Verdict::Unknown;
        }
        if (backdrop < chain.size()) {
            // What each element between the text and the backdrop lets through of itself and of its children.
            std::vector<double> fade(backdrop + 1, 1.0);
            for (size_t j = backdrop; j-- > 0;) {
                const double own = nodes[chain[j]].reachable ? NumberOf(tree.Shown(chain[j], "Opacity"), 1) : 1;
                fade[j] = fade[j + 1] * std::clamp(own, 0.0, 1.0);
            }
            Argb under = *ColourOf(tree.Shown(chain[backdrop], "Background"));
            for (size_t j = backdrop; j-- > 0;) {
                if (!nodes[chain[j]].reachable) continue;
                std::optional<Argb> layer = ColourOf(tree.Shown(chain[j], "Background"));
                if (layer && layer->a > 0) under = Blend(under, *layer, layer->a / 255.0 * fade[j]);
            }
            const Argb shown = Blend(under, *ink, ink->a / 255.0 * fade[0]);
            result.backdrop = chain[backdrop];
            result.under = Hex(under);
            result.text = Hex(shown);
            result.ratio = ContrastRatio(shown, under);
            if (result.verdict == TextContrast::Verdict::Fine && result.ratio < result.needed) {
                result.verdict = TextContrast::Verdict::Low;
            }
        }
        judged.push_back(std::move(result));
    }
    return judged;
}

}  // namespace ampify::theme
