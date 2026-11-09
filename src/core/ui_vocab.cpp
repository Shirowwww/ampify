#include "ui_vocab.hpp"

#include <algorithm>

#include "json.hpp"

namespace ampify::uivocab {

namespace {

constexpr Primitive kPrimitives[] = {
    {.name = "stack", .children = true, .style = StyleKind::Box, .gap = 8},
    {.name = "row", .children = true, .style = StyleKind::Box, .gap = 12},
    {.name = "text", .style = StyleKind::Text, .textAlign = true, .textAttribute = "text"},
    {.name = "button", .click = true, .textAttribute = "label"},
    {.name = "toggle", .change = true, .textAttribute = "label"},
    {.name = "slider", .change = true},
    {.name = "list", .children = true, .gap = 4},
    {.name = "image", .click = true, .style = StyleKind::Image},
    {.name = "icon", .click = true},
    {.name = "card", .children = true, .click = true, .style = StyleKind::Box},
    {.name = "textfield", .change = true},
    {.name = "dropdown", .children = true, .change = true, .onlyChild = "option"},
    {.name = "option", .container = "dropdown", .textAttribute = "label"},
    {.name = "layer", .children = true, .style = StyleKind::Box},
    // A lyrics node reports the start of the line the user picked, in ms.
    {.name = "lyrics", .change = true, .style = StyleKind::Text},
    {.name = "grid", .children = true, .style = StyleKind::Grid},
    {.name = "tabs", .children = true, .onlyChild = "tab", .gap = 4},
    {.name = "tab", .click = true, .container = "tabs"},
    {.name = "menu", .children = true, .onlyChild = "item"},
    {.name = "item", .click = true, .container = "menu"},
    // A bar filled to `value` out of `max`; without a value it moves on its own, for a wait of unknown length.
    {.name = "progress"},
    {.name = "checkbox", .change = true, .textAttribute = "label"},
    {.name = "divider"},
    // Apple's spinning ring, for a wait of unknown length.
    {.name = "spinner"},
};

constexpr Placement kPlacements[] = {
    {"topbar", "ui.topbar", Slot::Bar},
    {"playbar", "ui.playbar", Slot::Bar},
    {"sidebar", "ui.sidebar", Slot::Sidebar},
    {"menu-context", "ui.menu", Slot::Menu},
    {"menu-app", "ui.menu", Slot::Menu},
};

Json NamesOf(std::span<const std::string_view> names) {
    Json::Array list;
    for (std::string_view name : names) list.emplace_back(std::string(name));
    return Json(std::move(list));
}

}  // namespace

std::span<const Primitive> Primitives() { return kPrimitives; }

const Primitive* FindPrimitive(std::string_view name) {
    auto found = std::find_if(std::begin(kPrimitives), std::end(kPrimitives),
                              [&](const Primitive& primitive) { return primitive.name == name; });
    return found == std::end(kPrimitives) ? nullptr : &*found;
}

std::span<const Placement> Placements() { return kPlacements; }

const Placement* FindPlacement(std::string_view name) {
    auto found = std::find_if(std::begin(kPlacements), std::end(kPlacements),
                              [&](const Placement& placement) { return placement.name == name; });
    return found == std::end(kPlacements) ? nullptr : &*found;
}

double DefaultGap(std::string_view primitive, double fallback) {
    const Primitive* found = FindPrimitive(primitive);
    return found && found->gap >= 0 ? found->gap : fallback;
}

std::string VocabularyJson() {
    Json::Array containers;
    Json::Object textAttributes;
    for (const Primitive& primitive : kPrimitives) {
        if (primitive.children) containers.emplace_back(std::string(primitive.name));
        if (!primitive.textAttribute.empty()) {
            textAttributes.emplace_back(std::string(primitive.name), Json(std::string(primitive.textAttribute)));
        }
    }
    Json::Object vocabulary;
    vocabulary.emplace_back("containers", Json(std::move(containers)));
    vocabulary.emplace_back("place", NamesOf(kCommonStyle));
    vocabulary.emplace_back("box", NamesOf(kBoxStyle));
    vocabulary.emplace_back("textAttribute", Json(std::move(textAttributes)));
    vocabulary.emplace_back("maxSubmenuEntries", Json(static_cast<double>(kMaxSubmenuEntries)));
    return Json(std::move(vocabulary)).Dump();
}

}  // namespace ampify::uivocab
