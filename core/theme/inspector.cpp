#include "inspector.hpp"

#include <algorithm>

#include "../json.hpp"

namespace amk::theme {

namespace {

// The properties worth seeing when choosing a rule. Each one is read with Read(), so an element that lacks it is skipped.
constexpr const char* kInspectedProperties[] = {
    "Background", "Foreground", "BorderBrush", "CornerRadius", "FontFamily", "FontSize", "FontWeight",
    "Margin", "Padding", "Width", "Height", "Opacity", "Visibility", "FlowDirection",
};

// Items of a JSON list, one per line, for a file a person may open.
std::string JsonLines(const std::vector<std::string>& items) {
    if (items.empty()) return "[]";
    std::string text = "[";
    for (size_t i = 0; i < items.size(); i++) text += (i ? ",\n    " : "\n    ") + items[i];
    return text + "\n  ]";
}

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

std::string InspectTreeJson(TreeBackend& tree, const InspectOptions& options) {
    std::vector<std::string> resources;
    for (const std::string& key : tree.ResourceKeys()) {
        std::optional<Value> value = tree.ReadResource(key);
        Json::Object entry;
        entry.emplace_back("key", Json(key));
        entry.emplace_back("value", value ? Json(ValueText(*value)) : Json());
        resources.push_back(Json(std::move(entry)).Dump());
    }

    std::vector<NodeInfo> nodes = tree.Snapshot();
    std::vector<std::string> list;
    bool truncated = false;
    for (int index = 0; index < static_cast<int>(nodes.size()); index++) {
        if (static_cast<size_t>(index) >= options.maxLines) {
            truncated = true;
            break;
        }
        const NodeInfo& node = nodes[index];
        Json::Object properties;
        for (const char* property : kInspectedProperties) {
            std::optional<Value> value = tree.Read(node.id, property);
            if (value) properties.emplace_back(property, Json(ValueText(*value)));
        }

        Json::Object entry;
        entry.emplace_back("index", Json(static_cast<double>(index)));
        entry.emplace_back("id", Json(std::to_string(node.id)));
        entry.emplace_back("type", Json(node.type));
        entry.emplace_back("fullType", Json(node.fullType));
        entry.emplace_back("name", Json(node.name));
        entry.emplace_back("parent", Json(static_cast<double>(node.parent)));
        entry.emplace_back("depth", Json(static_cast<double>(node.depth)));
        entry.emplace_back("selector", Json(SuggestSelector(nodes, index)));
        entry.emplace_back("properties", Json(std::move(properties)));
        list.push_back(Json(std::move(entry)).Dump());
    }

    return std::string("{\n  \"version\": 1,\n  \"truncated\": ") + (truncated ? "true" : "false") +
           ",\n  \"resources\": " + JsonLines(resources) + ",\n  \"nodes\": " + JsonLines(list) + "\n}\n";
}

}  // namespace amk::theme
