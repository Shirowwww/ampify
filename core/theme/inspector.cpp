#include "inspector.hpp"

#include <algorithm>

#include "../json.hpp"
#include "properties.hpp"

namespace ampify::theme {

namespace {

// The properties worth seeing when choosing a rule. Each one is read with Read(), so an element that lacks it is skipped.
constexpr const char* kInspectedProperties[] = {
    "Background", "Foreground", "BorderBrush", "CornerRadius", "FontFamily", "FontSize", "FontWeight",
    "Margin", "Padding", "Width", "Height", "Opacity", "Visibility", "FlowDirection", "HorizontalAlignment",
    "VerticalAlignment", "Grid.Row", "Grid.Column", "Canvas.ZIndex", "States",
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
    if (value.kind == ValueKind::Opaque) {
        // "handle:N;#AARRGGBB": an app brush kept as an object, with its colour (opacity included) when it is plain.
        size_t colour = value.text.find(';');
        return colour == std::string::npos ? "present" : value.text.substr(colour + 1);
    }
    if (value.kind == ValueKind::Template) return "present";
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
    // The size @media rules compare with.
    if (std::optional<ViewSize> size = tree.WindowSize()) {
        dump += "Window: " + FormatNumber(size->width) + " x " + FormatNumber(size->height) + "\n\n";
    }

    std::vector<std::string> keys = tree.ResourceKeys();
    dump += "Application resources (" + std::to_string(keys.size()) + "):\n";
    for (const std::string& key : keys) {
        std::optional<Value> value = tree.ReadResource(key);
        dump += "  " + key + " = " + (value ? ValueText(*value) : std::string("(unreadable)")) + "\n";
    }

    std::vector<NodeInfo> nodes = tree.Snapshot();
    dump += "\nTree (" + std::to_string(nodes.size()) + " elements):\n";

    size_t lines = 0;
    for (int index = 0; index < static_cast<int>(nodes.size()); index++) {
        if (lines++ >= options.maxLines) {
            dump += "... stopped after " + std::to_string(options.maxLines) + " lines\n";
            break;
        }

        const NodeInfo& node = nodes[index];
        dump += std::string(static_cast<size_t>(node.depth) * 2, ' ') + Label(node);
        if (!node.fullType.empty()) dump += "  (" + node.fullType + ")";
        dump += "  selector: " + SuggestSelector(nodes, index);

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

std::optional<std::vector<NodeInfo>> ReadInspectorTree(std::string_view text, std::string* problem) {
    auto fail = [&](std::string reason) -> std::optional<std::vector<NodeInfo>> {
        if (problem) *problem = std::move(reason);
        return std::nullopt;
    };
    std::string error;
    std::optional<Json> json = Json::Parse(text, &error);
    if (!json) return fail("not JSON: " + error);
    const Json* list = json->Find("nodes");
    if (!list || !list->AsArray()) return fail("no \"nodes\" list: not an inspector.json");

    auto textOf = [](const Json& item, std::string_view key) {
        const Json* member = item.Find(key);
        const std::string* value = member ? member->AsString() : nullptr;
        return value ? *value : std::string();
    };
    std::vector<NodeInfo> nodes;
    std::vector<int> children;  // per node, its children so far
    for (const Json& item : *list->AsArray()) {
        const Json* parent = item.Find("parent");
        const Json* depth = item.Find("depth");
        if (!parent || !parent->AsNumber() || !depth || !depth->AsNumber()) {
            return fail("node " + std::to_string(nodes.size()) + " has no parent or depth");
        }
        NodeInfo node;
        node.id = nodes.size() + 1;
        node.type = textOf(item, "type");
        node.fullType = textOf(item, "fullType");
        node.name = textOf(item, "name");
        node.parent = static_cast<int>(*parent->AsNumber());
        node.depth = static_cast<int>(*depth->AsNumber());
        if (node.parent < -1 || node.parent >= static_cast<int>(nodes.size())) {
            return fail("node " + std::to_string(nodes.size()) + " comes before its parent");
        }
        node.position = node.parent < 0 ? 0 : children[node.parent]++;
        nodes.push_back(std::move(node));
        children.push_back(0);
    }
    for (NodeInfo& node : nodes) node.siblings = node.parent < 0 ? 1 : children[node.parent];
    return nodes;
}

}  // namespace ampify::theme
