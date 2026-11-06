#pragma once

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "hover.hpp"
#include "tree.hpp"

namespace ampify::theme {

// The offline bench's interface: a tree recorded by the inspector (logs\inspector.json, version 1 or 2), served to the
// real engine as if it were Apple Music's (docs/development/offline-bench.md). Read gives the recorded values, Write and
// Reparent change the copy and are all recorded, and what the live backend would refuse is refused here too, with the
// reason: a property the element's class does not have, a value of the wrong kind or out of range.
//
// What a dump does not hold, the backend works out or leaves out, and says so in Warnings():
//   - properties the inspector does not read (BorderThickness, MinWidth, Template...) exist when the element's class
//     has them, judged from the recorded ones (an element with a BorderBrush has a BorderThickness), at WinUI's defaults;
//   - a version 1 dump has no element flags (panel, icon, player): panels are judged from their recorded properties,
//     and no element is an icon or in a player window;
//   - boxes (version 2) only for the wireframe; a dump recorded under a theme gives the originals back (version 2),
//     except a value the element had none of its own for, which stays as recorded.
// An element the inspector could read nothing from (a template part WinUI never realized) is listed but unreachable,
// as in the live tree.
class ReplayBackend final : public TreeBackend, private HoverTracker::Access {
public:
    struct Node {
        std::string type;
        std::string fullType;
        std::string name;
        int parent = -1;  // index in Nodes(), -1 for a root
        bool icon = false;
        bool panel = false;
        bool player = false;
        bool playing = false;
        bool reachable = true;
        std::optional<NodeBox> box;
        std::map<std::string, Value, std::less<>> properties;  // as recorded, then as written
        std::vector<int> children;
    };

    // One call of Write, Reparent, WriteResource or SetDictionary, accepted or not.
    struct Change {
        int pass = 0;
        int node = -1;         // index in Nodes(); -1 for a resource or the dictionary
        std::string property;  // XAML property, "resource <key>", "dictionary", "parent" (Reparent)
        std::string before;    // as the inspector shows values
        std::string after;
        bool accepted = false;
        std::string reason;  // why it was refused
    };

    // Nothing, with `problem` set, for a file that is not an inspector.json. `asRecorded` keeps the values as recorded
    // under a theme instead of giving the originals back.
    static std::unique_ptr<ReplayBackend> Load(std::string_view json, std::string* problem = nullptr,
                                               bool asRecorded = false);

    // The pass that the next changes belong to, from 1.
    void BeginPass(int pass) { m_pass = pass; }
    // Overrides the recorded window size (version 1 dumps have none: @media rules then apply nowhere).
    void SetWindow(const ViewSize& size) { m_window = size; }

    // The app's light or dark mode, as the theme service sets it after each pass (XamlTreeBackend::SetAppMode: the
    // theme's mode, or nothing to leave Apple's). Apple's own colours follow the mode through {ThemeResource}; the
    // dump holds them in the mode it was recorded in, and the other mode's are not known.
    void SetAppMode(std::optional<bool> dark) { m_appMode = dark; }
    std::optional<bool> AppMode() const { return m_appMode; }
    // The mode the dump was recorded in, judged from its resources (the base background, else the primary text);
    // nothing when it holds neither.
    std::optional<bool> RecordedDark() const { return m_recordedDark; }
    // The mode an element shows: the nearest RequestedTheme the theme wrote on it or a parent (color-scheme), else
    // the app's, else the recorded one.
    std::optional<bool> ModeOf(int node) const;
    // True when the element shows another mode than the dump was recorded in, or a mode while the recorded one is
    // unknown: Apple's own colours there are not those the dump holds.
    bool ModeDiffers(int node) const;

    std::vector<NodeInfo> Snapshot() override;
    bool Alive(NodeId id) override;
    std::optional<Value> Read(NodeId id, std::string_view property) override;
    bool Write(NodeId id, std::string_view property, const Value& value) override;
    bool Reparent(NodeId id, NodeId parent, int index) override;
    std::vector<std::string> ResourceKeys() override;
    std::optional<Value> ReadResource(std::string_view key) override;
    bool WriteResource(std::string_view key, const Value& value) override;
    bool SetDictionary(std::string_view xaml) override;
    bool SetHover(NodeId id, const std::vector<HoverValue>& values) override;
    DecorChange SetDecor(NodeId id, PseudoElement slot, const Decor* decor) override;
    std::optional<ViewSize> WindowSize() override { return m_window; }
    std::optional<NodeBox> Box(NodeId id) override;

    const std::vector<Node>& Nodes() const { return m_nodes; }
    const std::vector<Change>& Changes() const { return m_changes; }
    // The values each element shows under the pointer, while focused or pressed, as the engine last gave them.
    const std::map<int, std::vector<HoverValue>>& Hovers() const { return m_hovers; }
    const std::map<std::pair<int, PseudoElement>, Decor>& Decors() const { return m_decors; }
    const std::string& Dictionary() const { return m_dictionary; }
    // The value the element shows with no state on, as the inspector shows it; nothing when it has no such property.
    std::optional<Value> Shown(int node, std::string_view property);
    const std::vector<std::string>& Warnings() const { return m_warnings; }
    // Resource keys written that the dump did not hold: taken as the live app would, unverified.
    const std::set<std::string>& UnrecordedResources() const { return m_unrecorded; }
    int Version() const { return m_version; }
    const std::string& RecordedTheme() const { return m_recordedTheme; }
    bool HasBoxes() const { return m_hasBoxes; }
    // The id the engine sees for the element at `node`, and back; -1 for an id that is no element.
    static NodeId IdOf(int node) { return static_cast<NodeId>(node) + 1; }
    int IndexOf(NodeId id) const;

private:
    std::optional<Value> ReadRaw(NodeId id, std::string_view property) override;
    bool WriteRaw(NodeId id, std::string_view property, const Value& value) override;
    // Why the live backend would refuse the value, empty when it takes it.
    std::string Refusal(const Node& node, std::string_view property, const Value& value) const;
    void Record(int node, std::string property, std::string before, std::string after, bool accepted, std::string reason);

    std::vector<Node> m_nodes;
    std::vector<int> m_roots;
    std::map<std::string, Value, std::less<>> m_resources;
    std::vector<std::string> m_resourceOrder;
    std::optional<ViewSize> m_window;
    std::optional<bool> m_appMode;
    std::optional<bool> m_recordedDark;
    std::vector<Change> m_changes;
    std::map<int, std::vector<HoverValue>> m_hovers;
    std::map<std::pair<int, PseudoElement>, Decor> m_decors;
    std::string m_dictionary;
    std::vector<std::string> m_warnings;
    std::set<std::string> m_unrecorded;
    std::string m_recordedTheme;
    std::string m_lastRefusal;  // set by WriteRaw for Write to record
    HoverTracker m_hover;
    int m_pass = 0;
    int m_version = 1;
    bool m_hasBoxes = false;
};

// Text and value of one recorded property, as the live backend's Read gives it ("present" is an object the engine
// cannot express, kept as an opaque value). Nothing for an unreadable text.
std::optional<Value> RecordedValue(std::string_view property, std::string_view text);

}  // namespace ampify::theme
