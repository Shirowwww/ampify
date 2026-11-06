#include "dump_scrub.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <vector>

#include "../core/json.hpp"
#include "../core/theme/inspector.hpp"

namespace ampify::cli {

namespace {

constexpr const char* kText = "<text>";
constexpr const char* kAddress = "<address>";

// The members of a node that hold a person's text when a backend records them. Addresses (Source, Uri...) go through
// ScrubAddresses instead, which keeps the app's own files.
bool IsTextKey(std::string_view key) {
    static constexpr std::string_view kKeys[] = {"text",  "Text",    "label",       "Label",          "title",
                                                 "Title", "subtitle", "Subtitle",   "ToolTip",        "tooltip",
                                                 "AutomationName", "automationName", "description", "Description",
                                                 "Content", "content", "Header", "PlaceholderText"};
    return std::find(std::begin(kKeys), std::end(kKeys), key) != std::end(kKeys);
}

// x:Name as developers write it: no space, no accent, not a sentence. A playlist or a person's name is none of those.
bool IsIdentifier(std::string_view text) {
    if (text.empty() || text.size() > 80) return false;
    if (!std::isalpha(static_cast<unsigned char>(text[0])) && text[0] != '_') return false;
    return std::all_of(text.begin(), text.end(), [](char c) {
        unsigned char u = static_cast<unsigned char>(c);
        return u < 0x80 && (std::isalnum(u) || c == '_' || c == '-' || c == '.');
    });
}

// A class name: an identifier, or a generic one (`Foo`1<Bar>`).
bool IsTypeName(std::string_view text) {
    return text.empty() || std::all_of(text.begin(), text.end(), [](char c) {
               unsigned char u = static_cast<unsigned char>(c);
               return u < 0x80 && (std::isalnum(u) || c == '_' || c == '.' || c == '`' || c == '<' || c == '>' || c == ',' || c == ' ');
           });
}

bool StartsAt(std::string_view text, size_t at, std::string_view word) {
    return text.substr(at, word.size()) == word;
}

// Replaces each address in a value: a URL (but the app's own ms-appx:/// files), a drive path or a rooted path. A font
// keeps its family after '#', an image its fill after '|'.
std::string ScrubAddresses(std::string_view text, ScrubCounts& counts) {
    const bool drive = text.size() > 2 && std::isalpha(static_cast<unsigned char>(text[0])) && text[1] == ':' &&
                       (text[2] == '/' || text[2] == '\\');
    const size_t scheme = text.find("://");
    const bool rooted = text.starts_with('/') || text.starts_with('\\');
    if (!drive && scheme == std::string_view::npos && !rooted) return std::string(text);
    if (StartsAt(text, 0, "ms-appx:///") || (rooted && text.find("/Assets/") != std::string_view::npos &&
                                              text.find("Users") == std::string_view::npos)) {
        return std::string(text);  // a file of Apple Music's own package
    }
    counts.addresses++;
    size_t keep = std::min(text.find('#'), text.find('|'));
    return std::string(kAddress) + (keep == std::string_view::npos ? std::string() : std::string(text.substr(keep)));
}

std::string ScrubValue(std::string_view text, const Identity& who, ScrubCounts& counts) {
    std::string scrubbed = ScrubAddresses(text, counts);
    std::string masked = Anonymize(scrubbed, who);
    if (masked != scrubbed) counts.masked++;
    return masked;
}

// Every string of an object scrubbed as a value; texts by their key.
Json ScrubObject(const Json::Object& object, const Identity& who, ScrubCounts& counts) {
    Json::Object result;
    for (const auto& [key, value] : object) {
        if (const std::string* text = value.AsString()) {
            if (IsTextKey(key) && !text->empty()) {
                counts.texts++;
                result.emplace_back(key, Json(kText));
            } else {
                result.emplace_back(key, Json(ScrubValue(*text, who, counts)));
            }
        } else if (value.AsObject()) {
            result.emplace_back(key, ScrubObject(*value.AsObject(), who, counts));
        } else {
            result.emplace_back(key, value.AsArray() ? Json() : value);  // no list is expected here
        }
    }
    return Json(std::move(result));
}

std::string Lines(const std::vector<std::string>& items) {
    if (items.empty()) return "[]";
    std::string text = "[";
    for (size_t i = 0; i < items.size(); i++) text += (i ? ",\n    " : "\n    ") + items[i];
    return text + "\n  ]";
}

}  // namespace

std::optional<std::string> ScrubDump(std::string_view text, const Identity& who, ScrubCounts& counts, std::string* problem) {
    std::string error;
    std::optional<Json> json = Json::Parse(text, &error);
    const Json* list = json ? json->Find("nodes") : nullptr;
    if (!list || !list->AsArray()) {
        if (problem) *problem = json ? "no \"nodes\" list: not an inspector.json" : "not JSON: " + error;
        return std::nullopt;
    }

    std::map<std::string, std::string> renamed;  // the same free name gets the same placeholder
    std::vector<theme::NodeInfo> infos;
    std::vector<Json::Object> nodes;
    for (const Json& item : *list->AsArray()) {
        if (!item.AsObject()) continue;
        Json::Object node;
        theme::NodeInfo info;
        for (const auto& [key, value] : *item.AsObject()) {
            const std::string* string = value.AsString();
            if (key == "id") {
                node.emplace_back(key, Json(std::to_string(infos.size())));
            } else if (key == "selector") {
                node.emplace_back(key, Json());  // rebuilt below
            } else if (key == "name" && string) {
                std::string name = *string;
                if (!name.empty() && !IsIdentifier(name)) {
                    auto [it, fresh] = renamed.try_emplace(name, "name" + std::to_string(renamed.size() + 1));
                    if (fresh) counts.names++;
                    name = it->second;
                }
                info.name = name;
                node.emplace_back(key, Json(name));
            } else if ((key == "type" || key == "fullType") && string) {
                std::string type = IsTypeName(*string) ? *string : std::string("Type");
                if (key == "type") info.type = type;
                node.emplace_back(key, Json(type));
            } else if (key == "parent" && value.AsNumber()) {
                info.parent = static_cast<int>(*value.AsNumber());
                node.emplace_back(key, value);
            } else if (string) {
                if (IsTextKey(key) && !string->empty()) counts.texts++;
                node.emplace_back(key, IsTextKey(key) && !string->empty() ? Json(kText) : Json(ScrubValue(*string, who, counts)));
            } else if (value.AsObject()) {
                node.emplace_back(key, ScrubObject(*value.AsObject(), who, counts));
            } else {
                node.emplace_back(key, value);  // numbers, flags, the box
            }
        }
        if (info.parent >= static_cast<int>(infos.size())) info.parent = -1;
        infos.push_back(std::move(info));
        nodes.push_back(std::move(node));
    }

    std::vector<std::string> lines;
    for (size_t i = 0; i < nodes.size(); i++) {
        for (auto& [key, value] : nodes[i]) {
            if (key == "selector") value = Json(theme::SuggestSelector(infos, static_cast<int>(i)));
        }
        lines.push_back(Json(std::move(nodes[i])).Dump());
    }
    std::vector<std::string> resources;
    if (const Json* entries = json->Find("resources"); entries && entries->AsArray()) {
        for (const Json& entry : *entries->AsArray()) {
            if (!entry.AsObject()) continue;
            resources.push_back(ScrubObject(*entry.AsObject(), who, counts).Dump());
        }
    }

    std::string out = "{\n  \"version\": ";
    const Json* version = json->Find("version");
    out += version && version->AsNumber() ? version->Dump() : std::string("1");
    out += ",\n  \"scrubbed\": true";
    for (std::string_view key : {"truncated", "window"}) {
        if (const Json* member = json->Find(key)) out += ",\n  " + Json::Quote(key) + ": " + member->Dump();
    }
    if (const Json* theme = json->Find("theme"); theme && theme->AsString()) {
        out += ",\n  \"theme\": " + Json(IsIdentifier(*theme->AsString()) || theme->AsString()->empty()
                                              ? *theme->AsString() : std::string("theme")).Dump();
    }
    return out + ",\n  \"resources\": " + Lines(resources) + ",\n  \"nodes\": " + Lines(lines) + "\n}\n";
}

}  // namespace ampify::cli
