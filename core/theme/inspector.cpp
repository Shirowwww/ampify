#include "inspector.hpp"

#include <algorithm>

namespace amk::theme {

namespace {

// The properties worth seeing when choosing a rule. Each one is read with Read(), so an element that lacks it is skipped.
constexpr const char* kInspectedProperties[] = {
    "Background", "Foreground", "BorderBrush", "CornerRadius", "FontFamily", "FontSize", "FontWeight",
    "Margin", "Padding", "Width", "Height", "Opacity", "Visibility",
};

std::string Label(const NodeInfo& node) {
    return node.name.empty() ? node.type : node.type + "#" + node.name;
}

std::string ValueText(const Value& value) {
    if (value.kind == ValueKind::Template || value.kind == ValueKind::Opaque) return "présent";
    return value.text;
}

}  // namespace

std::string SuggestSelector(const std::vector<NodeInfo>& nodes, int index) {
    std::vector<int> chain;  // from the element up to the nearest named ancestor
    for (int current = index; current >= 0 && current < static_cast<int>(nodes.size()); current = nodes[current].parent) {
        chain.push_back(current);
        if (!nodes[current].name.empty()) break;
    }
    std::reverse(chain.begin(), chain.end());

    std::string selector;
    for (int position : chain) {
        if (!selector.empty()) selector += " > ";
        selector += Label(nodes[position]);
    }
    return selector;
}

std::string InspectTree(TreeBackend& tree, const InspectOptions& options) {
    std::string dump;

    std::vector<std::string> keys = tree.ResourceKeys();
    dump += "Ressources de l'application (" + std::to_string(keys.size()) + ") :\n";
    for (const std::string& key : keys) {
        std::optional<Value> value = tree.ReadResource(key);
        dump += "  " + key + " = " + (value ? ValueText(*value) : std::string("(illisible)")) + "\n";
    }

    std::vector<NodeInfo> nodes = tree.Snapshot();
    dump += "\nArbre (" + std::to_string(nodes.size()) + " éléments) :\n";

    size_t lines = 0;
    for (int index = 0; index < static_cast<int>(nodes.size()); index++) {
        if (lines++ >= options.maxLines) {
            dump += "... arrêté après " + std::to_string(options.maxLines) + " lignes\n";
            break;
        }

        const NodeInfo& node = nodes[index];
        dump += std::string(static_cast<size_t>(node.depth) * 2, ' ') + Label(node);
        if (!node.fullType.empty()) dump += "  (" + node.fullType + ")";
        dump += "  sélecteur : " + SuggestSelector(nodes, index);

        for (const char* property : kInspectedProperties) {
            std::optional<Value> value = tree.Read(node.id, property);
            if (value) dump += "  " + std::string(property) + "=" + ValueText(*value);
        }
        dump += '\n';
    }
    return dump;
}

}  // namespace amk::theme
