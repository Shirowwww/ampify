#include "ui_model.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace amk {

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
    static const std::array<std::string_view, 13> kPrimitives = {
        "stack", "row", "text", "button", "toggle", "slider", "list", "image", "icon",
        "card", "textfield", "dropdown", "option"};
    return OneOf(kPrimitives, name);
}

bool HasChildren(const std::string& name) {
    static const std::array<std::string_view, 5> kContainers = {"stack", "row", "list", "card", "dropdown"};
    return OneOf(kContainers, name);
}

// The primitives that report a click, and those that report a new value.
bool TakesClick(const std::string& name) {
    static const std::array<std::string_view, 4> kClickable = {"button", "card", "image", "icon"};
    return OneOf(kClickable, name);
}

bool TakesChange(const std::string& name) {
    static const std::array<std::string_view, 4> kEditable = {"toggle", "slider", "textfield", "dropdown"};
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
        if (!allowed) return Fail(error, key + " non autorisé sur " + name);
        if (!IsCallbackId(value)) return Fail(error, key + " doit être un identifiant de rappel (entier positif)");
    } else if (key == "tooltip") {
        if (!value.IsString()) return Fail(error, "tooltip doit être un texte");
    } else if (key == "tooltipPlacement") {
        static const std::array<std::string_view, 4> kPlacements = {"top", "bottom", "left", "right"};
        const std::string* placement = value.AsString();
        if (!placement || !OneOf(kPlacements, *placement)) {
            return Fail(error, "tooltipPlacement attend top, bottom, left ou right");
        }
    } else if (key == "tooltipDelay") {
        const double* delay = value.AsNumber();
        if (!delay || *delay < 0 || *delay > kMaxTooltipDelayMs) {
            return Fail(error, "tooltipDelay attend un nombre de millisecondes entre 0 et 10000");
        }
    }
    return true;
}

bool CheckNode(const Json& node, const std::string& parent, int depth, int& nodes, std::string* error) {
    if (++nodes > kMaxTreeNodes) return Fail(error, "interface trop grande (300 éléments au plus)");
    if (depth > kMaxTreeDepth) return Fail(error, "interface trop profonde (8 niveaux au plus)");
    const Json::Object* attributes = node.AsObject();
    if (!attributes) return Fail(error, "chaque élément de l'interface doit être un objet");

    const Json* type = node.Find("type");
    const std::string* name = type ? type->AsString() : nullptr;
    if (!name) return Fail(error, "élément sans type");
    if (!IsPrimitive(*name)) return Fail(error, "primitive inconnue : " + *name);
    if ((*name == "option") != (parent == "dropdown")) {
        return Fail(error, *name == "option" ? "option hors d'une liste déroulante"
                                             : "une liste déroulante ne contient que des options");
    }

    for (const auto& [key, value] : *attributes) {
        if (key == "children") continue;
        if (value.AsArray() || value.AsObject()) return Fail(error, "attribut non autorisé : " + key);
        if (const std::string* text = value.AsString()) {
            if (text->size() > kMaxText) return Fail(error, "texte trop long dans " + key + " (500 caractères au plus)");
        }
        if (!CheckAttribute(*name, key, value, error)) return false;
    }

    const Json* children = node.Find("children");
    if (!children) return true;
    const Json::Array* items = children->AsArray();
    if (!items) return Fail(error, "children doit être une liste");
    if (!items->empty() && !HasChildren(*name)) return Fail(error, "primitive " + *name + " sans enfants");
    for (const Json& child : *items) {
        if (!CheckNode(child, *name, depth + 1, nodes, error)) return false;
    }
    return true;
}

}  // namespace

int UiModel::AddControl(const std::string& elementId, const std::string& placement, const std::string& label,
                        const std::string& icon) {
    if (!IsPlacement(placement)) return 0;
    std::lock_guard<std::mutex> guard(m_mutex);
    UiControl control;
    control.id = m_nextId++;
    control.elementId = elementId;
    control.placement = placement;
    control.label = label;
    control.icon = icon;
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

int UiModel::ReserveId() {
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_nextId++;
}

void UiModel::SetTitle(const std::string& elementId, const std::string& title) {
    std::lock_guard<std::mutex> guard(m_mutex);
    m_titles[elementId] = title;
}

void UiModel::Navigate(const std::string& elementId, const std::string& path) {
    std::lock_guard<std::mutex> guard(m_mutex);
    m_route = std::make_pair(elementId, path);
}

void UiModel::RemoveElement(const std::string& elementId) {
    std::lock_guard<std::mutex> guard(m_mutex);
    auto owned = [&](const auto& item) { return item.elementId == elementId; };
    m_controls.erase(std::remove_if(m_controls.begin(), m_controls.end(), owned), m_controls.end());
    m_panels.erase(std::remove_if(m_panels.begin(), m_panels.end(), owned), m_panels.end());
    m_pages.erase(std::remove_if(m_pages.begin(), m_pages.end(), owned), m_pages.end());
    m_popups.erase(std::remove_if(m_popups.begin(), m_popups.end(), owned), m_popups.end());
    m_titles.erase(elementId);
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

bool ValidateTree(const Json& tree, std::string* error) {
    int nodes = 0;
    return CheckNode(tree, "", 1, nodes, error);
}

bool IsPlacement(std::string_view placement) {
    return placement == "topbar" || placement == "playbar" || placement == "menu-context" || placement == "menu-app";
}

std::string_view PlacementPermission(std::string_view placement) {
    if (placement == "topbar") return "ui.topbar";
    if (placement == "playbar") return "ui.playbar";
    return "ui.menu";
}

}  // namespace amk
