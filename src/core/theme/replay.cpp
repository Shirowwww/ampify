#include "replay.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <span>

#include "../json.hpp"
#include "animation.hpp"
#include "color.hpp"
#include "inspector.hpp"
#include "properties.hpp"
#include "selector.hpp"

namespace ampify::theme {

namespace {

constexpr std::string_view kRecordedObject = "recorded";  // an opaque value of the dump ("present")

std::optional<double> Number(std::string_view text) {
    while (!text.empty() && text.front() == ' ') text.remove_prefix(1);
    if (text.starts_with("-nan") || text.starts_with("nan") || text == "NaN") return std::nan("");
    double value = 0;
    auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc() || end != text.data() + text.size()) return std::nullopt;
    return value;
}

std::vector<std::string_view> Parts(std::string_view text, char separator) {
    std::vector<std::string_view> parts;
    for (size_t at = text.find(separator); at != std::string_view::npos; at = text.find(separator)) {
        parts.push_back(text.substr(0, at));
        text.remove_prefix(at + 1);
    }
    parts.push_back(text);
    return parts;
}

// Each part a number, `count` of them (0: any count).
std::optional<std::vector<double>> Numbers(std::string_view text, size_t count) {
    std::vector<double> numbers;
    for (std::string_view part : Parts(text, ',')) {
        std::optional<double> number = Number(part);
        if (!number) return std::nullopt;
        numbers.push_back(*number);
    }
    if (count && numbers.size() != count) return std::nullopt;
    return numbers;
}

bool IsColourText(std::string_view text) {
    return text.size() == 9 && text[0] == '#' &&
           std::all_of(text.begin() + 1, text.end(), [](char c) { return std::isxdigit(static_cast<unsigned char>(c)); });
}

bool IsBrushProperty(std::string_view property) {
    return property == "Background" || property == "Foreground" || property == "BorderBrush" ||
           property == "PointerOverBackground" || property == "PressedBackground" || property == "SelectedBackground" ||
           property == "SelectedPointerOverBackground" || property == "SelectedPressedBackground" ||
           property == kCheckedForegroundProperty;
}

bool IsThicknessProperty(std::string_view property) {
    return property == "Margin" || property == "Padding" || property == "BorderThickness" || property == "CornerRadius";
}

bool IsLengthProperty(std::string_view property) {
    return property == "FontSize" || property == "Width" || property == "Height" || property == "MinWidth" ||
           property == "MaxWidth" || property == "MinHeight" || property == "MaxHeight" || property == "LineHeight" ||
           property == "CompactPaneLength" || property == "TrackHeight" || property == "TrackRectRadius";
}

// Apple's progress line's own (src/win/apple_properties.cpp).
bool IsScrubberProperty(std::string_view property) {
    return property == "TrackHeight" || property == "TrackRectRadius" || property == "ThumbVisibility";
}

bool IsVisibilityProperty(std::string_view property) {
    return property == "Visibility" || property == "ThumbVisibility";
}

bool IsNumberProperty(std::string_view property) {
    return property == "FontWeight" || property == "Opacity" || property == "CharacterSpacing" ||
           property.starts_with("Grid.") || property == "Canvas.ZIndex" || property == kOrderProperty;
}

// The members XAML accepts for each enumeration the engine writes.
std::span<const std::string_view> KeywordsOf(std::string_view property) {
    static constexpr std::string_view kFlow[] = {"LeftToRight", "RightToLeft"};
    static constexpr std::string_view kHorizontal[] = {"Left", "Center", "Right", "Stretch"};
    static constexpr std::string_view kVertical[] = {"Top", "Center", "Bottom", "Stretch"};
    static constexpr std::string_view kText[] = {"Left", "Center", "Right", "Justify", "DetectFromContent", "Start", "End"};
    static constexpr std::string_view kTrimming[] = {"None", "CharacterEllipsis", "WordEllipsis", "Clip"};
    static constexpr std::string_view kWrapping[] = {"NoWrap", "Wrap", "WrapWholeWords"};
    static constexpr std::string_view kStyle[] = {"Normal", "Italic", "Oblique"};
    static constexpr std::string_view kMode[] = {"Default", "Light", "Dark"};
    if (property == "FlowDirection") return kFlow;
    if (property == "HorizontalAlignment") return kHorizontal;
    if (property == "VerticalAlignment") return kVertical;
    if (property == "TextAlignment") return kText;
    if (property == "TextTrimming") return kTrimming;
    if (property == "TextWrapping") return kWrapping;
    if (property == "FontStyle") return kStyle;
    if (property == "RequestedTheme") return kMode;
    return {};
}

bool Recorded(const ReplayBackend::Node& node, std::string_view property) {
    return node.properties.count(property) > 0;
}

bool IsInspected(std::string_view property) {
    std::span<const char* const> inspected = InspectedProperties();
    return std::any_of(inspected.begin(), inspected.end(), [&](const char* name) { return property == name; });
}

// A Control (Button, ToggleButton, ContentControl...): the live backend reads a FontSize and a Background only on one.
bool IsControl(const ReplayBackend::Node& node) {
    return Recorded(node, "FontSize") && Recorded(node, "Background");
}

// A Panel whose children Ampify may add to (version 1 dumps lack the flag): a Background without a Foreground is a
// Panel or a Border; Border is told by name, and the items hosts of lists by theirs.
bool LooksLikePanel(const ReplayBackend::Node& node) {
    static constexpr std::string_view kNotPanels[] = {"Border", "ItemsStackPanel", "ItemsWrapGrid", "VirtualizingStackPanel",
                                                      "WrapGrid", "ContentPresenter", "Popup"};
    if (!Recorded(node, "Background") || Recorded(node, "Foreground")) return false;
    return std::none_of(std::begin(kNotPanels), std::end(kNotPanels), [&](std::string_view type) { return node.type == type; });
}

// The value a property the inspector does not read shows on an element whose class has it, at WinUI's default; nothing
// for a class without it. Judged from the recorded properties, which the live backend reads by class too.
std::optional<Value> Derived(const ReplayBackend::Node& node, std::string_view property) {
    const bool element = Recorded(node, "Opacity");     // a UIElement
    const bool framework = Recorded(node, "Margin");    // a FrameworkElement
    const Value none{ValueKind::Opaque, std::string(kRecordedObject)};
    if (property == "BorderThickness") {
        if (Recorded(node, "BorderBrush")) return Value{ValueKind::Thickness, "0,0,0,0"};
    } else if (property == "MinWidth" || property == "MinHeight") {
        if (framework) return Value{ValueKind::Length, "0"};
    } else if (property == "MaxWidth" || property == "MaxHeight") {
        if (framework) return Value{ValueKind::Length, "inf"};
    } else if (property == "CompactPaneLength") {
        if (node.type == "NavigationView") return Value{ValueKind::Length, "48"};
    } else if (IsScrubberProperty(property)) {
        if (node.type == "ScrubberControl") return none;  // Apple's own values, which no dump has recorded yet
    } else if (property == "RequestedTheme") {
        if (framework) return Value{ValueKind::Keyword, "Default"};
    } else if (property == "Grid.RowSpan" || property == "Grid.ColumnSpan") {
        if (Recorded(node, "Grid.Row")) return Value{ValueKind::Number, "1"};
    } else if (property == "RenderTransform" || property == "Shadow") {
        if (element) return none;
    } else if (property == "RenderTransformOrigin") {
        if (element) return Value{ValueKind::Point, "0,0"};
    } else if (property == "Transition") {
        if (element) return Value{ValueKind::Transition, ""};
    } else if (property == "Animation") {
        if (element) return Value{ValueKind::Animation, ""};
    } else if (property == "Clip") {
        if (framework) return none;
    } else if (property == kTemplateProperty) {
        if (IsControl(node)) return Value{ValueKind::Opaque, "template"};
    } else if (property == kCheckedForegroundProperty) {
        if (IsControl(node)) return kNoOwnValue;
    } else if (property == "PointerOverBackground" || property == "PressedBackground" || property.starts_with("Selected")) {
        if (node.type == "ListViewItemPresenter" || node.type == "GridViewItemPresenter") return none;
    }
    return std::nullopt;
}

std::string ShownValue(std::string_view property, const Value& value) {
    if (value.kind == ValueKind::Opaque) return value == kNoOwnValue ? std::string(kNoOwnValueText) : "present";
    if (value.kind == ValueKind::Template) return "template";
    return ShownText(property, value);
}

bool HasNegative(std::string_view text) {
    std::optional<std::vector<double>> numbers = Numbers(text, 0);
    return numbers && std::any_of(numbers->begin(), numbers->end(), [](double side) { return side < 0; });
}

std::string KindRefusal(std::string_view property, const Value& value) {
    const ValueKind kind = value.kind;
    const std::string where = "value " + value.text + " for " + std::string(property);
    if (IsBrushProperty(property)) {
        if (kind == ValueKind::Color && !IsColourText(value.text)) return where + ": not a #AARRGGBB colour";
        if (kind != ValueKind::Color && kind != ValueKind::Image && kind != ValueKind::Gradient &&
            kind != ValueKind::Acrylic) {
            return where + ": not a brush";
        }
    } else if (IsThicknessProperty(property)) {
        if (kind != ValueKind::Thickness || !Numbers(value.text, 4)) return where + ": not four lengths";
        if (property != "Margin" && HasNegative(value.text)) {
            return where + ": a negative side, which WinUI refuses at the next layout and ends Apple Music";
        }
    } else if (IsLengthProperty(property)) {
        std::optional<double> length = kind == ValueKind::Length ? Number(value.text) : std::nullopt;
        if (!length) return where + ": not a length";
        if (*length < 0) return where + ": a negative length";
        if (property == "FontSize" && !(*length > 0)) return where + ": a font size must be above 0";
    } else if (IsNumberProperty(property)) {
        std::optional<double> number = kind == ValueKind::Number ? Number(value.text) : std::nullopt;
        if (!number) return where + ": not a number";
        if (property == "FontWeight" && (*number < 1 || *number > 999)) return where + ": a weight goes from 1 to 999";
        if ((property == "Grid.RowSpan" || property == "Grid.ColumnSpan") && *number < 1) return where + ": a span starts at 1";
        if (property == kOrderProperty && *number < 0) return where + ": a position starts at 0";
    } else if (IsVisibilityProperty(property)) {
        if (value.text != "Visible" && value.text != "Collapsed") return where + ": Visible or Collapsed";
    } else if (!KeywordsOf(property).empty()) {
        std::span<const std::string_view> members = KeywordsOf(property);
        if (std::find(members.begin(), members.end(), value.text) == members.end()) return where + ": not a member of " + std::string(property);
    } else if (property == "FontFamily") {
        if (kind != ValueKind::Text || value.text.empty()) return where + ": not a font family";
    } else if (property == kTemplateProperty) {
        if (kind != ValueKind::Template || value.text.find("ControlTemplate") == std::string::npos) {
            return "template for " + std::string(property) + ": not a ControlTemplate";
        }
    } else if (property == "RenderTransform") {
        if (kind != ValueKind::Transform || !Numbers(value.text, 5)) return where + ": not a transform";
    } else if (property == "RenderTransformOrigin") {
        if (kind != ValueKind::Point || !Numbers(value.text, 2)) return where + ": not a point";
    } else if (property == "Shadow") {
        if (kind != ValueKind::Shadow || !Number(value.text)) return where + ": not a depth";
    } else if (property == "Clip") {
        if (kind != ValueKind::Clip || (!value.text.empty() && !Numbers(value.text, 4))) return where + ": not a clip";
    } else if (property == "Transition") {
        if (kind != ValueKind::Transition) return where + ": not a transition";
    } else if (property == "Animation") {
        if (kind != ValueKind::Animation || (!value.text.empty() && !DecodeAnimations(value.text))) {
            return where + ": not an animation";
        }
    }
    return {};
}

bool MovesTransform(const Value& animation) {
    std::optional<std::vector<Animation>> runs = DecodeAnimations(animation.text);
    if (!runs) return false;
    for (const Animation& run : *runs) {
        for (const AnimationTrack& track : run.tracks) {
            if (track.channel == AnimationTrack::Channel::Scale || track.channel == AnimationTrack::Channel::Rotation ||
                track.channel == AnimationTrack::Channel::Translation) {
                return true;
            }
        }
    }
    return false;
}

std::string TextOf(const Json& item, std::string_view key) {
    const Json* member = item.Find(key);
    const std::string* value = member ? member->AsString() : nullptr;
    return value ? *value : std::string();
}

bool FlagOf(const Json& item, std::string_view key, bool& recorded) {
    const Json* member = item.Find(key);
    if (!member || !member->AsBool()) return false;
    recorded = true;
    return *member->AsBool();
}

std::optional<NodeBox> BoxOf(const Json& item) {
    const Json* box = item.Find("box");
    const Json::Array* list = box ? box->AsArray() : nullptr;
    if (!list || list->size() != 4) return std::nullopt;
    double numbers[4] = {};
    for (size_t i = 0; i < 4; i++) {
        const double* number = (*list)[i].AsNumber();
        if (!number) return std::nullopt;
        numbers[i] = *number;
    }
    return NodeBox{numbers[0], numbers[1], numbers[2], numbers[3]};
}

}  // namespace

std::optional<Value> RecordedValue(std::string_view property, std::string_view text) {
    if (text == "present") return Value{ValueKind::Opaque, std::string(kRecordedObject)};
    if (text == kNoOwnValueText) return kNoOwnValue;
    const std::string copy(text);
    if (IsBrushProperty(property)) {
        if (IsColourText(text)) return Value{ValueKind::Color, copy};
        if (text.find('@') != std::string_view::npos) return Value{ValueKind::Gradient, copy};
        if (text.size() > 10 && IsColourText(text.substr(0, 9)) && text[9] == ';') return Value{ValueKind::Acrylic, copy};
        return Value{ValueKind::Image, copy};
    }
    if (property == "CornerRadius") {
        // inspector.json shows corners in XAML's order (top-left, top-right, bottom-right, bottom-left); the engine
        // keeps them as a thickness (bottom-left first, ShownText).
        std::vector<std::string_view> corners = Parts(text, ',');
        if (corners.size() != 4) return std::nullopt;
        return Value{ValueKind::Thickness, std::string(corners[3]) + "," + std::string(corners[0]) + "," +
                                               std::string(corners[1]) + "," + std::string(corners[2])};
    }
    if (IsThicknessProperty(property)) return Value{ValueKind::Thickness, copy};
    if (IsLengthProperty(property)) {
        std::optional<double> length = Number(text);
        if (!length) return std::nullopt;
        return Value{ValueKind::Length, std::isnan(*length) ? std::string("NaN") : copy};
    }
    if (IsNumberProperty(property)) return Value{ValueKind::Number, copy};
    if (IsVisibilityProperty(property)) return Value{ValueKind::Visibility, copy};
    if (!KeywordsOf(property).empty()) return Value{ValueKind::Keyword, copy};
    return Value{ValueKind::Text, copy};  // FontFamily, States
}

std::unique_ptr<ReplayBackend> ReplayBackend::Load(std::string_view text, std::string* problem, bool asRecorded) {
    auto fail = [&](std::string reason) -> std::unique_ptr<ReplayBackend> {
        if (problem) *problem = std::move(reason);
        return nullptr;
    };
    std::string error;
    std::optional<Json> json = Json::Parse(text, &error);
    if (!json) return fail("not JSON: " + error);
    const Json* list = json->Find("nodes");
    if (!list || !list->AsArray()) return fail("no \"nodes\" list: not an inspector.json");

    auto backend = std::make_unique<ReplayBackend>();
    if (const Json* version = json->Find("version"); version && version->AsNumber()) {
        backend->m_version = static_cast<int>(*version->AsNumber());
    }
    backend->m_recordedTheme = TextOf(*json, "theme");
    if (const Json* window = json->Find("window")) {
        const Json* width = window->Find("width");
        const Json* height = window->Find("height");
        const Json* folded = window->Find("sidebarFolded");
        if (width && width->AsNumber() && height && height->AsNumber()) {
            backend->m_window = ViewSize{*width->AsNumber(), *height->AsNumber(), folded && folded->AsBool() && *folded->AsBool()};
        }
    }
    if (const Json* resources = json->Find("resources"); resources && resources->AsArray()) {
        for (const Json& entry : *resources->AsArray()) {
            std::string key = TextOf(entry, "key");
            const Json* value = entry.Find("value");
            if (key.empty() || !value || !value->AsString()) continue;  // unreadable in the dump: missing here too
            std::optional<Value> parsed = RecordedValue("Background", *value->AsString());
            if (!parsed) continue;
            if (!backend->m_resources.count(key)) backend->m_resourceOrder.push_back(key);
            backend->m_resources[key] = *parsed;
        }
    }

    bool flags = false;
    int unknownOriginals = 0;
    int givenBack = 0;
    std::vector<int> bars;  // version 1: the visible "now playing" bars, for :playing
    for (const Json& item : *list->AsArray()) {
        const Json* parent = item.Find("parent");
        if (!parent || !parent->AsNumber()) return fail("node " + std::to_string(backend->m_nodes.size()) + " has no parent");
        Node node;
        node.type = TextOf(item, "type");
        node.fullType = TextOf(item, "fullType");
        node.name = TextOf(item, "name");
        node.parent = static_cast<int>(*parent->AsNumber());
        const int index = static_cast<int>(backend->m_nodes.size());
        if (node.parent < -1 || node.parent >= index) return fail("node " + std::to_string(index) + " comes before its parent");
        node.icon = FlagOf(item, "icon", flags);
        node.panel = FlagOf(item, "panel", flags);
        node.player = FlagOf(item, "player", flags);
        node.playing = FlagOf(item, "playing", flags);
        node.box = BoxOf(item);
        backend->m_hasBoxes = backend->m_hasBoxes || node.box.has_value();
        if (const Json* properties = item.Find("properties"); properties && properties->AsObject()) {
            for (const auto& [property, value] : *properties->AsObject()) {
                if (!value.AsString()) continue;
                if (std::optional<Value> parsed = RecordedValue(property, *value.AsString())) node.properties[property] = *parsed;
            }
        }
        node.reachable = !node.properties.empty();
        if (const Json* originals = item.Find("originals"); originals && originals->AsObject() && !asRecorded) {
            for (const auto& [property, value] : *originals->AsObject()) {
                const std::string* shown = value.AsString();
                if (!shown || *shown == kNoOwnValueText) {
                    unknownOriginals++;  // it showed its template's or parent's value: unknown here, kept as recorded
                    continue;
                }
                if (std::optional<Value> parsed = RecordedValue(property, *shown)) {
                    node.properties[property] = *parsed;
                    givenBack++;
                }
            }
        }
        if (node.parent >= 0) backend->m_nodes[node.parent].children.push_back(index);
        else backend->m_roots.push_back(index);
        if (node.name == "NowPlayingIndicator") {
            auto shown = node.properties.find("Visibility");
            if (shown != node.properties.end() && shown->second.text == "Visible") bars.push_back(index);
        }
        backend->m_nodes.push_back(std::move(node));
    }

    // The recorded mode, as DarkFromPalette judges a scheme: a dark base background, else a light primary text.
    auto dark = [&](const char* key, bool background) -> std::optional<bool> {
        auto it = backend->m_resources.find(key);
        std::optional<Argb> colour = it == backend->m_resources.end() ? std::nullopt : ParseColor(it->second.text);
        if (!colour) return std::nullopt;
        const bool darkColour = 0.2126 * colour->r + 0.7152 * colour->g + 0.0722 * colour->b < 128.0;
        return background ? darkColour : !darkColour;
    };
    for (auto [key, background] : {std::pair{"SolidBackgroundFillColorBaseBrush", true},
                                   std::pair{"ApplicationPageBackgroundThemeBrush", true},
                                   std::pair{"TextFillColorPrimaryBrush", false}}) {
        if (!backend->m_recordedDark) backend->m_recordedDark = dark(key, background);
    }

    std::vector<std::string>& warnings = backend->m_warnings;
    if (!flags) {
        for (Node& node : backend->m_nodes) node.panel = node.reachable && LooksLikePanel(node);
        std::vector<NodeInfo> infos = backend->Snapshot();
        MarkPlayingRows(infos, bars);
        for (const NodeInfo& info : infos) backend->m_nodes[info.id - 1].playing = info.playing;
        warnings.push_back("dump version " + std::to_string(backend->m_version) +
                           ": no element flags; panels judged from their properties, no icon text, no player window");
    }
    if (!backend->m_window) warnings.push_back("no window size recorded: @media rules apply nowhere (give --window WxH)");
    if (!backend->m_hasBoxes) warnings.push_back("no element boxes recorded: the wireframe is a list, not a layout");
    if (!backend->m_recordedTheme.empty()) {
        warnings.push_back("recorded under theme \"" + backend->m_recordedTheme + "\": " +
                           (asRecorded ? std::string("its values are kept as recorded")
                                       : std::to_string(givenBack) + " original value(s) given back, " +
                                             std::to_string(unknownOriginals) +
                                             " left as recorded (the element had no value of its own)"));
        warnings.push_back("resources and resources.xaml are as recorded under that theme");
    } else if (backend->m_version < 2) {
        warnings.push_back("dump version 1: whether a theme was on screen is unknown, values are taken as Apple's own");
    }
    return backend;
}

std::optional<bool> ReplayBackend::ModeOf(int node) const {
    for (int at = node; at >= 0 && at < static_cast<int>(m_nodes.size()); at = m_nodes[at].parent) {
        auto it = m_nodes[at].properties.find("RequestedTheme");
        if (it != m_nodes[at].properties.end() && it->second.text == "Dark") return true;
        if (it != m_nodes[at].properties.end() && it->second.text == "Light") return false;
    }
    return m_appMode ? m_appMode : m_recordedDark;
}

bool ReplayBackend::ModeDiffers(int node) const {
    std::optional<bool> shown = ModeOf(node);
    return shown && shown != m_recordedDark;
}

int ReplayBackend::IndexOf(NodeId id) const {
    if (id == 0 || id > m_nodes.size()) return -1;
    return static_cast<int>(id - 1);
}

std::vector<NodeInfo> ReplayBackend::Snapshot() {
    std::vector<NodeInfo> infos;
    infos.reserve(m_nodes.size());
    struct Pending {
        int node;
        int parent;  // index in `infos`
        int depth;
        int position;
        int siblings;
    };
    std::vector<Pending> stack;
    for (int i = static_cast<int>(m_roots.size()) - 1; i >= 0; i--) {
        stack.push_back({m_roots[i], -1, 0, 0, 1});
    }
    while (!stack.empty()) {
        Pending item = stack.back();
        stack.pop_back();
        const Node& node = m_nodes[item.node];
        NodeInfo info;
        info.id = IdOf(item.node);
        info.type = node.type;
        info.fullType = node.fullType;
        info.name = node.name;
        info.parent = item.parent;
        info.depth = item.depth;
        info.icon = node.icon;
        info.position = item.position;
        info.siblings = item.siblings;
        info.player = node.player;
        info.panel = node.panel;
        info.playing = node.playing;
        const int at = static_cast<int>(infos.size());
        infos.push_back(std::move(info));
        const int count = static_cast<int>(node.children.size());
        for (int i = count - 1; i >= 0; i--) stack.push_back({node.children[i], at, item.depth + 1, i, count});
    }
    return infos;
}

bool ReplayBackend::Alive(NodeId id) {
    int index = IndexOf(id);
    return index >= 0 && m_nodes[index].reachable;
}

std::optional<Value> ReplayBackend::ReadRaw(NodeId id, std::string_view property) {
    int index = IndexOf(id);
    if (index < 0 || !m_nodes[index].reachable) return std::nullopt;
    Node& node = m_nodes[index];
    if (auto it = node.properties.find(property); it != node.properties.end()) return it->second;
    if (property == kOrderProperty) {
        if (node.parent < 0 || !m_nodes[node.parent].panel) return std::nullopt;
        const std::vector<int>& siblings = m_nodes[node.parent].children;
        auto place = std::find(siblings.begin(), siblings.end(), index) - siblings.begin();
        return Value{ValueKind::Number, std::to_string(place)};
    }
    // Read by the inspector: not there. The scrubber's own properties are newer than the first dumps.
    if (property == "States" || (IsInspected(property) && !IsScrubberProperty(property))) return std::nullopt;
    return Derived(node, property);
}

std::string ReplayBackend::Refusal(const Node& node, std::string_view property, const Value& value) const {
    const std::string label = node.name.empty() ? node.type : node.type + "#" + node.name;
    if (property == "States") return "States is read only";
    if (value.kind == ValueKind::Opaque) return {};  // an original given back
    std::string kind = KindRefusal(property, value);
    if (!kind.empty()) return kind;
    if (property == "Transition" && node.type == "NavigationViewItem" &&
        (value.text.find("opacity") != std::string::npos || value.text.find("background") != std::string::npos)) {
        return label + " is animated by Apple Music: its opacity and background transitions are refused";
    }
    if (property == "Animation" && MovesTransform(value)) {
        auto transform = node.properties.find("RenderTransform");
        if (transform != node.properties.end() && transform->second.kind == ValueKind::Transform) {
            return label + " also has a transform: an animated scale, rotation or translation is refused with it";
        }
    }
    return {};
}

bool ReplayBackend::WriteRaw(NodeId id, std::string_view property, const Value& value) {
    int index = IndexOf(id);
    m_lastRefusal.clear();
    if (index < 0 || !m_nodes[index].reachable) {
        m_lastRefusal = "the element is gone";
        return false;
    }
    Node& node = m_nodes[index];
    if (!ReadRaw(id, property)) {
        m_lastRefusal = (node.name.empty() ? node.type : node.type + "#" + node.name) + " has no property " +
                        std::string(property);
        return false;
    }
    m_lastRefusal = Refusal(node, property, value);
    if (!m_lastRefusal.empty()) return false;

    if (property == kOrderProperty) {
        std::vector<int>& siblings = m_nodes[node.parent].children;
        siblings.erase(std::find(siblings.begin(), siblings.end(), index));
        size_t place = std::min(siblings.size(), static_cast<size_t>(std::atoi(value.text.c_str())));
        siblings.insert(siblings.begin() + static_cast<std::ptrdiff_t>(place), index);
        return true;
    }
    Value stored = value;
    // WinUI keeps lengths in single precision: 16.8 reads back as 16.799999237060547.
    if (value.kind == ValueKind::Length) {
        if (std::optional<double> length = Number(value.text); length && !std::isnan(*length) && !std::isinf(*length)) {
            stored.text = FormatNumber(static_cast<double>(static_cast<float>(*length)));
        }
    }
    node.properties[std::string(property)] = std::move(stored);
    return true;
}

std::optional<Value> ReplayBackend::Read(NodeId id, std::string_view property) {
    return m_hover.Read(id, property, *this);
}

std::optional<Value> ReplayBackend::Shown(int node, std::string_view property) {
    return ReadRaw(IdOf(node), property);
}

void ReplayBackend::Record(int node, std::string property, std::string before, std::string after, bool accepted,
                           std::string reason) {
    m_changes.push_back({m_pass, node, std::move(property), std::move(before), std::move(after), accepted, std::move(reason)});
}

bool ReplayBackend::Write(NodeId id, std::string_view property, const Value& value) {
    const int index = IndexOf(id);
    std::optional<Value> before = Read(id, property);
    const bool accepted = m_hover.Write(id, property, value, *this);
    Record(index, std::string(property), before ? ShownValue(property, *before) : std::string(),
           ShownValue(property, value), accepted, accepted ? std::string() : m_lastRefusal);
    return accepted;
}

bool ReplayBackend::Reparent(NodeId id, NodeId parent, int index) {
    const int node = IndexOf(id);
    const int target = IndexOf(parent);
    std::string reason;
    if (node < 0 || target < 0 || !m_nodes[node].reachable || !m_nodes[target].reachable) reason = "an element is gone";
    else if (!m_nodes[target].panel) reason = "the new parent is a " + m_nodes[target].type + ", not a panel";
    else if (m_nodes[node].parent < 0 || !m_nodes[m_nodes[node].parent].panel) reason = "the element's parent is not a panel";
    for (int up = target; reason.empty() && up >= 0; up = m_nodes[up].parent) {
        if (up == node) reason = "the new parent is inside the element";
    }
    const std::string from = node >= 0 && m_nodes[node].parent >= 0 ? std::to_string(m_nodes[node].parent) : "";
    Record(node, "parent", from, std::to_string(target), reason.empty(), reason);
    if (!reason.empty()) return false;

    std::vector<int>& old = m_nodes[m_nodes[node].parent].children;
    old.erase(std::find(old.begin(), old.end(), node));
    std::vector<int>& children = m_nodes[target].children;
    size_t place = index < 0 ? children.size() : std::min(children.size(), static_cast<size_t>(index));
    children.insert(children.begin() + static_cast<std::ptrdiff_t>(place), node);
    m_nodes[node].parent = target;
    return true;
}

std::vector<std::string> ReplayBackend::ResourceKeys() {
    return m_resourceOrder;
}

// The inspector records only the fixed list of Fluent colour keys (colour_keys.cpp); WinUI's control keys
// (ToggleSwitchFillOn, SliderTrackValueFill...) exist in the live app without being listed. A key the dump does not
// hold reads as an unknown object, so the engine writes it, and the write is counted in UnrecordedResources().
std::optional<Value> ReplayBackend::ReadResource(std::string_view key) {
    auto it = m_resources.find(key);
    if (it == m_resources.end()) return Value{ValueKind::Opaque, "unrecorded"};
    return it->second;
}

bool ReplayBackend::WriteResource(std::string_view key, const Value& value) {
    auto it = m_resources.find(key);
    std::string reason;
    if (value.kind == ValueKind::Color && !IsColourText(value.text)) reason = "not a #AARRGGBB colour";
    Record(-1, "resource " + std::string(key), it == m_resources.end() ? std::string("unrecorded") : ShownValue(key, it->second),
           ShownValue(key, value), reason.empty(), reason);
    if (!reason.empty()) return false;
    if (it == m_resources.end()) {
        m_unrecorded.insert(std::string(key));
        m_resources[std::string(key)] = value;  // not listed by ResourceKeys(): the dump did not have it
    } else {
        it->second = value;
    }
    return true;
}

bool ReplayBackend::SetDictionary(std::string_view xaml) {
    std::string reason;
    if (!xaml.empty() && xaml.find("ResourceDictionary") == std::string_view::npos) reason = "not a ResourceDictionary";
    Record(-1, "dictionary", m_dictionary.empty() ? "" : "merged", xaml.empty() ? "" : "merged", reason.empty(), reason);
    if (!reason.empty()) return false;
    m_dictionary = std::string(xaml);
    return true;
}

bool ReplayBackend::SetHover(NodeId id, const std::vector<HoverValue>& values) {
    const int index = IndexOf(id);
    if (index < 0 || !m_nodes[index].reachable || !Recorded(m_nodes[index], "Opacity")) return values.empty();
    m_hover.Set(id, values, *this);
    if (values.empty()) m_hovers.erase(index);
    else m_hovers[index] = values;
    return true;
}

DecorChange ReplayBackend::SetDecor(NodeId id, PseudoElement slot, const Decor* decor) {
    const int index = IndexOf(id);
    const auto key = std::make_pair(index, slot);
    if (!decor) return m_decors.erase(key) ? DecorChange::Applied : DecorChange::Unchanged;
    if (index < 0 || !m_nodes[index].reachable || !m_nodes[index].panel) return DecorChange::Refused;
    if (!decor->image.starts_with(kBarsContent) && !decor->image.starts_with(kArtworkImage)) {
        std::string path = decor->image.substr(0, decor->image.find('|'));
        std::error_code error;
        if (!std::filesystem::is_regular_file(std::filesystem::path(std::u8string(path.begin(), path.end())), error)) return DecorChange::Refused;
    }
    auto known = m_decors.find(key);
    if (known != m_decors.end() && known->second == *decor) return DecorChange::Unchanged;
    m_decors[key] = *decor;
    return DecorChange::Applied;
}

std::optional<NodeBox> ReplayBackend::Box(NodeId id) {
    int index = IndexOf(id);
    if (index < 0) return std::nullopt;
    return m_nodes[index].box;
}

}  // namespace ampify::theme
