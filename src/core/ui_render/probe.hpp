#pragma once

// How Ampify finds its way in Apple Music's live XAML tree, without Windows: the Windows side (src/win/ui_service.cpp)
// copies a bounded part of the tree into a flat list of ProbeNode, and the functions below choose the page, the
// anchors and the template rows. Tested under Linux with fake trees (tests/ui_render_tests.cpp).
//
// The rules for the settings page, the bounded walk and the refresh ticket come from AMExclusive (MIT,
// https://github.com/AntiOblivionis/AMExclusive, mod/src/UiAdapter.cpp and mod/src/UiRefreshState.h at 91f589d),
// adapted to Ampify. The placement of the top bar and play bar buttons is provisional until the real tree of
// Apple Music is dumped (docs/development/testing-on-a-pc.md).

#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace ampify::uirender {

// What the Windows side knows of a node's type, as flags: try_as on the projected types.
enum ProbeFlag : unsigned {
    kProbePanel = 1u << 0,
    kProbeStackPanel = 1u << 1,
    kProbeToggleSwitch = 1u << 2,
    kProbeItemsControl = 1u << 3,
    kProbeItemsRepeater = 1u << 4,
    kProbeButtonBase = 1u << 5,
    kProbePage = 1u << 6,
    kProbeFrame = 1u << 7,
    kProbeUserControl = 1u << 8,
    kProbeHorizontal = 1u << 9,  // a StackPanel with Orientation Horizontal
    kProbeGrid = 1u << 10,
    kProbeTextBlock = 1u << 11,
    kProbeBorder = 1u << 12,
    kProbeComboBox = 1u << 13,
    kProbeSlider = 1u << 14,
    kProbeTextBox = 1u << 15,
    kProbeAutoSuggest = 1u << 16,
    kProbeNavigationView = 1u << 17,
};

struct ProbeNode {
    int parent = -1;          // index in the same list, parents first
    std::string className;    // runtime class name (winrt::get_class_name), such as "AppleMusic.PlaybackSettingsPage"
    std::string name;         // x:Name
    std::string automationId;
    unsigned flags = 0;

    bool Is(unsigned flag) const { return (flags & flag) != 0; }
};

// The discovery walks only these nodes' children: never into item lists (the library can hold thousands of rows),
// buttons, or Ampify's own elements.
bool ShouldDescend(const ProbeNode& node);

// The first node whose runtime class name contains `fragment`, such as "PlaybackSettingsPage".
std::optional<int> FindByClass(const std::vector<ProbeNode>& nodes, std::string_view fragment);

// The first node whose x:Name or AutomationId is `name`.
std::optional<int> FindByName(const std::vector<ProbeNode>& nodes, std::string_view name);

// True when `node` is `ancestor` or below it.
bool IsUnder(const std::vector<ProbeNode>& nodes, int node, int ancestor);

// True when `node` or one of its ancestors carries an Ampify or AMExclusive name.
bool InsideModElement(const std::vector<ProbeNode>& nodes, int node);

// The name of the item a menu is about (a song, an album, a playlist), from the texts of its entries: Apple Music's
// "Play «Name»" entry quotes it. The text between the first opening quote and the last closing one of the first entry
// that has both, without the spaces around it. Empty for a menu of options, which quotes nothing.
std::string QuotedMenuName(const std::vector<std::string>& texts);

// The settings anchor of a page: the StackPanel under `page` that holds the most ToggleSwitch (outside Ampify's and
// AMExclusive's elements). On a tie the outermost wins, as in AMExclusive. Nothing when the page has no toggle.
std::optional<int> ChooseSettingsAnchor(const std::vector<ProbeNode>& nodes, int page);

// The direct children of `parent`, in order.
std::vector<int> ChildrenOf(const std::vector<ProbeNode>& nodes, int parent);

// The row that serves as model for a new card: the last direct child of `anchor` that is not an Ampify or AMExclusive
// element. Nothing when the anchor has no such child.
std::optional<int> ChooseTemplateRow(const std::vector<ProbeNode>& nodes, int anchor);

// The last ToggleSwitch under `root` outside the mod elements: the native toggle whose style a new toggle copies.
std::optional<int> LastNativeOf(const std::vector<ProbeNode>& nodes, int root, unsigned flag);

// The first node under `root` (or anywhere when root < 0) with `flag`, outside the mod elements and outside item
// lists: the style probe takes the native Button, ComboBox, Slider and TextBox of the page from here.
std::optional<int> FirstNativeOf(const std::vector<ProbeNode>& nodes, int root, unsigned flag);

// Where a button of the play bar goes: after Apple Music's buttons at the right of the bar
// (StackPanel#NowPlayingToggleButtons, 1.1792), as Spicetify's play bar buttons; on older versions, in the panel that
// holds the transport buttons (TransportControl_SkipForward, TransportControl_PlayPauseStop), after the last of them.
struct Insertion {
    int parent = -1;     // the panel
    int after = -1;      // the sibling to insert after (its index in `nodes`), -1 for the end
    int styleFrom = -1;  // the native control whose style is copied, -1 for none
};
std::optional<Insertion> ChoosePlaybarInsertion(const std::vector<ProbeNode>& nodes);

// Where a button of the top bar goes: the horizontal StackPanel or Grid that holds the search box (an AutoSuggestBox),
// at its end. Provisional: the real tree may put the search box elsewhere.
std::optional<Insertion> ChooseTopbarInsertion(const std::vector<ProbeNode>& nodes);

// The bounded breadth-first walk of the discovery: at most `limit` nodes (512 by default). `children(node, push)`
// calls push(child) for each child it wants walked; push returns false once the budget is spent.
template <typename Node, typename Identity, typename Children>
std::size_t WalkBounded(Node root, Identity identity, Children children, std::size_t limit = 512) {
    std::deque<Node> pending;
    if (root) pending.push_back(root);
    std::unordered_set<std::uintptr_t> visited;
    while (!pending.empty() && visited.size() < limit) {
        Node node = pending.front();
        pending.pop_front();
        if (!node || !visited.insert(identity(node)).second) continue;
        children(node, [&](Node child) {
            if (!child) return true;
            if (visited.size() + pending.size() >= limit) return false;
            pending.push_back(child);
            return true;
        });
    }
    return visited.size();
}

// One queue ticket per live window: a refresh is queued at most once, a stale callback only retires its own ticket,
// and closing the window invalidates the queued work. LayoutUpdated wakes the refresh a few times (4 within 750 ms)
// after a page appears, until it is mounted. Port of AMExclusive's UiRefreshState.
class RefreshTicket {
public:
    std::uint64_t Request();
    bool Take(std::uint64_t ticket);
    void Close();
    bool Closed() const;
    void ArmLayout(std::uint64_t nowMs);
    bool OnLayout(std::uint64_t nowMs);
    void StopLayout();

private:
    mutable std::mutex m_mutex;
    std::uint64_t m_serial = 0;
    std::uint64_t m_pending = 0;
    std::uint64_t m_layoutDeadline = 0;
    unsigned m_layoutBudget = 0;
    bool m_closed = false;
};

}  // namespace ampify::uirender
