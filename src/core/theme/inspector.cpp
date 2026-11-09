#include "inspector.hpp"

#include <algorithm>
#include <span>

#include "../json.hpp"
#include "properties.hpp"
#include "value_codec.hpp"

namespace ampify::theme {

namespace {

// The properties worth seeing when choosing a rule. Each one is read with Read(), so an element that lacks it is skipped.
constexpr const char* kInspectedProperties[] = {
    "Background", "Foreground", "BorderBrush", "CornerRadius", "FontFamily", "FontSize", "FontWeight",
    "FontStyle", "CharacterSpacing", "LineHeight", "TextAlignment", "TextTrimming", "TextWrapping", "Margin", "Padding", "Width", "Height", "Opacity", "Visibility", "FlowDirection", "HorizontalAlignment",
    "VerticalAlignment", "Grid.Row", "Grid.Column", "Canvas.ZIndex", "TrackHeight", "TrackRectRadius",
    "ThumbVisibility", "States",
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

std::string ValueText(const Value& value, std::string_view property = {}) {
    if (value.kind == ValueKind::Opaque) {
        // An app brush kept as an object shows its colour (opacity included) when it is plain.
        std::string_view colour = HandleColour(value.text);
        return colour.empty() ? "present" : std::string(colour);
    }
    if (value.kind == ValueKind::Template) return "present";
    return ShownText(property, value);
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

Inspection Inspect(TreeBackend& tree, size_t maxLines) {
    Inspection inspection;
    inspection.maxLines = maxLines;
    inspection.window = tree.WindowSize();
    for (const std::string& key : tree.ResourceKeys()) inspection.resources.emplace_back(key, tree.ReadResource(key));
    inspection.nodes = tree.Snapshot();
    const size_t count = std::min(maxLines, inspection.nodes.size());
    inspection.inspected.resize(count);
    for (size_t index = 0; index < count; index++) {
        const NodeId id = inspection.nodes[index].id;
        InspectedNode& node = inspection.inspected[index];
        for (const char* property : kInspectedProperties) {
            if (std::optional<Value> value = tree.Read(id, property)) node.properties.emplace_back(property, *value);
        }
        node.box = tree.Box(id);
    }
    return inspection;
}

std::string InspectionText(const Inspection& inspection) {
    std::string dump;
    // The size @media rules compare with.
    if (const std::optional<ViewSize>& size = inspection.window) {
        dump += "Window: " + FormatNumber(size->width) + " x " + FormatNumber(size->height) + "\n\n";
    }

    dump += "Application resources (" + std::to_string(inspection.resources.size()) + "):\n";
    for (const auto& [key, value] : inspection.resources) {
        dump += "  " + key + " = " + (value ? ValueText(*value) : std::string("(unreadable)")) + "\n";
    }

    const std::vector<NodeInfo>& nodes = inspection.nodes;
    dump += "\nTree (" + std::to_string(nodes.size()) + " elements):\n";
    for (size_t index = 0; index < inspection.inspected.size(); index++) {
        const NodeInfo& node = nodes[index];
        dump += std::string(static_cast<size_t>(node.depth) * 2, ' ') + Label(node);
        if (!node.fullType.empty()) dump += "  (" + node.fullType + ")";
        dump += "  selector: " + SuggestSelector(nodes, static_cast<int>(index));
        for (const auto& [property, value] : inspection.inspected[index].properties) {
            dump += "  " + std::string(property) + "=" + ValueText(value, property);
        }
        dump += '\n';
    }
    if (inspection.Truncated()) dump += "... stopped after " + std::to_string(inspection.maxLines) + " lines\n";
    return dump;
}

std::string InspectTree(TreeBackend& tree, const InspectOptions& options) {
    return InspectionText(Inspect(tree, options.maxLines));
}

std::span<const char* const> InspectedProperties() {
    return kInspectedProperties;
}

namespace {

// The originals the engine holds for one element, as inspector.json writes them.
Json::Object OriginalsOf(NodeId id, const OriginalValues* originals) {
    Json::Object entries;
    if (!originals) return entries;
    for (auto it = originals->lower_bound({id, std::string()}); it != originals->end() && it->first.first == id; ++it) {
        const std::string& property = it->first.second;
        const std::string text = it->second == kNoOwnValue ? std::string(kNoOwnValueText) : ValueText(it->second, property);
        entries.emplace_back(property, Json(text));
    }
    return entries;
}

std::string NodeJson(const Inspection& inspection, int index, const InspectOptions& options) {
    const NodeInfo& node = inspection.nodes[index];
    const InspectedNode& read = inspection.inspected[index];
    Json::Object properties;
    for (const auto& [property, value] : read.properties) {
        properties.emplace_back(property, Json(ValueText(value, property)));
    }

    Json::Object entry;
    entry.emplace_back("index", Json(static_cast<double>(index)));
    entry.emplace_back("id", Json(std::to_string(node.id)));
    entry.emplace_back("type", Json(node.type));
    entry.emplace_back("fullType", Json(node.fullType));
    entry.emplace_back("name", Json(node.name));
    entry.emplace_back("parent", Json(static_cast<double>(node.parent)));
    entry.emplace_back("depth", Json(static_cast<double>(node.depth)));
    entry.emplace_back("selector", Json(SuggestSelector(inspection.nodes, index)));
    entry.emplace_back("icon", Json(node.icon));
    entry.emplace_back("panel", Json(node.panel));
    entry.emplace_back("player", Json(node.player));
    entry.emplace_back("playing", Json(node.playing));
    if (node.checked) entry.emplace_back("checked", Json(true));
    if (node.selected) entry.emplace_back("selected", Json(true));
    if (node.disabled) entry.emplace_back("disabled", Json(true));
    if (const std::optional<NodeBox>& box = read.box) {
        entry.emplace_back("box", Json(Json::Array{Json(box->x), Json(box->y), Json(box->width), Json(box->height)}));
    }
    entry.emplace_back("properties", Json(std::move(properties)));
    Json::Object originals = OriginalsOf(node.id, options.originals);
    if (!originals.empty()) entry.emplace_back("originals", Json(std::move(originals)));
    return Json(std::move(entry)).Dump();
}

}  // namespace

std::string InspectionJson(const Inspection& inspection, const InspectOptions& options) {
    std::vector<std::string> resources;
    for (const auto& [key, value] : inspection.resources) {
        Json::Object entry;
        entry.emplace_back("key", Json(key));
        entry.emplace_back("value", value ? Json(ValueText(*value)) : Json());
        resources.push_back(Json(std::move(entry)).Dump());
    }
    std::vector<std::string> list;
    for (size_t index = 0; index < inspection.inspected.size(); index++) {
        list.push_back(NodeJson(inspection, static_cast<int>(index), options));
    }

    std::string head = std::string("{\n  \"version\": 2,\n  \"truncated\": ") +
                       (inspection.Truncated() ? "true" : "false") + ",\n  \"theme\": " + Json(options.theme).Dump();
    if (const std::optional<ViewSize>& size = inspection.window) {
        Json::Object window;
        window.emplace_back("width", Json(size->width));
        window.emplace_back("height", Json(size->height));
        window.emplace_back("sidebarFolded", Json(size->sidebarFolded));
        head += ",\n  \"window\": " + Json(std::move(window)).Dump();
    }
    return head + ",\n  \"resources\": " + JsonLines(resources) + ",\n  \"nodes\": " + JsonLines(list) + "\n}\n";
}

std::string InspectTreeJson(TreeBackend& tree, const InspectOptions& options) {
    return InspectionJson(Inspect(tree, options.maxLines), options);
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
        auto flag = [&](std::string_view key) {
            const Json* member = item.Find(key);
            return member && member->AsBool() && *member->AsBool();
        };
        node.icon = flag("icon");  // version 2 on: a version 1 tree leaves them all false
        node.panel = flag("panel");
        node.player = flag("player");
        node.playing = flag("playing");
        node.checked = flag("checked");
        node.selected = flag("selected");
        node.disabled = flag("disabled");
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
