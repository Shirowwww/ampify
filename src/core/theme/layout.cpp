#include "layout.hpp"

#include <algorithm>
#include <cctype>
#include <set>

#include "../json.hpp"
#include "selector.hpp"

namespace ampify::theme {

namespace {

bool IsElementName(std::string_view name) {
    if (name.empty() || name.size() > 128) return false;
    return std::all_of(name.begin(), name.end(), [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-';
    });
}

}  // namespace

std::string_view LayoutFileName(LayoutKind kind) {
    return kind == LayoutKind::Sidebar ? "sidebar.json" : "home.json";
}

std::optional<LayoutConfig> ParseLayout(std::string_view json, std::vector<std::string>& problems) {
    std::string error;
    std::optional<Json> document = Json::Parse(json, &error);
    if (!document || !document->IsObject()) {
        problems.push_back("invalid JSON" + (error.empty() ? std::string() : ": " + error));
        return std::nullopt;
    }
    const Json* items = document->Find("items");
    if (!items || !items->IsArray()) {
        problems.push_back("list \"items\" missing");
        return std::nullopt;
    }

    LayoutConfig config;
    if (const Json* reorder = document->Find("reorder")) {
        if (reorder->AsBool()) {
            config.reorder = *reorder->AsBool();
        } else {
            problems.push_back("\"reorder\" must be true or false, false is used");
        }
    }

    std::set<std::string> seen;
    int index = 0;
    for (const Json& item : *items->AsArray()) {
        index++;
        const Json* name = item.Find("name");
        if (!item.IsObject() || !name || !name->AsString() || !IsElementName(*name->AsString())) {
            problems.push_back("element " + std::to_string(index) + ": \"name\" must be an x:Name (letters, digits, _ and -), ignored");
            continue;
        }
        if (!seen.insert(*name->AsString()).second) {
            problems.push_back("element " + std::to_string(index) + ": \"" + *name->AsString() + "\" is a duplicate, ignored");
            continue;
        }
        LayoutItem entry;
        entry.name = *name->AsString();
        if (const Json* hidden = item.Find("hidden")) {
            if (hidden->AsBool()) {
                entry.hidden = *hidden->AsBool();
            } else {
                problems.push_back("element " + std::to_string(index) + ": \"hidden\" must be true or false");
            }
        }
        config.items.push_back(std::move(entry));
    }
    return config;
}

std::string WriteLayout(const LayoutConfig& config) {
    std::string text = "{\n  \"version\": 1,\n  \"reorder\": ";
    text += config.reorder ? "true" : "false";
    text += ",\n  \"items\": [";
    for (size_t i = 0; i < config.items.size(); i++) {
        const LayoutItem& item = config.items[i];
        text += i ? ",\n" : "\n";
        text += "    {\"name\": " + Json(item.name).Dump() + ", \"hidden\": " + (item.hidden ? "true" : "false") + "}";
    }
    text += config.items.empty() ? "]\n}\n" : "\n  ]\n}\n";
    return text;
}

LayoutConfig GenerateLayout(const std::optional<LayoutConfig>& existing, const std::vector<std::string>& names) {
    LayoutConfig config;
    config.reorder = true;
    std::set<std::string> held;
    if (existing) {
        config = *existing;
        for (const LayoutItem& item : config.items) held.insert(item.name);
    }
    for (const std::string& name : names) {
        if (!IsElementName(name) || !held.insert(name).second) continue;
        config.items.push_back({name, false});
    }
    return config;
}

std::vector<std::string> NamesMatching(const std::vector<NodeInfo>& nodes, const Selector& selector) {
    std::vector<std::string> names;
    std::set<std::string> seen;
    for (int index = 0; index < static_cast<int>(nodes.size()); index++) {
        const std::string& name = nodes[index].name;
        if (name.empty() || !Matches(selector, nodes, index)) continue;
        if (seen.insert(name).second) names.push_back(name);
    }
    return names;
}

void AddLayoutRules(const LayoutConfig& config, LayoutKind kind, Theme& out) {
    const std::string type = kind == LayoutKind::Sidebar ? "NavigationViewItem" : "";
    const std::string source(LayoutFileName(kind));
    int order = static_cast<int>(out.assignments.size());

    for (size_t index = 0; index < config.items.size(); index++) {
        const LayoutItem& item = config.items[index];
        std::optional<Selector> selector = ParseSelector(type + "#" + item.name);
        if (!selector) continue;  // ParseLayout only keeps names a selector accepts

        auto add = [&](std::string property, Value value) {
            Assignment assignment{*selector, SpecificityOf(*selector), std::move(property), std::move(value), false,
                                  order++, static_cast<int>(index) + 1};
            assignment.source = source;
            out.assignments.push_back(std::move(assignment));
        };
        if (item.hidden) add("Visibility", {ValueKind::Visibility, "Collapsed"});
        if (config.reorder) add(std::string(kOrderProperty), {ValueKind::Number, std::to_string(index)});
    }
}

}  // namespace ampify::theme
