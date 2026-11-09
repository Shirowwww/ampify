#include "ui_model.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <utility>

#include "text_util.hpp"
#include "ui_render/style_values.hpp"
#include "ui_vocab.hpp"

namespace ampify {

namespace {

using uivocab::Primitive;
using uivocab::StyleKind;

bool Fail(std::string* error, const std::string& message) {
    if (error) *error = message;
    return false;
}

// A limit as it reads in a message: 300, 0.5, 10000.
std::string Num(double value) { return ShortestNumberText(value); }

// "between 0 and 1000", from the limits.
std::string Between(double low, double high) { return "between " + Num(low) + " and " + Num(high); }

template <size_t N>
bool OneOf(const std::array<std::string_view, N>& names, std::string_view name) {
    return std::find(names.begin(), names.end(), name) != names.end();
}

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
std::optional<bool> CheckCommonStyle(const Primitive& primitive, const std::string& key, const Json& value,
                                     std::string* error) {
    static const std::array<std::string_view, 4> kPlaces = {"start", "center", "end", "stretch"};
    static const std::array<std::string_view, 3> kTextAligns = {"left", "center", "right"};
    if (key == "opacity") {
        if (!NumberIn(value, 0, 1)) return Fail(error, "opacity expects a number between 0 and 1");
    } else if (key == "scale") {
        if (!NumberIn(value, 0, uivocab::kMaxScale)) {
            return Fail(error, "scale expects a number " + Between(0, uivocab::kMaxScale));
        }
    } else if (key == "transition") {
        const std::string* text = value.AsString();
        if (!text || !uirender::ParseTransition(*text)) {
            return Fail(error, "transition expects \"<opacity|scale|translation|all> <duration>ms\", comma-separated (" +
                                   Num(uivocab::kMaxTransitionMs / 1000) + " s at most), or \"none\"");
        }
    } else if (key == "valign") {
        if (!TextOneOf(value, kPlaces)) return Fail(error, "valign expects start, center, end or stretch");
    } else if (key == "grow") {
        if (!value.AsBool()) return Fail(error, "grow expects true or false");
    } else if (key == "inert") {
        if (!value.AsBool()) return Fail(error, "inert expects true or false");
    } else if (key == "align") {
        if (primitive.textAlign && !TextOneOf(value, kTextAligns)) {
            return Fail(error, "align on text expects left, center or right");
        }
        if (!primitive.textAlign && !TextOneOf(value, kPlaces)) {
            return Fail(error, "align expects start, center, end or stretch");
        }
    } else {
        return std::nullopt;  // not a common style attribute
    }
    return true;
}

bool CheckBoxStyle(StyleKind style, const std::string& key, const Json& value, std::string* error) {
    bool box = style == StyleKind::Box;
    bool image = style == StyleKind::Image;
    if ((box || image) && key == "cornerRadius") {
        if (!NumberIn(value, 0, uivocab::kMaxCorner)) {
            return Fail(error, "cornerRadius expects a number " + Between(0, uivocab::kMaxCorner));
        }
    } else if ((box && (key == "minWidth" || key == "maxWidth")) || ((box || image) && (key == "width" || key == "height"))) {
        if (!NumberIn(value, 0, uivocab::kMaxLength)) {
            return Fail(error, key + " expects a number of pixels " + Between(0, uivocab::kMaxLength));
        }
    } else if (box && key == "background") {
        const std::string* text = value.AsString();
        if (!text || !uirender::ParseBackground(*text)) {
            return Fail(error, "background expects #RRGGBB, #AARRGGBB, acrylic, acrylic(#AARRGGBB, 0.6) or "
                               "linear-gradient(#top, #bottom)");
        }
    } else if (box && key == "padding") {
        if (!uirender::ParsePadding(value)) {
            return Fail(error, "padding expects a number or \"top,right,bottom,left\" (0 to " +
                                   Num(uivocab::kMaxPadding) + " each)");
        }
    } else if (image && key == "fit") {
        static const std::array<std::string_view, 3> kFits = {"cover", "contain", "fill"};
        if (!TextOneOf(value, kFits)) return Fail(error, "fit expects cover, contain or fill");
    }
    return true;
}

bool CheckTextStyle(const std::string& key, const Json& value, std::string* error) {
    if (key == "size") {
        if (!NumberIn(value, uivocab::kMinTextSize, uivocab::kMaxTextSize)) {
            return Fail(error, "size expects a number of pixels " + Between(uivocab::kMinTextSize, uivocab::kMaxTextSize));
        }
    } else if (key == "weight") {
        if (!WholeIn(value, 100, 900)) return Fail(error, "weight expects a whole number between 100 and 900");
    } else if (key == "color") {
        const std::string* text = value.AsString();
        if (!text || !uirender::ParseColor(*text)) return Fail(error, "color expects #RRGGBB or #AARRGGBB");
    } else if (key == "wrap") {
        if (!value.AsBool()) return Fail(error, "wrap expects true or false");
    } else if (key == "maxLines") {
        if (!WholeIn(value, 0, uivocab::kMaxLines)) {
            return Fail(error, "maxLines expects a whole number " + Between(0, uivocab::kMaxLines));
        }
    }
    return true;
}

bool CheckGridStyle(const std::string& key, const Json& value, std::string* error) {
    if (key == "gap") {
        if (!NumberIn(value, 0, uivocab::kMaxGridGap)) {
            return Fail(error, "gap expects a number of pixels " + Between(0, uivocab::kMaxGridGap));
        }
        return true;
    }
    if (key != "columns") return true;
    if (!WholeIn(value, 1, uivocab::kMaxGridColumns)) {
        return Fail(error, "columns expects a whole number " + Between(1, uivocab::kMaxGridColumns));
    }
    return true;
}

bool CheckStyle(const Primitive& primitive, const std::string& key, const Json& value, std::string* error) {
    if (std::optional<bool> common = CheckCommonStyle(primitive, key, value, error)) return *common;
    switch (primitive.style) {
        case StyleKind::Text: return CheckTextStyle(key, value, error);
        case StyleKind::Grid: return CheckGridStyle(key, value, error);
        default: return CheckBoxStyle(primitive.style, key, value, error);
    }
}

bool IsCallbackId(const Json& value) {
    const double* number = value.AsNumber();
    return number && *number >= 1 && *number <= 2147483647 && std::floor(*number) == *number;
}

// The attributes whose value is checked beyond "a scalar".
bool CheckAttribute(const Primitive& primitive, const std::string& key, const Json& value, std::string* error) {
    if (key == "onClick" || key == "onChange") {
        bool allowed = key == "onClick" ? primitive.click : primitive.change;
        if (!allowed) return Fail(error, key + " is not allowed on " + std::string(primitive.name));
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
        if (!delay || *delay < 0 || *delay > uivocab::kMaxTooltipDelayMs) {
            return Fail(error, "tooltipDelay expects a number of milliseconds " + Between(0, uivocab::kMaxTooltipDelayMs));
        }
    }
    return CheckStyle(primitive, key, value, error);
}

// Where a node may sit: option, tab and item belong to one container only, and that container holds nothing else.
bool CheckPlace(const Primitive& primitive, const Primitive* parent, std::string* error) {
    if (!primitive.container.empty() && (!parent || parent->name != primitive.container)) {
        return Fail(error, std::string(primitive.name) + " outside " + std::string(primitive.container));
    }
    if (parent && !parent->onlyChild.empty() && primitive.name != parent->onlyChild) {
        return Fail(error, std::string(parent->name) + " only holds " + std::string(parent->onlyChild) + " nodes");
    }
    return true;
}

bool CheckNode(const Json& node, const Primitive* parent, int depth, int& nodes, std::string* error) {
    if (++nodes > uivocab::kMaxTreeNodes) {
        return Fail(error, "interface too large (" + std::to_string(uivocab::kMaxTreeNodes) + " elements at most)");
    }
    if (depth > uivocab::kMaxTreeDepth) {
        return Fail(error, "interface too deep (" + std::to_string(uivocab::kMaxTreeDepth) + " levels at most)");
    }
    const Json::Object* attributes = node.AsObject();
    if (!attributes) return Fail(error, "each element of the interface must be an object");

    const Json* type = node.Find("type");
    const std::string* name = type ? type->AsString() : nullptr;
    if (!name) return Fail(error, "element without a type");
    const Primitive* primitive = uivocab::FindPrimitive(*name);
    if (!primitive) return Fail(error, "unknown primitive: " + *name);
    if (!CheckPlace(*primitive, parent, error)) return false;

    for (const auto& [key, value] : *attributes) {
        if (key == "children") continue;
        if (value.AsArray() || value.AsObject()) return Fail(error, "attribute not allowed: " + key);
        if (const std::string* text = value.AsString()) {
            if (text->size() > uivocab::kMaxText) {
                return Fail(error, "text too long in " + key + " (" + std::to_string(uivocab::kMaxText) + " characters at most)");
            }
        }
        if (!CheckAttribute(*primitive, key, value, error)) return false;
    }

    const Json* children = node.Find("children");
    if (!children) return true;
    const Json::Array* items = children->AsArray();
    if (!items) return Fail(error, "children must be a list");
    if (!items->empty() && !primitive->children) return Fail(error, "primitive " + *name + " cannot have children");
    for (const Json& child : *items) {
        if (!CheckNode(child, primitive, depth + 1, nodes, error)) return false;
    }
    return true;
}

// The slot of an element with this id, in a list of panels, pages or popups.
template <typename Slots>
auto* FindSlot(Slots& slots, const std::string& elementId, const std::string& id) {
    auto found = std::find_if(slots.begin(), slots.end(),
                              [&](const UiSlot& slot) { return slot.elementId == elementId && slot.id == id; });
    return found == slots.end() ? nullptr : &*found;
}

// Registering the same slot again replaces its tree.
void Upsert(std::vector<UiSlot>& slots, const std::string& elementId, const std::string& id, Json tree) {
    if (UiSlot* slot = FindSlot(slots, elementId, id)) {
        slot->tree = std::move(tree);
    } else {
        slots.push_back(UiSlot{elementId, id, std::move(tree)});
    }
}

// Applies `change` to the control the element added, false when it has none with this id.
template <typename Change>
bool ChangeControl(std::vector<UiControl>& controls, const std::string& elementId, int controlId, Change change) {
    for (UiControl& control : controls) {
        if (control.id != controlId || control.elementId != elementId) continue;
        change(control);
        return true;
    }
    return false;
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
    Upsert(m_panels, elementId, panelId, std::move(tree));
}

bool UiModel::RemovePanel(const std::string& elementId, const std::string& panelId) {
    std::lock_guard<std::mutex> guard(m_mutex);
    return std::erase_if(m_panels, [&](const UiSlot& slot) { return slot.elementId == elementId && slot.id == panelId; }) > 0;
}

void UiModel::SetPage(const std::string& elementId, const std::string& path, Json tree) {
    std::lock_guard<std::mutex> guard(m_mutex);
    Upsert(m_pages, elementId, path, std::move(tree));
}

void UiModel::OpenPopup(const std::string& elementId, Json tree) {
    std::lock_guard<std::mutex> guard(m_mutex);
    m_popups.push_back(UiSlot{elementId, {}, std::move(tree)});
}

void UiModel::SetPopup(const std::string& elementId, const std::string& popupId, Json tree) {
    std::lock_guard<std::mutex> guard(m_mutex);
    Upsert(m_popups, elementId, popupId, std::move(tree));
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
    return ChangeControl(m_controls, elementId, controlId, [&](UiControl& control) { control.label = label; });
}

bool UiModel::SetControlActive(const std::string& elementId, int controlId, bool active) {
    std::lock_guard<std::mutex> guard(m_mutex);
    return ChangeControl(m_controls, elementId, controlId, [&](UiControl& control) { control.active = active; });
}

bool UiModel::SetControlDisabled(const std::string& elementId, int controlId, bool disabled) {
    std::lock_guard<std::mutex> guard(m_mutex);
    return ChangeControl(m_controls, elementId, controlId, [&](UiControl& control) { control.disabled = disabled; });
}

bool UiModel::SetControlMark(const std::string& elementId, int controlId, double from, double to) {
    std::lock_guard<std::mutex> guard(m_mutex);
    for (UiControl& control : m_controls) {
        if (control.id != controlId || control.elementId != elementId) continue;
        if (control.placement != "playbar") return false;
        if (!(from >= 0)) {  // NaN too
            control.markFrom = control.markTo = -1;
        } else if (!(to >= 0)) {
            control.markFrom = std::min(from, 1.0);
            control.markTo = -1;
        } else {
            control.markFrom = std::clamp(std::min(from, to), 0.0, 1.0);
            control.markTo = std::clamp(std::max(from, to), 0.0, 1.0);
        }
        return true;
    }
    return false;
}

bool UiModel::SetControlKinds(const std::string& elementId, int controlId, std::vector<std::string> kinds) {
    std::lock_guard<std::mutex> guard(m_mutex);
    return ChangeControl(m_controls, elementId, controlId,
                         [&](UiControl& control) { control.kinds = std::move(kinds); });
}

bool UiModel::RemoveControl(const std::string& elementId, int controlId) {
    std::lock_guard<std::mutex> guard(m_mutex);
    auto gone = [&](const UiControl& control) {
        return control.elementId == elementId && (control.id == controlId || control.parent == controlId);
    };
    auto from = std::remove_if(m_controls.begin(), m_controls.end(), gone);
    bool removed = from != m_controls.end();
    m_controls.erase(from, m_controls.end());
    return removed;
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
    const UiSlot* slot = FindSlot(m_panels, elementId, panelId);
    return slot ? std::optional<UiSlot>(*slot) : std::nullopt;
}

std::optional<UiSlot> UiModel::Page(const std::string& elementId, const std::string& path) const {
    std::lock_guard<std::mutex> guard(m_mutex);
    const UiSlot* slot = FindSlot(m_pages, elementId, path);
    return slot ? std::optional<UiSlot>(*slot) : std::nullopt;
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
    return CheckNode(tree, nullptr, 1, nodes, error);
}

bool IsPlacement(std::string_view placement) { return uivocab::FindPlacement(placement) != nullptr; }

bool IsMenuItemKind(std::string_view kind) {
    static constexpr std::array<std::string_view, 10> kKinds = {
        "song", "album", "artist", "composer", "genre", "playlist", "station", "video", "concert", "container"};
    return std::find(kKinds.begin(), kKinds.end(), kind) != kKinds.end();
}

bool ShowsForItem(const UiControl& control, std::string_view kind) {
    if (control.kinds.empty()) return true;
    return std::find(control.kinds.begin(), control.kinds.end(), kind) != control.kinds.end();
}

std::string_view PlacementPermission(std::string_view placement) {
    const uivocab::Placement* found = uivocab::FindPlacement(placement);
    return found ? found->permission : "ui.menu";  // what an unnamed placement has always asked for
}

}  // namespace ampify
