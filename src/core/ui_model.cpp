#include "ui_model.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <utility>

#include "ui_render/style.hpp"

namespace ampify {

namespace {

constexpr int kMaxTreeDepth = 8;
constexpr int kMaxTreeNodes = 300;
constexpr size_t kMaxText = 500;

bool Fail(std::string* error, const std::string& message) {
    if (error) *error = message;
    return false;
}

constexpr double kMaxTooltipDelayMs = 10000;

template <size_t N>
bool OneOf(const std::array<std::string_view, N>& names, std::string_view name) {
    return std::find(names.begin(), names.end(), name) != names.end();
}

bool IsPrimitive(const std::string& name) {
    static const std::array<std::string_view, 20> kPrimitives = {
        "stack", "row", "text", "button", "toggle", "slider", "list", "image", "icon", "card", "textfield",
        "dropdown", "option", "layer", "lyrics", "grid", "tabs", "tab", "menu", "item"};
    return OneOf(kPrimitives, name);
}

bool HasChildren(const std::string& name) {
    static const std::array<std::string_view, 9> kContainers = {"stack", "row", "list", "card", "dropdown", "layer",
                                                                "grid", "tabs", "menu"};
    return OneOf(kContainers, name);
}

// The node a container holds and nothing else: options in a dropdown, tabs in tabs, items in a menu.
std::string_view OnlyChildOf(const std::string& parent) {
    if (parent == "dropdown") return "option";
    if (parent == "tabs") return "tab";
    if (parent == "menu") return "item";
    return {};
}

// The container a node must sit in: option, tab and item belong to one only.
std::string_view ContainerOf(const std::string& name) {
    if (name == "option") return "dropdown";
    if (name == "tab") return "tabs";
    if (name == "item") return "menu";
    return {};
}

// The containers that take a background, a padding, rounded corners and a size.
bool IsBox(const std::string& name) {
    static const std::array<std::string_view, 4> kBoxes = {"stack", "row", "layer", "card"};
    return OneOf(kBoxes, name);
}

constexpr double kMaxLength = 10000;    // width, height, minWidth, maxWidth, in pixels
constexpr double kMaxCorner = 1000;
constexpr double kMaxScale = 10;
constexpr double kMinTextSize = 8;
constexpr double kMaxTextSize = 120;
constexpr double kMaxLines = 100;

bool NumberIn(const Json& value, double low, double high) {
    const double* number = value.AsNumber();
    return number && std::isfinite(*number) && *number >= low && *number <= high;
}

bool WholeIn(const Json& value, double low, double high) {
    return NumberIn(value, low, high) && std::floor(*value.AsNumber()) == *value.AsNumber();
}

template <size_t N>
bool TextOneOf(const Json& value, const std::array<std::string_view, N>& names) {
    const std::string* text = value.AsString();
    return text && OneOf(names, *text);
}

// The style attributes that every node takes: its opacity, scale, transitions and place in its parent.
std::optional<bool> CheckCommonStyle(const std::string& name, const std::string& key, const Json& value,
                                     std::string* error) {
    static const std::array<std::string_view, 4> kPlaces = {"start", "center", "end", "stretch"};
    static const std::array<std::string_view, 3> kTextAligns = {"left", "center", "right"};
    if (key == "opacity") {
        if (!NumberIn(value, 0, 1)) return Fail(error, "opacity expects a number between 0 and 1");
    } else if (key == "scale") {
        if (!NumberIn(value, 0, kMaxScale)) return Fail(error, "scale expects a number between 0 and 10");
    } else if (key == "transition") {
        const std::string* text = value.AsString();
        if (!text || !uirender::ParseTransition(*text)) {
            return Fail(error, "transition expects \"<opacity|scale|translation|all> <duration>ms\", comma-separated "
                               "(10 s at most), or \"none\"");
        }
    } else if (key == "valign") {
        if (!TextOneOf(value, kPlaces)) return Fail(error, "valign expects start, center, end or stretch");
    } else if (key == "grow") {
        if (!value.AsBool()) return Fail(error, "grow expects true or false");
    } else if (key == "align") {
        if (name == "text" && !TextOneOf(value, kTextAligns)) return Fail(error, "align on text expects left, center or right");
        if (name != "text" && !TextOneOf(value, kPlaces)) return Fail(error, "align expects start, center, end or stretch");
    } else {
        return std::nullopt;  // not a common style attribute
    }
    return true;
}

bool CheckBoxStyle(const std::string& name, const std::string& key, const Json& value, std::string* error) {
    bool box = IsBox(name);
    bool image = name == "image";
    if ((box || image) && key == "cornerRadius") {
        if (!NumberIn(value, 0, kMaxCorner)) return Fail(error, "cornerRadius expects a number between 0 and 1000");
    } else if ((box && (key == "minWidth" || key == "maxWidth")) || ((box || image) && (key == "width" || key == "height"))) {
        if (!NumberIn(value, 0, kMaxLength)) return Fail(error, key + " expects a number of pixels between 0 and 10000");
    } else if (box && key == "background") {
        const std::string* text = value.AsString();
        if (!text || !uirender::ParseBackground(*text)) {
            return Fail(error, "background expects #RRGGBB, #AARRGGBB, acrylic or acrylic(#AARRGGBB, 0.6)");
        }
    } else if (box && key == "padding") {
        if (!uirender::ParsePadding(value)) {
            return Fail(error, "padding expects a number or \"top,right,bottom,left\" (0 to 1000 each)");
        }
    } else if (image && key == "fit") {
        static const std::array<std::string_view, 3> kFits = {"cover", "contain", "fill"};
        if (!TextOneOf(value, kFits)) return Fail(error, "fit expects cover, contain or fill");
    }
    return true;
}

bool CheckTextStyle(const std::string& key, const Json& value, std::string* error) {
    if (key == "size") {
        if (!NumberIn(value, kMinTextSize, kMaxTextSize)) return Fail(error, "size expects a number of pixels between 8 and 120");
    } else if (key == "weight") {
        if (!WholeIn(value, 100, 900)) return Fail(error, "weight expects a whole number between 100 and 900");
    } else if (key == "color") {
        const std::string* text = value.AsString();
        if (!text || !uirender::ParseColor(*text)) return Fail(error, "color expects #RRGGBB or #AARRGGBB");
    } else if (key == "wrap") {
        if (!value.AsBool()) return Fail(error, "wrap expects true or false");
    } else if (key == "maxLines") {
        if (!WholeIn(value, 0, kMaxLines)) return Fail(error, "maxLines expects a whole number between 0 and 100");
    }
    return true;
}

// The space between the cells of a grid, in pixels: a Grid spacing beyond it is not a layout anyone wants.
constexpr double kMaxGridGap = 1000;

bool CheckGridStyle(const std::string& key, const Json& value, std::string* error) {
    if (key == "gap") {
        if (!NumberIn(value, 0, kMaxGridGap)) return Fail(error, "gap expects a number of pixels between 0 and 1000");
        return true;
    }
    if (key != "columns") return true;
    if (!WholeIn(value, 1, uirender::kMaxGridColumns)) {
        return Fail(error, "columns expects a whole number between 1 and 12");
    }
    return true;
}

bool CheckStyle(const std::string& name, const std::string& key, const Json& value, std::string* error) {
    if (std::optional<bool> common = CheckCommonStyle(name, key, value, error)) return *common;
    if (name == "text" || name == "lyrics") return CheckTextStyle(key, value, error);
    if (name == "grid") return CheckGridStyle(key, value, error);
    return CheckBoxStyle(name, key, value, error);
}

// The primitives that report a click, and those that report a new value.
bool TakesClick(const std::string& name) {
    static const std::array<std::string_view, 6> kClickable = {"button", "card", "image", "icon", "tab", "item"};
    return OneOf(kClickable, name);
}

bool TakesChange(const std::string& name) {
    // A lyrics node reports the start of the line the user picked, in ms.
    static const std::array<std::string_view, 5> kEditable = {"toggle", "slider", "textfield", "dropdown", "lyrics"};
    return OneOf(kEditable, name);
}

bool IsCallbackId(const Json& value) {
    const double* number = value.AsNumber();
    return number && *number >= 1 && *number <= 2147483647 && std::floor(*number) == *number;
}

// The attributes whose value is checked beyond "a scalar".
bool CheckAttribute(const std::string& name, const std::string& key, const Json& value, std::string* error) {
    if (key == "onClick" || key == "onChange") {
        bool allowed = key == "onClick" ? TakesClick(name) : TakesChange(name);
        if (!allowed) return Fail(error, key + " is not allowed on " + name);
        if (!IsCallbackId(value)) return Fail(error, key + " must be a callback id (a positive whole number)");
    } else if (key == "tooltip") {
        if (!value.IsString()) return Fail(error, "tooltip must be a text");
    } else if (key == "tooltipPlacement") {
        static const std::array<std::string_view, 4> kPlacements = {"top", "bottom", "left", "right"};
        const std::string* placement = value.AsString();
        if (!placement || !OneOf(kPlacements, *placement)) {
            return Fail(error, "tooltipPlacement expects top, bottom, left or right");
        }
    } else if (key == "tooltipDelay") {
        const double* delay = value.AsNumber();
        if (!delay || *delay < 0 || *delay > kMaxTooltipDelayMs) {
            return Fail(error, "tooltipDelay expects a number of milliseconds between 0 and 10000");
        }
    }
    return CheckStyle(name, key, value, error);
}

bool CheckNode(const Json& node, const std::string& parent, int depth, int& nodes, std::string* error) {
    if (++nodes > kMaxTreeNodes) return Fail(error, "interface too large (300 elements at most)");
    if (depth > kMaxTreeDepth) return Fail(error, "interface too deep (8 levels at most)");
    const Json::Object* attributes = node.AsObject();
    if (!attributes) return Fail(error, "each element of the interface must be an object");

    const Json* type = node.Find("type");
    const std::string* name = type ? type->AsString() : nullptr;
    if (!name) return Fail(error, "element without a type");
    if (!IsPrimitive(*name)) return Fail(error, "unknown primitive: " + *name);
    std::string_view container = ContainerOf(*name);
    if (!container.empty() && container != std::string_view(parent)) {
        return Fail(error, *name + " outside " + std::string(container));
    }
    std::string_view only = OnlyChildOf(parent);
    if (!only.empty() && std::string_view(*name) != only) {
        return Fail(error, parent + " only holds " + std::string(only) + " nodes");
    }

    for (const auto& [key, value] : *attributes) {
        if (key == "children") continue;
        if (value.AsArray() || value.AsObject()) return Fail(error, "attribute not allowed: " + key);
        if (const std::string* text = value.AsString()) {
            if (text->size() > kMaxText) return Fail(error, "text too long in " + key + " (500 characters at most)");
        }
        if (!CheckAttribute(*name, key, value, error)) return false;
    }

    const Json* children = node.Find("children");
    if (!children) return true;
    const Json::Array* items = children->AsArray();
    if (!items) return Fail(error, "children must be a list");
    if (!items->empty() && !HasChildren(*name)) return Fail(error, "primitive " + *name + " cannot have children");
    for (const Json& child : *items) {
        if (!CheckNode(child, *name, depth + 1, nodes, error)) return false;
    }
    return true;
}

}  // namespace

int UiModel::AddControl(const std::string& elementId, const std::string& placement, const std::string& label,
                        const std::string& icon, bool clickable, int parent) {
    if (!IsPlacement(placement)) return 0;
    std::lock_guard<std::mutex> guard(m_mutex);
    if (parent != 0) {
        auto submenu = std::find_if(m_controls.begin(), m_controls.end(), [&](const UiControl& other) {
            return other.id == parent && other.elementId == elementId && other.placement == placement &&
                   !other.clickable && other.parent == 0;
        });
        if (submenu == m_controls.end() || !clickable) return 0;
    }
    UiControl control;
    control.parent = parent;
    control.id = m_nextId++;
    control.elementId = elementId;
    control.placement = placement;
    control.label = label;
    control.icon = icon;
    control.clickable = clickable;
    m_controls.push_back(control);
    return control.id;
}

void UiModel::SetPanel(const std::string& elementId, const std::string& panelId, Json tree) {
    std::lock_guard<std::mutex> guard(m_mutex);
    auto same = [&](const UiSlot& slot) { return slot.elementId == elementId && slot.id == panelId; };
    auto found = std::find_if(m_panels.begin(), m_panels.end(), same);
    if (found != m_panels.end()) {
        found->tree = std::move(tree);
    } else {
        m_panels.push_back(UiSlot{elementId, panelId, std::move(tree)});
    }
}

bool UiModel::RemovePanel(const std::string& elementId, const std::string& panelId) {
    std::lock_guard<std::mutex> guard(m_mutex);
    auto same = [&](const UiSlot& slot) { return slot.elementId == elementId && slot.id == panelId; };
    auto found = std::find_if(m_panels.begin(), m_panels.end(), same);
    if (found == m_panels.end()) return false;
    m_panels.erase(found);
    return true;
}

void UiModel::SetPage(const std::string& elementId, const std::string& path, Json tree) {
    std::lock_guard<std::mutex> guard(m_mutex);
    auto same = [&](const UiSlot& slot) { return slot.elementId == elementId && slot.id == path; };
    auto found = std::find_if(m_pages.begin(), m_pages.end(), same);
    if (found != m_pages.end()) {
        found->tree = std::move(tree);
    } else {
        m_pages.push_back(UiSlot{elementId, path, std::move(tree)});
    }
}

void UiModel::OpenPopup(const std::string& elementId, Json tree) {
    std::lock_guard<std::mutex> guard(m_mutex);
    m_popups.push_back(UiSlot{elementId, {}, std::move(tree)});
}

void UiModel::SetPopup(const std::string& elementId, const std::string& popupId, Json tree) {
    std::lock_guard<std::mutex> guard(m_mutex);
    auto same = [&](const UiSlot& slot) { return slot.elementId == elementId && slot.id == popupId; };
    auto found = std::find_if(m_popups.begin(), m_popups.end(), same);
    if (found != m_popups.end()) {
        found->tree = std::move(tree);
    } else {
        m_popups.push_back(UiSlot{elementId, popupId, std::move(tree)});
    }
}

void UiModel::ClosePopup(const std::string& elementId, const std::string& popupId) {
    std::lock_guard<std::mutex> guard(m_mutex);
    auto same = [&](const UiSlot& slot) { return slot.elementId == elementId && slot.id == popupId; };
    m_popups.erase(std::remove_if(m_popups.begin(), m_popups.end(), same), m_popups.end());
}

std::uint64_t UiModel::SetFullscreen(const std::string& elementId, Json tree, std::uint64_t session) {
    std::lock_guard<std::mutex> guard(m_mutex);
    bool owner = m_fullscreen && m_fullscreen->elementId == elementId;
    if (session != 0) {
        if (!owner || session != m_fullscreenSession) return 0;  // closed or taken meanwhile: a late update
        m_fullscreen->tree = std::move(tree);
        return session;
    }
    if (m_fullscreen && !owner) m_displaced.emplace_back(m_fullscreen->elementId, m_fullscreenSession);
    m_fullscreenSession = ++m_fullscreenSessions;
    m_fullscreen = UiSlot{elementId, std::to_string(m_fullscreenSession), std::move(tree)};
    return m_fullscreenSession;
}

void UiModel::CloseFullscreen(const std::string& elementId, std::uint64_t session) {
    std::lock_guard<std::mutex> guard(m_mutex);
    if (!m_fullscreen || m_fullscreen->elementId != elementId) return;
    if (session != 0 && session != m_fullscreenSession) return;
    m_fullscreen.reset();
    m_fullscreenSession = 0;
}

std::optional<UiSlot> UiModel::Fullscreen() const {
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_fullscreen;
}

std::vector<std::pair<std::string, std::uint64_t>> UiModel::TakeDisplacedFullscreens() {
    std::lock_guard<std::mutex> guard(m_mutex);
    return std::exchange(m_displaced, {});
}

int UiModel::ReserveId() {
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_nextId++;
}

bool UiModel::SetControlLabel(const std::string& elementId, int controlId, const std::string& label) {
    std::lock_guard<std::mutex> guard(m_mutex);
    for (UiControl& control : m_controls) {
        if (control.id != controlId || control.elementId != elementId) continue;
        control.label = label;
        return true;
    }
    return false;
}

bool UiModel::SetControlActive(const std::string& elementId, int controlId, bool active) {
    std::lock_guard<std::mutex> guard(m_mutex);
    for (UiControl& control : m_controls) {
        if (control.id != controlId || control.elementId != elementId) continue;
        control.active = active;
        return true;
    }
    return false;
}

void UiModel::SetTitle(const std::string& elementId, const std::string& title) {
    std::lock_guard<std::mutex> guard(m_mutex);
    m_titles[elementId] = title;
}

void UiModel::Navigate(const std::string& elementId, const std::string& path) {
    std::lock_guard<std::mutex> guard(m_mutex);
    m_route = std::make_pair(elementId, path);
    m_routeSerial++;
}

void UiModel::RemoveElement(const std::string& elementId) {
    std::lock_guard<std::mutex> guard(m_mutex);
    auto owned = [&](const auto& item) { return item.elementId == elementId; };
    m_controls.erase(std::remove_if(m_controls.begin(), m_controls.end(), owned), m_controls.end());
    m_panels.erase(std::remove_if(m_panels.begin(), m_panels.end(), owned), m_panels.end());
    m_pages.erase(std::remove_if(m_pages.begin(), m_pages.end(), owned), m_pages.end());
    m_popups.erase(std::remove_if(m_popups.begin(), m_popups.end(), owned), m_popups.end());
    m_titles.erase(elementId);
    auto displaced = [&](const auto& pair) { return pair.first == elementId; };
    m_displaced.erase(std::remove_if(m_displaced.begin(), m_displaced.end(), displaced), m_displaced.end());
    if (m_fullscreen && m_fullscreen->elementId == elementId) {
        m_fullscreen.reset();
        m_fullscreenSession = 0;
    }
    if (m_route && m_route->first == elementId) m_route.reset();
}

std::optional<UiControl> UiModel::Find(int controlId) const {
    std::lock_guard<std::mutex> guard(m_mutex);
    for (const UiControl& control : m_controls) {
        if (control.id == controlId) return control;
    }
    return std::nullopt;
}

std::vector<UiControl> UiModel::Controls() const {
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_controls;
}

std::optional<UiSlot> UiModel::Panel(const std::string& elementId, const std::string& panelId) const {
    std::lock_guard<std::mutex> guard(m_mutex);
    for (const UiSlot& slot : m_panels) {
        if (slot.elementId == elementId && slot.id == panelId) return slot;
    }
    return std::nullopt;
}

std::optional<UiSlot> UiModel::Page(const std::string& elementId, const std::string& path) const {
    std::lock_guard<std::mutex> guard(m_mutex);
    for (const UiSlot& slot : m_pages) {
        if (slot.elementId == elementId && slot.id == path) return slot;
    }
    return std::nullopt;
}

std::vector<UiSlot> UiModel::Panels() const {
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_panels;
}

std::vector<UiSlot> UiModel::Pages() const {
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_pages;
}

std::vector<UiSlot> UiModel::Popups() const {
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_popups;
}

std::optional<std::string> UiModel::Title(const std::string& elementId) const {
    std::lock_guard<std::mutex> guard(m_mutex);
    auto found = m_titles.find(elementId);
    if (found == m_titles.end()) return std::nullopt;
    return found->second;
}

std::optional<std::pair<std::string, std::string>> UiModel::Route() const {
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_route;
}

std::uint64_t UiModel::RouteSerial() const {
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_routeSerial;
}

void UiModel::RequestApplePlayer() {
    std::lock_guard<std::mutex> guard(m_mutex);
    m_applePlayerRequests++;
}

std::uint64_t UiModel::ApplePlayerRequests() const {
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_applePlayerRequests;
}

bool ValidateTree(const Json& tree, std::string* error) {
    int nodes = 0;
    return CheckNode(tree, "", 1, nodes, error);
}

bool IsPlacement(std::string_view placement) {
    return placement == "topbar" || placement == "playbar" || placement == "sidebar" || placement == "menu-context" ||
           placement == "menu-app";
}

std::string_view PlacementPermission(std::string_view placement) {
    if (placement == "topbar") return "ui.topbar";
    if (placement == "playbar") return "ui.playbar";
    if (placement == "sidebar") return "ui.sidebar";
    return "ui.menu";
}

bool FullscreenPointerDue(std::int64_t nowMs, std::optional<std::int64_t> lastMs) {
    return !lastMs || nowMs - *lastMs >= kFullscreenPointerGapMs;
}

}  // namespace ampify
