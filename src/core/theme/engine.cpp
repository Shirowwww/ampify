#include "engine.hpp"

#include <algorithm>
#include <cstdlib>
#include <iterator>
#include <optional>
#include <vector>

#include "pane.hpp"
#include "properties.hpp"

namespace ampify::theme {

namespace {

// The log's verbosity: the first pass of a theme names every value it writes, the next few passes give one summary
// line each while they still write something, and later passes stay silent (a tick every 3 s would flood the log).
constexpr int kSummarisedPasses = 5;

// The highest layer of the theme's own rules; snippets come above it (snippets.cpp).
constexpr int kThemeLayer = 0;

// A line-height factor or a letter-spacing in pixels, measured against the element's font size now: nothing for an
// element without one.
std::optional<Value> WithFontSize(NodeId id, const Value& value, TreeBackend& tree) {
    std::optional<Value> size = tree.Read(id, "FontSize");
    return size ? ApplyFontSize(value, *size) : std::nullopt;
}

// The text properties CSS inherits, which a panel passes to the texts inside it (InheritTextValues).
bool IsInheritedTextProperty(std::string_view property) {
    static constexpr std::string_view kInherited[] = {"CharacterSpacing", "FontFamily", "FontSize",      "FontStyle",
                                                      "FontWeight",       "Foreground", "LineHeight",    "TextAlignment"};
    return std::find(std::begin(kInherited), std::end(kInherited), property) != std::end(kInherited);
}

bool IsText(const NodeInfo& node) { return node.type == "TextBlock" || node.type == "RichTextBlock"; }

// A Fluent colour key the theme leaves to Apple Music ($Key that its [colors] do not define): the colour the app holds
// for it now. Any other value passes as it is; nothing when the app has no such colour.
std::optional<Value> AppColour(const Value& value, TreeBackend& tree) {
    if (value.kind != ValueKind::ColorKey) return value;
    std::optional<Value> colour = tree.ReadResource(value.text);
    if (!colour || colour->kind != ValueKind::Color) return std::nullopt;
    return colour;
}

// The scheme `@media (prefers-color-scheme)` follows: the theme's, as its loader decided. With none (snippets alone, or
// a theme that gives no background), Apple Music's own, from the background it holds now: a snippet's light rules
// darkened Apple's dark display. Unknown reads as light.
bool DarkScheme(const Theme& theme, TreeBackend& tree) {
    if (theme.dark) return *theme.dark;
    for (const char* key : {"SolidBackgroundFillColorBaseBrush", "ApplicationPageBackgroundThemeBrush"}) {
        std::optional<Value> background = tree.ReadResource(key);
        if (!background || background->kind != ValueKind::Color) continue;
        if (std::optional<Argb> colour = ParseColor(background->text)) return RelativeLuminance(*colour) < kDarkBelowLuminance;
    }
    return false;
}

std::string Describe(const NodeInfo& node) {
    std::string text = node.type;
    if (!node.name.empty()) text += "#" + node.name;
    return text;
}

// The ancestor a state value watches, for the log; a sibling (`A:hover + B`) is not looked for.
std::string WatchedName(const NodeInfo& node, NodeId watch, const std::vector<NodeInfo>& nodes) {
    for (int k = node.parent; k >= 0; k = nodes[k].parent) {
        if (nodes[k].id == watch) return Describe(nodes[k]);
    }
    return "a sibling";
}

// The assignments grouped by the last compound of their selector (the element that receives them): by #name when it
// has one, else by type, else in `any`. An element then only tries the groups of its own name and type, instead of
// every rule of the theme: a pass over Apple Music's ~5000 elements stays short.
struct RuleIndex {
    std::map<std::string, std::vector<const Assignment*>, std::less<>> byName;
    std::map<std::string, std::vector<const Assignment*>, std::less<>> byType;
    std::vector<const Assignment*> any;

    explicit RuleIndex(const std::vector<Assignment>& assignments) {
        for (const Assignment& assignment : assignments) {
            if (assignment.selector.parts.empty()) continue;
            const Compound& last = assignment.selector.parts.back();
            if (!last.name.empty()) {
                byName[last.name].push_back(&assignment);
            } else if (!last.type.empty()) {
                byType[last.type].push_back(&assignment);
            } else {
                any.push_back(&assignment);
            }
        }
    }

    template <typename Visit>
    void ForCandidates(const NodeInfo& node, Visit&& visit) const {
        auto group = [&](const auto& map, const std::string& key) {
            if (key.empty()) return;
            if (auto it = map.find(key); it != map.end()) {
                for (const Assignment* assignment : it->second) visit(*assignment);
            }
        };
        group(byName, node.name);
        group(byType, node.type);
        if (node.fullType != node.type) group(byType, node.fullType);
        for (const Assignment* assignment : any) visit(*assignment);
    }
};

// Each @media condition of the theme once, for TreeBackend::WatchSize.
std::vector<MediaQuery> MediaQueries(const Theme& theme) {
    std::vector<MediaQuery> queries;
    for (const Assignment& assignment : theme.assignments) {
        if (assignment.media && std::find(queries.begin(), queries.end(), *assignment.media) == queries.end()) {
            queries.push_back(*assignment.media);
        }
    }
    return queries;
}

// The index of the first element the selector matches, or -1 when none does or the selector cannot be read.
int FindContainer(const std::string& text, const std::vector<NodeInfo>& nodes) {
    std::optional<Selector> selector = ParseSelector(text);
    if (!selector) return -1;
    for (int i = 0; i < static_cast<int>(nodes.size()); i++) {
        if (Matches(*selector, nodes, i)) return i;
    }
    return -1;
}

// A dimmed sidebar (Cinema's house lights) is see-through as a whole: open over the pages, they showed through its
// entries. There its opacity leaves `winners`, so GiveBack puts Apple's back. True when the theme dims it at all.
bool KeepSidebarOpaque(const std::vector<NodeInfo>& nodes, int position,
                       std::map<std::string, const Assignment*>& winners, const std::optional<ViewSize>& size) {
    auto opacity = winners.find("Opacity");
    if (opacity == winners.end() || !SidebarPaneLayer(nodes, position)) return false;
    if (size && size->sidebarOverPages) winners.erase(opacity);
    return true;
}

}  // namespace

ThemeEngine::ThemeEngine(Journal& journal) : m_journal(journal) {}

ThemeEngine::Report ThemeEngine::Apply(const Theme& theme, TreeBackend& tree) {
    Report report;
    m_detailed = m_passes == 0;
    m_passes++;
    std::vector<NodeInfo> nodes = tree.Snapshot();

    // What the engine must give back to an element stays while the element exists, in the interface or out of it (the
    // entries of a closed menu). The elements that are gone are forgotten, so a long session stays small.
    std::set<NodeId> live;
    for (const NodeInfo& node : nodes) live.insert(node.id);
    std::map<NodeId, bool> absent;  // the elements missing from this snapshot, and whether they still exist
    auto kept = [&](NodeId id) {
        if (live.count(id)) return true;
        auto [it, fresh] = absent.try_emplace(id, false);
        if (fresh) it->second = tree.Alive(id);
        return it->second;
    };
    for (auto it = m_originals.begin(); it != m_originals.end();) {
        it = kept(it->first.first) ? std::next(it) : m_originals.erase(it);
    }
    std::erase_if(m_baseSizes, [&](const auto& entry) { return !kept(entry.first.first); });
    std::erase_if(m_moves, [&](const Move& move) { return !kept(move.id); });
    std::erase_if(m_reparents, [&](const Reparent& move) { return !kept(move.id); });
    std::erase_if(m_hovers, [&](const auto& entry) { return !kept(entry.first); });
    ForgetDecors(kept, tree);

    ApplyDictionary(theme, tree, report);
    ApplyResources(theme, tree, report);
    ApplyAssignments(theme, tree, nodes, report);
    m_active = true;

    const bool summarised = report.applied > 0 && m_passes <= kSummarisedPasses;
    if ((report.applied > 0 || report.refused > 0) && (m_detailed || summarised)) {
        m_journal.Write("Theme \"" + theme.name + "\": " + std::to_string(report.applied) + " value(s) applied, " +
                        std::to_string(report.unchanged) + " already in place, " + std::to_string(report.refused) +
                        " refused");
    }
    return report;
}

void ThemeEngine::ApplyAssignments(const Theme& theme, TreeBackend& tree, const std::vector<NodeInfo>& nodes,
                                   Report& report) {
    std::vector<PendingMove> pending;
    std::vector<PendingMove> reparents;
    const RuleIndex index(theme.assignments);
    const std::optional<ViewSize> size = tree.WindowSize();
    const bool darkScheme = DarkScheme(theme, tree);
    tree.WatchSize(MediaQueries(theme));
    tree.WatchStates(std::any_of(theme.assignments.begin(), theme.assignments.end(), [](const Assignment& assignment) {
        return UsesControlStates(assignment.selector.parts);
    }));
    std::vector<Winners> inherited(nodes.size());
    bool sidebarDimmed = false;
    for (int position = 0; position < static_cast<int>(nodes.size()); position++) {
        const NodeInfo& node = nodes[position];

        // The winning declaration for each property of this element, and apart from them those of its states and of
        // its ::before and ::after images.
        Winners winners;
        StateWinners stateWinners;
        DecorWinners decorWinners;
        DecorWinners themeDecorWinners;
        auto keep = [](auto& target, const auto& key, const Assignment& assignment) {
            auto [it, inserted] = target.try_emplace(key, &assignment);
            if (!inserted && Beats(assignment, *it->second)) it->second = &assignment;
        };
        index.ForCandidates(node, [&](const Assignment& assignment) {
            if (assignment.media && !(size && assignment.media->Matches(size->width, size->height, size->sidebarFolded))) {
                return;
            }
            if (assignment.media && !assignment.media->MatchesScheme(darkScheme)) return;
            if (!Matches(assignment.selector, nodes, position)) return;

            const Selector& selector = assignment.selector;
            if (selector.pseudo != PseudoElement::None) {
                keep(decorWinners, std::make_pair(selector.pseudo, assignment.property), assignment);
                if (assignment.layer <= kThemeLayer) {
                    keep(themeDecorWinners, std::make_pair(selector.pseudo, assignment.property), assignment);
                }
            } else if (selector.trigger == Trigger::Always) {
                keep(winners, assignment.property, assignment);
            } else {
                const bool own = selector.statePart + 1 >= selector.parts.size();
                const int watched = own ? -1 : MatchedElement(selector, nodes, position, selector.statePart);
                keep(stateWinners, std::make_tuple(selector.trigger, assignment.property, watched), assignment);
            }
        });
        std::optional<Assignment> fittedWidth;  // `winners` may point to it until the end of this element
        InheritTextValues(node, position, winners, inherited, tree);
        PlacePane(nodes, position, winners, fittedWidth, tree, report);
        sidebarDimmed = KeepSidebarOpaque(nodes, position, winners, size) || sidebarDimmed;

        // The transition first, so a transform or opacity written in the same pass already animates. The animation
        // last, so it starts from the opacity and around the origin the theme gives the element.
        // The font size before the values measured in it (NeedsFontSize).
        for (const char* first : {"Transition", "FontSize"}) {
            if (auto it = winners.find(first); it != winners.end()) WriteValue(node, *it->second, tree, report, true);
        }
        for (const auto& [property, assignment] : winners) {
            if (property == "Transition" || property == "FontSize" || property == "Animation") continue;
            if (property == kOrderProperty) {
                pending.push_back({&node, assignment});
                continue;
            }
            if (property == kMoveToProperty) {
                reparents.push_back({&node, assignment});
                continue;
            }
            WriteValue(node, *assignment, tree, report, true);
        }
        if (auto animation = winners.find("Animation"); animation != winners.end()) {
            WriteValue(node, *animation->second, tree, report, true);
        }
        GiveBack(node, winners, tree, report);
        // An element that lost its state rules (a theme edited live) is visited too, to take its states off.
        if (!stateWinners.empty() || m_hovers.count(node.id)) ApplyHover(node, stateWinners, nodes, tree, report);
        if (!decorWinners.empty() || m_decors.count({node.id, PseudoElement::Before}) ||
            m_decors.count({node.id, PseudoElement::After})) {
            ApplyDecors(node, decorWinners, themeDecorWinners, tree, report);
        }
    }
    tree.WatchSidebarOverPages(sidebarDimmed);
    ApplyMoves(pending, tree, report);
    GiveBackReparents(reparents, nodes, tree, report);
    ApplyReparents(reparents, nodes, tree, report);
}

// A panel (a Grid, a StackPanel) has no colour or font of its own: a rule giving it one means the texts inside, as in
// CSS. `.shelf-title` names the Grid that holds a shelf's title, so its font goes to the title's texts, while its
// background and sizes stay on the Grid. A text's own rule wins, the nearest panel wins over those above it, and an
// element that takes the property itself (a control with its own rule for it) stops what came from above.
void ThemeEngine::InheritTextValues(const NodeInfo& node, int position, Winners& winners,
                                    std::vector<Winners>& inherited, TreeBackend& tree) {
    Winners passed = node.parent >= 0 ? inherited[node.parent] : Winners{};
    for (auto it = winners.begin(); it != winners.end();) {
        if (!IsInheritedTextProperty(it->first)) {
            ++it;
        } else if (node.panel && !tree.Read(node.id, it->first)) {
            passed[it->first] = it->second;
            it = winners.erase(it);
        } else {
            passed.erase(it->first);
            ++it;
        }
    }
    if (IsText(node)) {
        for (const auto& [property, assignment] : passed) winners.try_emplace(property, assignment);
    }
    inherited[position] = std::move(passed);
}

// Moves back each element of the snapshot that a `move-to` rule no longer moves: a setting changed, an @media stopped
// matching, a rule edited out. The session goes over the same theme without a Restore then, so nothing else would.
void ThemeEngine::GiveBackReparents(const std::vector<PendingMove>& pending, const std::vector<NodeInfo>& nodes,
                                    TreeBackend& tree, Report& report) {
    if (m_reparents.empty()) return;
    std::set<NodeId> moving;
    for (const PendingMove& move : pending) moving.insert(move.node->id);
    std::map<NodeId, const NodeInfo*> listed;
    for (const NodeInfo& node : nodes) listed.emplace(node.id, &node);
    // In the order they were moved, as Restore does: the places are ascending.
    for (auto it = m_reparents.begin(); it != m_reparents.end();) {
        auto node = listed.find(it->id);
        if (moving.count(it->id) || node == listed.end()) {
            ++it;
            continue;
        }
        if (tree.Reparent(it->id, it->parent, it->index)) report.applied++;
        m_journal.Write("Moved back: " + Describe(*node->second));
        it = m_reparents.erase(it);
    }
}

// The ::before and ::after images of one element: put in or kept when a `content` image wins, taken off when none does
// any more (`content: none`, a rule edited out, the element out of the selector).
void ThemeEngine::ApplyDecors(const NodeInfo& node, const DecorWinners& allWinners, const DecorWinners& themeWinners,
                              TreeBackend& tree, Report& report) {
    for (PseudoElement slot : {PseudoElement::Before, PseudoElement::After}) {
        // A snippet that brings its own image or bars to a slot where the theme has one leaves that slot whole to the
        // theme (an equalizer that is a theme's signature stays as the theme draws it); without a content of its own,
        // a snippet still tunes the theme's decor.
        const auto given = [slot](const DecorWinners& winners) -> const Assignment* {
            auto found = winners.find({slot, std::string(kContentProperty)});
            return found == winners.end() || found->second->value.text.empty() ? nullptr : found->second;
        };
        const Assignment* overall = given(allWinners);
        const bool themeKeeps = given(themeWinners) && overall && overall->layer > kThemeLayer;
        const DecorWinners& winners = themeKeeps ? themeWinners : allWinners;
        auto content = winners.find({slot, std::string(kContentProperty)});
        auto known = m_decors.find({node.id, slot});
        if (content == winners.end() || content->second->value.text.empty()) {
            if (known == m_decors.end()) continue;
            if (tree.SetDecor(node.id, slot, nullptr) == DecorChange::Applied) report.applied++;
            m_journal.Write("Decor removed: " + Describe(node) + PseudoElementName(slot));
            m_decors.erase(known);
            continue;
        }
        Decor decor{content->second->value.text, {}};
        for (const auto& [key, assignment] : winners) {
            if (key.first != slot || key.second == kContentProperty) continue;
            std::optional<Value> given = AppColour(assignment->value, tree);
            if (!given) {
                Refuse(Describe(node) + PseudoElementName(slot) + " " + key.second,
                       "Apple Music has no colour " + assignment->value.text, report, node.id);
                continue;
            }
            decor.properties.emplace_back(key.second, std::move(*given));
        }
        PlaceDecor(node, slot, decor, *content->second, tree, report);
    }
}

void ThemeEngine::PlaceDecor(const NodeInfo& node, PseudoElement slot, const Decor& decor, const Assignment& content,
                             TreeBackend& tree, Report& report) {
    const std::string where = Describe(node) + PseudoElementName(slot);
    if (!node.panel) {
        Refuse(where, "\"" + content.selector.text + "\": " + PseudoElementName(slot) +
                          " needs a panel (Grid, StackPanel...); " + Describe(node) + " is a " + node.type,
               report, node.id);
        return;
    }
    switch (tree.SetDecor(node.id, slot, &decor)) {
        case DecorChange::Refused:
            // A record already there stays, so Restore still takes off what the panel may hold.
            Refuse(where, "\"" + where + "\" refused its image " + decor.image, report, node.id);
            return;
        case DecorChange::Unchanged:
            report.unchanged++;
            break;
        case DecorChange::Applied:
            report.applied++;
            if (m_detailed) {
                m_journal.Write("Decor: " + where + " = " + decor.image + ", " + std::to_string(decor.properties.size()) +
                                " value(s) (" + content.source + " line " + std::to_string(content.line) + ")");
            }
            break;
    }
    m_decors[{node.id, slot}] = decor;
}

// Lets the backend release the image of a panel that no longer exists.
void ThemeEngine::ForgetDecors(const std::function<bool(NodeId)>& kept, TreeBackend& tree) {
    for (auto it = m_decors.begin(); it != m_decors.end();) {
        if (kept(it->first.first)) {
            ++it;
            continue;
        }
        tree.SetDecor(it->first.first, it->first.second, nullptr);
        it = m_decors.erase(it);
    }
}

// Puts back the original of each property the theme no longer gives this element: an @media rule that stopped
// matching, a rule edited out of a theme, an element that moved out of a selector.
void ThemeEngine::GiveBack(const NodeInfo& node, const std::map<std::string, const Assignment*>& winners,
                           TreeBackend& tree, Report& report) {
    for (bool templates : {true, false}) {  // the template first, as in Restore
        for (auto it = m_originals.lower_bound({node.id, std::string()});
             it != m_originals.end() && it->first.first == node.id;) {
            if ((it->first.second == kTemplateProperty) != templates || winners.count(it->first.second)) {
                ++it;
                continue;
            }
            if (tree.Write(node.id, it->first.second, it->second)) report.applied++;
            tree.GaveBack(node.id, it->first.second);
            m_baseSizes.erase(it->first);
            it = m_originals.erase(it);
        }
    }
}

// The state values of one element, in the order of the cascade among the values of one state and property, so the
// backend shows the winner when several watched elements are in that state. A property the element does not have is
// refused here, as a plain value would be.
std::vector<HoverValue> ThemeEngine::StateValues(const NodeInfo& node, const StateWinners& winners,
                                                 const std::vector<NodeInfo>& nodes, TreeBackend& tree, Report& report) {
    std::vector<std::pair<HoverValue, const Assignment*>> ranked;
    for (const auto& [key, assignment] : winners) {
        const auto& [trigger, property, watched] = key;
        std::optional<Value> current = tree.Read(node.id, property);
        if (!current) {
            if (tree.Alive(node.id)) {
                Refuse(Describe(node) + " " + property, "\"" + Describe(node) + "\" has no property " + property, report, node.id);
            }
            continue;
        }
        std::optional<Value> given = AppColour(assignment->value, tree);
        if (!given) {
            Refuse(Describe(node) + " " + property, "Apple Music has no colour " + assignment->value.text, report, node.id);
            continue;
        }
        // A relative size under the pointer scales the size underneath, the theme's own when it has one.
        std::optional<Value> value = IsRelativeSize(*given)  ? ScaleFontSize(*given, *current)
                                     : NeedsFontSize(*given) ? WithFontSize(node.id, *given, tree)
                                                             : given;
        if (!value) {
            Refuse(Describe(node) + " " + property, "\"" + Describe(node) + " " + property + "\" has no size to scale",
                   report, node.id);
            continue;
        }
        const NodeId watch = watched < 0 ? 0 : nodes[watched].id;
        ranked.push_back({HoverValue{property, *value, trigger, watch}, assignment});
    }
    std::stable_sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) {
        if (a.first.trigger != b.first.trigger) return a.first.trigger < b.first.trigger;
        if (a.first.property != b.first.property) return a.first.property < b.first.property;
        return Beats(*a.second, *b.second);
    });
    std::vector<HoverValue> values;
    for (auto& entry : ranked) values.push_back(std::move(entry.first));
    return values;
}

// Gives the backend the state values of one element when they differ from the ones it holds. No value left takes the
// states off.
void ThemeEngine::ApplyHover(const NodeInfo& node, const StateWinners& winners, const std::vector<NodeInfo>& nodes,
                             TreeBackend& tree, Report& report) {
    std::vector<HoverValue> values = StateValues(node, winners, nodes, tree, report);
    auto known = m_hovers.find(node.id);
    if (known != m_hovers.end() && known->second == values) {
        report.unchanged += static_cast<int>(values.size());
        return;
    }
    if (known == m_hovers.end() && values.empty()) return;

    if (!tree.SetHover(node.id, values)) {
        Refuse(Describe(node) + " :hover", "\"" + Describe(node) + "\" does not follow its states (" + StateList(", ") + ")",
               report, node.id);
        return;
    }
    if (values.empty()) {
        m_hovers.erase(node.id);
        if (m_detailed) m_journal.Write("Hover removed: " + Describe(node));  // like "Hover:", once per element
        return;
    }
    report.applied += static_cast<int>(values.size());
    std::string list;
    for (const HoverValue& value : values) {
        list += (list.empty() ? "" : ", ") + value.property + " = " + ShownText(value.property, value.value);
        if (value.watch != 0) {
            list += std::string(" (") + TriggerName(value.trigger) + " of " + WatchedName(node, value.watch, nodes) + ")";
        } else if (value.trigger != Trigger::Hover) {
            list += std::string(" (") + TriggerName(value.trigger) + ")";
        }
    }
    if (m_detailed) m_journal.Write("Hover: " + Describe(node) + " " + list);
    m_hovers[node.id] = std::move(values);
}

// The values the template sets on its pane leave `winners`, so GiveBack also takes back one written before. `width:
// auto` becomes the width that fits the pane's column: a pane sized by its content loops WinUI's layout.
void ThemeEngine::PlacePane(const std::vector<NodeInfo>& nodes, int position,
                            std::map<std::string, const Assignment*>& winners, std::optional<Assignment>& fitted,
                            TreeBackend& tree, Report& report) {
    const int owner = PaneOwner(nodes, position);
    if (owner < 0) return;
    const NodeInfo& node = nodes[position];
    const std::string where = "\"" + Describe(node) + "\" is placed by its SplitView's template: ";
    for (auto it = winners.begin(); it != winners.end();) {
        if (!TemplatePlacesPane(it->first)) {
            ++it;
            continue;
        }
        Refuse(Describe(node) + " " + it->first, where + it->first, report);
        it = winners.erase(it);
    }
    auto width = winners.find("Width");
    if (width == winners.end() || width->second->value.text != "NaN") return;
    auto margin = winners.find("Margin");
    std::optional<Value> sides = margin != winners.end() ? std::optional<Value>(margin->second->value)
                                                         : tree.Read(node.id, "Margin");
    std::optional<Value> length = tree.Read(nodes[owner].id, "OpenPaneLength");
    std::optional<double> fit = sides && length ? FittedPaneWidth(length->text, sides->text) : std::nullopt;
    if (!fit) {
        Refuse(Describe(node) + " Width", where + "Width auto, with no open pane length to fit", report);
        winners.erase(width);
        return;
    }
    fitted = *width->second;
    fitted->value = Value{ValueKind::Length, FormatNumber(*fit)};
    width->second = &*fitted;
}

// Writes one value when it differs from the current one. `keep` stores the first value seen, for Restore.
bool ThemeEngine::WriteValue(const NodeInfo& node, const Assignment& assignment, TreeBackend& tree, Report& report,
                             bool keep) {
    const std::string& property = assignment.property;
    if (node.icon && property == "FontFamily") {
        report.unchanged++;  // left in its icon font on purpose
        return false;
    }
    std::optional<Value> current = tree.Read(node.id, property);
    if (!current) {
        // Gone since the snapshot, or a template part WinUI never realized: nothing the theme did wrong.
        if (tree.Alive(node.id)) {
            Refuse(Describe(node) + " " + property, "\"" + Describe(node) + "\" has no property " + property, report, node.id);
        }
        return false;
    }
    std::optional<Value> given = AppColour(assignment.value, tree);
    if (!given) {
        Refuse(Describe(node) + " " + property, "Apple Music has no colour " + assignment.value.text, report, node.id);
        return false;
    }
    std::optional<Value> value = Resolve(node.id, property, *given, *current, tree);
    if (!value) {
        Refuse(Describe(node) + " " + property, "\"" + Describe(node) + " " + property + "\" has no size to scale", report, node.id);
        return false;
    }
    if (SameValue(*current, *value)) {
        report.unchanged++;
        return false;
    }

    auto key = std::make_pair(node.id, property);
    if (keep && !m_originals.count(key)) m_originals.emplace(key, tree.ReadOriginal(node.id, property).value_or(*current));

    if (!tree.Write(node.id, property, *value)) {
        std::string reason = property == "Transition"
                                 ? "\"" + Describe(node) + "\" is animated by Apple Music: transition refused, its "
                                   "changes apply at once"
                             : property == "Animation"
                                 ? "\"" + Describe(node) + "\" refused the animation: Apple Music moves this element "
                                   "itself, or the theme also gives it a transform"
                                 : "value refused for \"" + Describe(node) + " " + property + "\"";
        Refuse(Describe(node) + " " + property, reason, report, node.id);
        return false;
    }
    if (!keep) m_moves.push_back({node.id, *current});
    report.applied++;
    if (m_detailed) m_journal.Write("Applied: " + Describe(node) + " " + property + " = " + ShownText(property, *value) + " (" +
                    assignment.source + " line " + std::to_string(assignment.line) + ")");
    return true;
}

// A relative font size becomes pixels: the factor times the size the element had before the theme, kept from the
// first pass since the next ones read back the theme's own. A value measured in the font size takes the size the
// element has now, the theme's own included. Other values pass as they are.
std::optional<Value> ThemeEngine::Resolve(NodeId id, const std::string& property, const Value& value,
                                          const Value& current, TreeBackend& tree) {
    if (NeedsFontSize(value)) return WithFontSize(id, value, tree);
    if (!IsRelativeSize(value)) return value;
    const Value& before = m_baseSizes.try_emplace({id, property}, current).first->second;
    return ScaleFontSize(value, before);
}

// Moves the elements in the order of their target position: placing position 0 first, then 1, and so on, never
// disturbs an element already placed, so one pass is enough. Each move is recorded, Restore undoes them backwards.
void ThemeEngine::ApplyMoves(std::vector<PendingMove>& pending, TreeBackend& tree, Report& report) {
    auto position = [](const PendingMove& move) { return std::atoi(move.assignment->value.text.c_str()); };
    std::stable_sort(pending.begin(), pending.end(),
                     [&](const PendingMove& a, const PendingMove& b) { return position(a) < position(b); });
    for (const PendingMove& move : pending) WriteValue(*move.node, *move.assignment, tree, report, false);
}

// Moves each element into the first element its `move-to` selector names, unless it is there already. The place it
// had is kept the first time only, for Restore. Each pass checks again, so an element Apple rebuilt moves too.
void ThemeEngine::ApplyReparents(const std::vector<PendingMove>& pending, const std::vector<NodeInfo>& nodes,
                                 TreeBackend& tree, Report& report) {
    for (const PendingMove& move : pending) {
        const NodeInfo& node = *move.node;
        const std::string& selectorText = move.assignment->value.text;
        const int container = FindContainer(selectorText, nodes);
        if (container < 0) {
            Refuse(Describe(node) + " move-to", "\"" + Describe(node) + "\" moves to \"" + selectorText +
                       "\": no element matches it",
                   report, node.id);
            continue;
        }
        const NodeInfo& target = nodes[container];
        if (node.parent < 0) continue;
        if (nodes[node.parent].id == target.id) {
            report.unchanged++;
            continue;
        }
        if (!tree.Reparent(node.id, target.id, -1)) {
            Refuse(Describe(node) + " move-to", "\"" + Describe(node) + "\" could not move into " + Describe(target),
                   report, node.id);
            continue;
        }
        bool recorded = std::any_of(m_reparents.begin(), m_reparents.end(),
                                    [&](const Reparent& known) { return known.id == node.id; });
        if (!recorded) m_reparents.push_back({node.id, nodes[node.parent].id, node.position});
        report.applied++;
        if (m_detailed) m_journal.Write("Moved: " + Describe(node) + " into " + Describe(target));
    }
}

void ThemeEngine::ApplyResources(const Theme& theme, TreeBackend& tree, Report& report) {
    // A resource the theme no longer sets (a key edited out, a scheme without it) gets its original back.
    for (auto it = m_resourceOriginals.begin(); it != m_resourceOriginals.end();) {
        bool kept = std::any_of(theme.resources.begin(), theme.resources.end(),
                                [&](const ResourceOverride& resource) { return resource.key == it->first; });
        if (kept) {
            ++it;
            continue;
        }
        if (tree.WriteResource(it->first, it->second)) report.applied++;
        it = m_resourceOriginals.erase(it);
    }
    for (const ResourceOverride& resource : theme.resources) {
        std::optional<Value> current = tree.ReadResource(resource.key);
        if (!current) {
            Refuse("resource " + resource.key, "resource missing from the app: " + resource.key, report);
            continue;
        }
        if (*current == resource.value) {
            report.unchanged++;
            continue;
        }

        if (!m_resourceOriginals.count(resource.key)) m_resourceOriginals.emplace(resource.key, *current);
        if (!tree.WriteResource(resource.key, resource.value)) {
            Refuse("resource " + resource.key, "resource refused: " + resource.key, report);
            continue;
        }
        report.applied++;
        m_journal.Write("Resource: " + resource.key + " = " + resource.value.text);
    }
}

void ThemeEngine::ApplyDictionary(const Theme& theme, TreeBackend& tree, Report& report) {
    if (theme.resourceDictionary == m_dictionary) return;

    if (!tree.SetDictionary(theme.resourceDictionary)) {
        Refuse("dictionary", "resource dictionary refused by the interface", report);
        return;
    }
    m_dictionary = theme.resourceDictionary;
    report.applied++;
    m_journal.Write(m_dictionary.empty() ? "Resource dictionary removed"
                                         : "Resource dictionary merged (resources.xaml)");
}

void ThemeEngine::Refuse(const std::string& key, const std::string& message, Report& report, NodeId node) {
    report.refused++;
    Refusal& refusal = m_refusals[key];
    if (refusal.message.empty()) refusal.message = message;
    if (node && m_recordRefusedElements) refusal.elements.insert(node);
    if (m_reported.insert(key).second) m_journal.Write("Refused: " + message);
}

void ThemeEngine::Restore(TreeBackend& tree) {
    tree.WatchStates(false);
    int restored = 0;
    int gone = 0;

    // The hovers first: an element under the pointer gets back the values underneath before those are restored too.
    for (const auto& [id, values] : m_hovers) {
        if (tree.SetHover(id, {})) {
            restored++;
        } else {
            gone++;
        }
    }
    for (const auto& [key, decor] : m_decors) {
        if (tree.SetDecor(key.first, key.second, nullptr) == DecorChange::Applied) {
            restored++;
        } else {
            gone++;
        }
    }
    // Moved elements go back first, in the order they were moved: the places are ascending, so each one finds the
    // children before it in place.
    for (const Reparent& move : m_reparents) {
        if (tree.Reparent(move.id, move.parent, move.index)) {
            restored++;
        } else {
            gone++;
        }
    }
    for (auto it = m_moves.rbegin(); it != m_moves.rend(); ++it) {
        if (tree.Write(it->id, kOrderProperty, it->before)) {
            restored++;
        } else {
            gone++;
        }
    }
    // A control's own template before its other values: a theme template's Border refuses some of Apple Music's own
    // values (the search box's buttons have a negative padding), and WinUI then stops the app.
    for (bool templates : {true, false}) {
        for (const auto& [key, original] : m_originals) {
            if ((key.second == kTemplateProperty) != templates) continue;
            if (tree.Write(key.first, key.second, original)) {
                restored++;
            } else {
                gone++;
            }
            tree.GaveBack(key.first, key.second);
        }
    }
    for (const auto& [key, original] : m_resourceOriginals) {
        if (tree.WriteResource(key, original)) {
            restored++;
        } else {
            gone++;
        }
    }

    if (!m_dictionary.empty()) {
        if (tree.SetDictionary("")) {
            restored++;
        } else {
            gone++;
        }
        m_dictionary.clear();
    }

    m_hovers.clear();
    m_decors.clear();
    m_originals.clear();
    m_baseSizes.clear();
    m_resourceOriginals.clear();
    m_moves.clear();
    m_reparents.clear();
    m_reported.clear();
    m_refusals.clear();
    m_active = false;
    m_passes = 0;
    m_journal.Write("Theme removed: " + std::to_string(restored) + " value(s) restored, " + std::to_string(gone) +
                    " element(s) gone");
}

}  // namespace ampify::theme
