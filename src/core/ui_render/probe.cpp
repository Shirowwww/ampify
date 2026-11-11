#include "probe.hpp"

#include <utility>

#include "render_tree.hpp"

namespace ampify::uirender {

namespace {

bool NamedLike(const ProbeNode& node, std::string_view name) {
    return node.name == name || node.automationId == name;
}

// Without the spaces around it: spaces, no-break spaces, narrow no-break spaces and thin spaces.
std::string_view TrimSpaces(std::string_view text) {
    static constexpr std::string_view kSpaces[] = {" ", "\xC2\xA0", "\xE2\x80\xAF", "\xE2\x80\x89"};
    bool trimmed = true;
    while (trimmed) {
        trimmed = false;
        for (std::string_view space : kSpaces) {
            if (text.starts_with(space)) {
                text.remove_prefix(space.size());
                trimmed = true;
            }
            if (text.ends_with(space)) {
                text.remove_suffix(space.size());
                trimmed = true;
            }
        }
    }
    return text;
}

// The number of ToggleSwitch under each node, outside the mod elements. Parents come before their children, so one
// pass from the end adds each node's count to its parent.
std::vector<unsigned> ToggleCounts(const std::vector<ProbeNode>& nodes) {
    std::vector<unsigned> counts(nodes.size(), 0);
    std::vector<bool> excluded(nodes.size(), false);
    for (std::size_t index = 0; index < nodes.size(); index++) {
        int parent = nodes[index].parent;
        excluded[index] = IsAmpifyOrModName(nodes[index].name) || (parent >= 0 && excluded[parent]);
    }
    for (std::size_t index = nodes.size(); index-- > 0;) {
        if (excluded[index]) continue;
        if (nodes[index].Is(kProbeToggleSwitch)) counts[index]++;
        int parent = nodes[index].parent;
        if (parent >= 0) counts[parent] += counts[index];
    }
    return counts;
}

bool InsideItems(const std::vector<ProbeNode>& nodes, int node) {
    for (int current = nodes[node].parent; current >= 0; current = nodes[current].parent) {
        if (nodes[current].Is(kProbeItemsControl) || nodes[current].Is(kProbeItemsRepeater)) return true;
    }
    return false;
}

}  // namespace

bool ShouldDescend(const ProbeNode& node) {
    if (node.Is(kProbeItemsControl) || node.Is(kProbeItemsRepeater) || node.Is(kProbeButtonBase)) return false;
    return !IsOwnName(node.name);
}

std::optional<int> FindByClass(const std::vector<ProbeNode>& nodes, std::string_view fragment) {
    if (fragment.empty()) return std::nullopt;
    for (std::size_t index = 0; index < nodes.size(); index++) {
        if (nodes[index].className.find(fragment) != std::string::npos) return static_cast<int>(index);
    }
    return std::nullopt;
}

std::optional<int> FindByName(const std::vector<ProbeNode>& nodes, std::string_view name) {
    if (name.empty()) return std::nullopt;
    for (std::size_t index = 0; index < nodes.size(); index++) {
        if (NamedLike(nodes[index], name)) return static_cast<int>(index);
    }
    return std::nullopt;
}

bool IsUnder(const std::vector<ProbeNode>& nodes, int node, int ancestor) {
    for (int current = node; current >= 0; current = nodes[current].parent) {
        if (current == ancestor) return true;
    }
    return false;
}

bool InsideModElement(const std::vector<ProbeNode>& nodes, int node) {
    for (int current = node; current >= 0; current = nodes[current].parent) {
        if (IsAmpifyOrModName(nodes[current].name)) return true;
    }
    return false;
}

std::string QuotedMenuName(const std::vector<std::string>& texts) {
    // Opening and closing quotes in UTF-8: « », “ ”, „ “, 「 」 and straight ones.
    static constexpr std::pair<std::string_view, std::string_view> kQuotes[] = {
        {"\xC2\xAB", "\xC2\xBB"},
        {"\xE2\x80\x9C", "\xE2\x80\x9D"},
        {"\xE2\x80\x9E", "\xE2\x80\x9C"},
        {"\xE3\x80\x8C", "\xE3\x80\x8D"},
        {"\"", "\""},
    };
    for (const std::string& text : texts) {
        for (const auto& [open, close] : kQuotes) {
            std::size_t start = text.find(open);
            std::size_t end = text.rfind(close);
            if (start == std::string::npos || end == std::string::npos || end < start + open.size()) continue;
            std::string_view name = TrimSpaces(std::string_view(text).substr(start + open.size(), end - start - open.size()));
            if (!name.empty()) return std::string(name);
        }
    }
    return {};
}

std::optional<int> ChooseSettingsAnchor(const std::vector<ProbeNode>& nodes, int page) {
    if (page < 0 || page >= static_cast<int>(nodes.size())) return std::nullopt;
    std::vector<unsigned> counts = ToggleCounts(nodes);
    std::optional<int> best;
    unsigned bestCount = 0;
    for (std::size_t index = page; index < nodes.size(); index++) {
        int node = static_cast<int>(index);
        if (!nodes[index].Is(kProbeStackPanel) || !IsUnder(nodes, node, page) || InsideModElement(nodes, node)) continue;
        if (counts[index] > bestCount) {  // strictly more: on a tie the first one met, the outermost, stays
            best = node;
            bestCount = counts[index];
        }
    }
    return best;
}

std::vector<int> ChildrenOf(const std::vector<ProbeNode>& nodes, int parent) {
    std::vector<int> children;
    for (std::size_t index = 0; index < nodes.size(); index++) {
        if (nodes[index].parent == parent) children.push_back(static_cast<int>(index));
    }
    return children;
}

std::optional<int> ChooseTemplateRow(const std::vector<ProbeNode>& nodes, int anchor) {
    std::optional<int> row;
    for (int child : ChildrenOf(nodes, anchor)) {
        if (!IsAmpifyOrModName(nodes[child].name)) row = child;
    }
    return row;
}

std::optional<int> LastNativeOf(const std::vector<ProbeNode>& nodes, int root, unsigned flag) {
    std::optional<int> last;
    for (std::size_t index = 0; index < nodes.size(); index++) {
        int node = static_cast<int>(index);
        if (!nodes[index].Is(flag) || (root >= 0 && !IsUnder(nodes, node, root)) || InsideModElement(nodes, node)) continue;
        last = node;
    }
    return last;
}

std::optional<int> FirstNativeOf(const std::vector<ProbeNode>& nodes, int root, unsigned flag) {
    for (std::size_t index = 0; index < nodes.size(); index++) {
        int node = static_cast<int>(index);
        if (!nodes[index].Is(flag) || (root >= 0 && !IsUnder(nodes, node, root))) continue;
        if (InsideModElement(nodes, node) || InsideItems(nodes, node)) continue;
        return node;
    }
    return std::nullopt;
}

// Apple Music 1.1792: the buttons at the right of the play bar (volume, AirPlay, lyrics, queue), after which an Ampify
// button goes.
std::optional<Insertion> AfterToggleButtons(const std::vector<ProbeNode>& nodes) {
    std::optional<int> toggles = FindByName(nodes, "NowPlayingToggleButtons");
    if (!toggles || !nodes[*toggles].Is(kProbeStackPanel) || InsideModElement(nodes, *toggles)) return std::nullopt;
    Insertion insertion;
    insertion.parent = *toggles;
    insertion.after = -1;
    insertion.styleFrom = FirstNativeOf(nodes, *toggles, kProbeButtonBase).value_or(-1);
    return insertion;
}

namespace {

// The play bar buttons a control can stand beside, and the element of Apple's toggles each one is.
constexpr std::pair<std::string_view, std::string_view> kPlaybarNeighbours[] = {
    {"volume", "VolumeControl"},
};

std::string_view NeighbourName(std::string_view beside) {
    for (const auto& [name, element] : kPlaybarNeighbours) {
        if (name == beside) return element;
    }
    return {};
}

}  // namespace

bool IsPlaybarNeighbour(std::string_view beside) {
    return !NeighbourName(beside).empty();
}

std::optional<Insertion> ChoosePlaybarInsertion(const std::vector<ProbeNode>& nodes, std::string_view beside) {
    if (std::optional<Insertion> toggles = AfterToggleButtons(nodes)) {
        if (std::string_view element = NeighbourName(beside); !element.empty()) {
            for (int index = 0; index < static_cast<int>(nodes.size()); index++) {
                if (nodes[index].parent == toggles->parent && nodes[index].name == element) {
                    toggles->after = index;
                    break;
                }
            }
        }
        return toggles;
    }
    // Older versions: after the transport buttons.
    std::optional<int> anchor = FindByName(nodes, "TransportControl_SkipForward");
    if (!anchor) anchor = FindByName(nodes, "TransportControl_PlayPauseStop");
    if (!anchor) return std::nullopt;
    // The transport button may sit in a wrapper (a Grid around the button): climb to the first Panel.
    int child = *anchor;
    int parent = nodes[child].parent;
    while (parent >= 0 && !nodes[parent].Is(kProbePanel)) {
        child = parent;
        parent = nodes[parent].parent;
    }
    if (parent < 0 || InsideModElement(nodes, parent)) return std::nullopt;
    Insertion insertion;
    insertion.parent = parent;
    insertion.after = child;
    insertion.styleFrom = *anchor;
    return insertion;
}

std::optional<Insertion> ChooseTopbarInsertion(const std::vector<ProbeNode>& nodes) {
    // Apple Music 1.1792 has no separate top bar: its play bar is at the top, so a top bar button joins its buttons.
    if (std::optional<Insertion> toggles = AfterToggleButtons(nodes)) return toggles;
    std::optional<int> search = FirstNativeOf(nodes, -1, kProbeAutoSuggest);
    if (!search) return std::nullopt;
    for (int parent = nodes[*search].parent; parent >= 0; parent = nodes[parent].parent) {
        const ProbeNode& node = nodes[parent];
        if (InsideModElement(nodes, parent)) return std::nullopt;
        if ((node.Is(kProbeStackPanel) && node.Is(kProbeHorizontal)) || node.Is(kProbeGrid)) {
            Insertion insertion;
            insertion.parent = parent;
            insertion.after = -1;
            insertion.styleFrom = FirstNativeOf(nodes, parent, kProbeButtonBase).value_or(-1);
            return insertion;
        }
    }
    return std::nullopt;
}

std::uint64_t RefreshTicket::Request() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_closed || m_pending) return 0;
    m_pending = ++m_serial;
    return m_pending;
}

bool RefreshTicket::Take(std::uint64_t ticket) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_closed || !ticket || m_pending != ticket) return false;
    m_pending = 0;
    return true;
}

void RefreshTicket::Close() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_closed = true;
    m_pending = 0;
    m_layoutBudget = 0;
}

bool RefreshTicket::Closed() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_closed;
}

void RefreshTicket::ArmLayout(std::uint64_t nowMs) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_closed) return;
    m_layoutDeadline = nowMs + 750;
    m_layoutBudget = 4;
}

bool RefreshTicket::OnLayout(std::uint64_t nowMs) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_closed || !m_layoutBudget) return false;
    if (nowMs >= m_layoutDeadline) {
        m_layoutBudget = 0;
        return false;
    }
    --m_layoutBudget;
    return true;
}

void RefreshTicket::StopLayout() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_layoutBudget = 0;
}

}  // namespace ampify::uirender
