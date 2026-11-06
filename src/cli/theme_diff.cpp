#include <array>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "replay_util.hpp"
#include "theme_replay.hpp"

namespace ampify::theme {

using namespace replay;

namespace {

// node:property → {before, after, element}
std::map<std::pair<int, std::string>, std::array<std::string, 3>> FinalValues(const Json& report) {
    std::map<std::pair<int, std::string>, std::array<std::string, 3>> values;
    for (const Json& item : ArrayOf(report, "final")) {
        const Json* node = item.Find("node");
        const Json* list = item.Find("values");
        if (!node || !node->AsNumber() || !list || !list->AsObject()) continue;
        for (const auto& [property, value] : *list->AsObject()) {
            values[{static_cast<int>(*node->AsNumber()), property}] = {Str(value, "before"), Str(value, "after"),
                                                                       Str(item, "element")};
        }
    }
    return values;
}

std::set<std::string> Texts(const Json& report, std::string_view key, const std::vector<std::string_view>& fields) {
    std::set<std::string> texts;
    for (const Json& item : ArrayOf(report, key)) {
        std::string text;
        for (std::string_view field : fields) text += (text.empty() ? "" : " | ") + Str(item, field);
        texts.insert(text);
    }
    return texts;
}

// "node element::before" → property → value, the image under "Content".
std::map<std::string, std::map<std::string, std::string>> DecorValues(const Json& report) {
    std::map<std::string, std::map<std::string, std::string>> decors;
    for (const Json& item : ArrayOf(report, "decors")) {
        auto& values = decors[Str(item, "node") + " " + Str(item, "element") + Str(item, "slot")];
        values[std::string(kContentProperty)] = Str(item, "image");
        const Json* list = item.Find("values");
        if (!list || !list->AsObject()) continue;
        for (const auto& [property, value] : *list->AsObject()) {
            values[property] = value.AsString() ? *value.AsString() : value.Dump();
        }
    }
    return decors;
}

// Each value of a ::before or ::after that differs, "(none)" on the side without that decor or value.
Json DecorDiff(const Json& a, const Json& b) {
    auto decorsA = DecorValues(a);
    auto decorsB = DecorValues(b);
    std::set<std::string> keys;
    for (const auto& [key, values] : decorsA) keys.insert(key);
    for (const auto& [key, values] : decorsB) keys.insert(key);
    static const std::map<std::string, std::string> kNone;
    Json::Array list;
    for (const std::string& key : keys) {
        const auto& left = decorsA.count(key) ? decorsA[key] : kNone;
        const auto& right = decorsB.count(key) ? decorsB[key] : kNone;
        std::set<std::string> properties;
        for (const auto& [property, value] : left) properties.insert(property);
        for (const auto& [property, value] : right) properties.insert(property);
        for (const std::string& property : properties) {
            auto inA = left.find(property);
            auto inB = right.find(property);
            std::string before = inA == left.end() ? "(none)" : inA->second;
            std::string after = inB == right.end() ? "(none)" : inB->second;
            if (before == after) continue;
            Json::Object item;
            item.emplace_back("decor", Json(key));
            item.emplace_back("property", Json(property));
            item.emplace_back("before", Json(before));
            item.emplace_back("after", Json(after));
            list.push_back(Json(std::move(item)));
        }
    }
    return Json(std::move(list));
}

Json OnlyIn(const std::set<std::string>& mine, const std::set<std::string>& theirs) {
    Json::Array list;
    for (const std::string& text : mine) {
        if (!theirs.count(text)) list.push_back(Json(text));
    }
    return Json(std::move(list));
}

}  // namespace

std::optional<std::string> DiffReports(const std::string& before, const std::string& after, bool& same,
                                       std::string* problem) {
    std::optional<Json> a = Json::Parse(before);
    std::optional<Json> b = Json::Parse(after);
    if (!a || !b || !a->Find("final") || !b->Find("final")) {
        if (problem) *problem = "not a report.json of ampify theme replay";
        return std::nullopt;
    }
    auto valuesA = FinalValues(*a);
    auto valuesB = FinalValues(*b);
    std::set<std::pair<int, std::string>> keys;
    for (const auto& [key, value] : valuesA) keys.insert(key);
    for (const auto& [key, value] : valuesB) keys.insert(key);
    Json::Array values;
    for (const auto& key : keys) {
        auto inA = valuesA.find(key);
        auto inB = valuesB.find(key);
        // An element only one side changed shows Apple's value on the other.
        std::string left = inA != valuesA.end() ? inA->second[1] : inB->second[0];
        std::string right = inB != valuesB.end() ? inB->second[1] : inA->second[0];
        if (left == right) continue;
        Json::Object item;
        item.emplace_back("node", Json(static_cast<double>(key.first)));
        item.emplace_back("element", Json(inA != valuesA.end() ? inA->second[2] : inB->second[2]));
        item.emplace_back("property", Json(key.second));
        item.emplace_back("before", Json(left));
        item.emplace_back("after", Json(right));
        values.push_back(Json(std::move(item)));
    }

    std::map<std::string, std::pair<double, double>> counts;  // selector | property: matches before, after
    for (int side = 0; side < 2; side++) {
        for (const Json& rule : ArrayOf(side ? *b : *a, "rules")) {
            const Json* count = rule.Find("count");
            auto& entry = counts[Str(rule, "selector") + " | " + Str(rule, "property")];
            (side ? entry.second : entry.first) += count && count->AsNumber() ? *count->AsNumber() : 0;
        }
    }
    Json::Array matches;
    for (const auto& [rule, pair] : counts) {
        if (pair.first == pair.second) continue;
        Json::Object item;
        item.emplace_back("rule", Json(rule));
        item.emplace_back("before", Json(pair.first));
        item.emplace_back("after", Json(pair.second));
        matches.push_back(Json(std::move(item)));
    }

    const std::vector<std::string_view> refusal{"element", "property", "reason", "count"};
    const std::vector<std::string_view> selector{"selector"};
    const std::vector<std::string_view> hover{"element", "values"};
    Json::Object diff;
    diff.emplace_back("values", Json(std::move(values)));
    diff.emplace_back("matches", Json(std::move(matches)));
    diff.emplace_back("decors", DecorDiff(*a, *b));
    diff.emplace_back("refusedBefore", OnlyIn(Texts(*a, "refused", refusal), Texts(*b, "refused", refusal)));
    diff.emplace_back("refusedAfter", OnlyIn(Texts(*b, "refused", refusal), Texts(*a, "refused", refusal)));
    diff.emplace_back("unmatchedBefore", OnlyIn(Texts(*a, "unmatched", selector), Texts(*b, "unmatched", selector)));
    diff.emplace_back("unmatchedAfter", OnlyIn(Texts(*b, "unmatched", selector), Texts(*a, "unmatched", selector)));
    diff.emplace_back("hoverBefore", OnlyIn(Texts(*a, "hover", hover), Texts(*b, "hover", hover)));
    diff.emplace_back("hoverAfter", OnlyIn(Texts(*b, "hover", hover), Texts(*a, "hover", hover)));
    same = true;
    for (const auto& [key, value] : diff) same = same && value.AsArray()->empty();
    std::string text = "{";
    for (size_t i = 0; i < diff.size(); i++) {
        text += std::string(i ? ",\n  " : "\n  ") + Json::Quote(diff[i].first) + ": " + Lines(*diff[i].second.AsArray());
    }
    return text + "\n}\n";
}

}  // namespace ampify::theme
