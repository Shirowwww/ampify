// The portable half of the native renderer: render trees, reconciliation, node names, surfaces, anchors in a fake
// XAML tree, the bounded walk, the refresh ticket and the Mods card files. The Windows half (win/ui_renderer.cpp,
// win/ui_service.cpp) follows these decisions; it is only compiled by the MSVC job with the WinUI projection.

#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "../core/ui_model.hpp"
#include "../core/ui_render/mods.hpp"
#include "../core/ui_render/probe.hpp"
#include "../core/ui_render/render_tree.hpp"
#include "../core/ui_render/surfaces.hpp"
#include "../core/ui_render/svg_icon.hpp"
#include "test.hpp"

namespace {

namespace fs = std::filesystem;
using namespace ampify::uirender;

ampify::Json Parse(const std::string& text) {
    std::optional<ampify::Json> value = ampify::Json::Parse(text);
    return value ? *value : ampify::Json();
}

RenderNode Tree(const std::string& text) {
    return BuildRenderTree(Parse(text), "panel-test");
}

// A fake native widget tree that applies patches the way win/ui_renderer.cpp does: Keep and Update reuse the widget,
// Create and Replace build a new one. Each widget has a serial number, so a test sees whether a control survived.
struct Widget {
    int serial = 0;
    std::string type;
    std::string label;
    std::vector<std::shared_ptr<Widget>> children;
};

struct FakeRenderer {
    int nextSerial = 1;
    int built = 0;

    std::shared_ptr<Widget> Build(const RenderNode& node) {
        auto widget = std::make_shared<Widget>();
        widget->serial = nextSerial++;
        widget->type = node.type;
        widget->label = node.Text("label", node.Text("text"));
        built++;
        for (const RenderNode& child : node.children) widget->children.push_back(Build(child));
        return widget;
    }

    std::shared_ptr<Widget> Apply(const Patch& patch, std::shared_ptr<Widget> existing) {
        if (patch.op == PatchOp::Create || patch.op == PatchOp::Replace) return Build(*patch.node);
        if (patch.op == PatchOp::Update) existing->label = patch.node->Text("label", patch.node->Text("text"));
        std::vector<std::shared_ptr<Widget>> next;
        for (const Patch& child : patch.children) {
            next.push_back(Apply(child, child.from >= 0 ? existing->children[child.from] : nullptr));
        }
        existing->children = std::move(next);
        return existing;
    }
};

// Fake XAML tree for the probe: built depth first, parents before children, as the Windows side copies it.
struct FakeTree {
    std::vector<ProbeNode> nodes;

    int Add(int parent, const std::string& className, unsigned flags, const std::string& name = "") {
        ProbeNode node;
        node.parent = parent;
        node.className = className;
        node.flags = flags;
        node.name = name;
        nodes.push_back(node);
        return static_cast<int>(nodes.size()) - 1;
    }
};

const unsigned kStack = kProbePanel | kProbeStackPanel;

fs::path FreshKit(const std::string& name) {
    fs::path dir = fs::temp_directory_path() / "ampify-tests" / ("ui-render-" + name);
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

void WriteFile(const fs::path& file, const std::string& text) {
    fs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << text;
}

std::string ReadFile(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

void AddExtension(const fs::path& kit, const std::string& id, const std::string& name) {
    WriteFile(kit / "extensions" / id / "manifest.json",
              R"({"id":")" + id + R"(","name":")" + name + R"(","version":"1.0.0","kind":"extension","author":"x",)"
              R"("description":"Makes )" + name + R"(","kitApi":"1.0","main":"main.js","permissions":[]})");
    WriteFile(kit / "extensions" / id / "main.js", "Ampify.log('ok');");
}

}  // namespace

TEST_CASE(UiRender_NodeIdsFollowPositionsAndKeys) {
    RenderNode root = Tree(R"({"type":"stack","children":[{"type":"text","text":"a"},)"
                           R"({"type":"list","children":[{"type":"text","key":"piste 3","text":"b"},{"type":"text","text":"c"}]}]})");
    CHECK(root.id == "panel-test");
    CHECK(root.children[0].id == "panel-test.0");
    CHECK(root.children[1].children[0].id == "panel-test.1.kpiste-3");
    CHECK(root.children[1].children[1].id == "panel-test.1.1");
    CHECK(root.children[0].Text("text") == "a");
    CHECK(root.attributes.count("type") == 0);
    CHECK(CountNodes(root) == 5);
}

TEST_CASE(UiRender_RepeatedKeysFallBackToThePosition) {
    RenderNode root = Tree(R"({"type":"list","children":[{"type":"text","key":"x"},{"type":"text","key":"x"}]})");
    CHECK(root.children[0].id == "panel-test.kx");
    CHECK(root.children[1].id == "panel-test.1");
}

TEST_CASE(UiRender_ElementNamesAreSafeForXaml) {
    CHECK(ElementName("react-panel", "panel-react-panel.0.1") == "AMPIFY_react-panel_panel-react-panel.0.1");
    CHECK(ElementName("a b", "x/y") == "AMPIFY_a_b_x_y");
    CHECK(SurfaceNodeId("page", "/stats/top") == "page-stats-top");
    CHECK(SurfaceNodeId("popup", "") == "popup-main");
    CHECK(IsOwnName("AMPIFY_ModsCard"));
    CHECK(IsOwnName("AmpifyThemeSection"));
    CHECK(!IsOwnName("AMExclusiveModeCard"));
    CHECK(IsForeignModName("AMExclusiveModeCard"));
}

TEST_CASE(UiRender_AttributesReadAsTextNumberAndBoolean) {
    RenderNode node = Tree(R"({"type":"slider","min":5,"max":"120","value":30.5,"disabled":true,"label":"Length"})");
    CHECK(node.Number("min", 0) == 5);
    CHECK(node.Number("max", 0) == 120);
    CHECK(node.Text("min") == "5");
    CHECK(node.Bool("disabled", false));
    CHECK(!node.Bool("checked", false));
    CHECK(node.Text("label") == "Length");
    CHECK(node.Text("absent", "x") == "x");
}

TEST_CASE(UiRender_FirstRenderCreatesEverything) {
    RenderNode next = Tree(R"({"type":"stack","children":[{"type":"text","text":"a"}]})");
    Patch patch = Reconcile(nullptr, next);
    CHECK(patch.op == PatchOp::Create);
    PatchStats stats = CountPatch(patch);
    CHECK(stats.created == 2);
    CHECK(!stats.Unchanged());
}

TEST_CASE(UiRender_SameTreeKeepsEveryControl) {
    std::string text = R"({"type":"stack","children":[{"type":"text","text":"a"},{"type":"button","label":"OK","onClick":3}]})";
    RenderNode old = Tree(text);
    RenderNode next = Tree(text);
    Patch patch = Reconcile(&old, next);
    CHECK(patch.op == PatchOp::Keep);
    CHECK(!patch.reordered);
    CHECK(CountPatch(patch).Unchanged());
    CHECK(CountPatch(patch).kept == 3);
}

TEST_CASE(UiRender_ChangedLabelIsUpdatedInPlace) {
    FakeRenderer renderer;
    RenderNode first = Tree(R"({"type":"stack","children":[{"type":"text","text":"Clicks: 0"},{"type":"button","label":"Add one"}]})");
    std::shared_ptr<Widget> mounted = renderer.Build(first);
    int textSerial = mounted->children[0]->serial;
    int buttonSerial = mounted->children[1]->serial;

    RenderNode second = Tree(R"({"type":"stack","children":[{"type":"text","text":"Clicks: 1"},{"type":"button","label":"Add one"}]})");
    Patch patch = Reconcile(&first, second);
    CHECK(patch.children[0].op == PatchOp::Update);
    CHECK(patch.children[0].changed == std::vector<std::string>{"text"});
    CHECK(patch.children[1].op == PatchOp::Keep);
    CHECK(!patch.reordered);
    mounted = renderer.Apply(patch, mounted);
    CHECK(mounted->children[0]->serial == textSerial);
    CHECK(mounted->children[0]->label == "Clicks: 1");
    CHECK(mounted->children[1]->serial == buttonSerial);
    CHECK(CountPatch(patch).updated == 1);
}

TEST_CASE(UiRender_ChangedTypeIsRebuilt) {
    FakeRenderer renderer;
    RenderNode first = Tree(R"({"type":"stack","children":[{"type":"text","text":"a"}]})");
    std::shared_ptr<Widget> mounted = renderer.Build(first);
    int oldSerial = mounted->children[0]->serial;
    RenderNode second = Tree(R"({"type":"stack","children":[{"type":"button","label":"a"}]})");
    Patch patch = Reconcile(&first, second);
    CHECK(patch.children[0].op == PatchOp::Replace);
    CHECK(patch.reordered);
    mounted = renderer.Apply(patch, mounted);
    CHECK(mounted->children[0]->serial != oldSerial);
    CHECK(mounted->children[0]->type == "button");
    CHECK(CountPatch(patch).replaced == 1);
}

TEST_CASE(UiRender_RemovedAttributeCountsAsChanged) {
    RenderNode first = Tree(R"({"type":"button","label":"a","disabled":true})");
    RenderNode second = Tree(R"({"type":"button","label":"a"})");
    Patch patch = Reconcile(&first, second);
    CHECK(patch.op == PatchOp::Update);
    CHECK(patch.changed == std::vector<std::string>{"disabled"});
}

TEST_CASE(UiRender_KeyedRowsSurviveAnInsertion) {
    FakeRenderer renderer;
    RenderNode first = Tree(R"({"type":"list","children":[{"type":"text","key":"a","text":"A"},{"type":"text","key":"b","text":"B"}]})");
    std::shared_ptr<Widget> mounted = renderer.Build(first);
    int a = mounted->children[0]->serial;
    int b = mounted->children[1]->serial;
    RenderNode second = Tree(R"({"type":"list","children":[{"type":"text","key":"z","text":"Z"},)"
                             R"({"type":"text","key":"a","text":"A"},{"type":"text","key":"b","text":"B"}]})");
    Patch patch = Reconcile(&first, second);
    CHECK(patch.reordered);
    CHECK(patch.children[0].op == PatchOp::Create);
    CHECK(patch.children[1].from == 0);
    CHECK(patch.children[2].from == 1);
    mounted = renderer.Apply(patch, mounted);
    CHECK(mounted->children.size() == 3);
    CHECK(mounted->children[1]->serial == a);
    CHECK(mounted->children[2]->serial == b);
    CHECK(CountPatch(patch).created == 1);
}

TEST_CASE(UiRender_RemovedChildIsReported) {
    RenderNode first = Tree(R"({"type":"stack","children":[{"type":"text","text":"a"},{"type":"text","text":"b"}]})");
    RenderNode second = Tree(R"({"type":"stack","children":[{"type":"text","text":"a"}]})");
    Patch patch = Reconcile(&first, second);
    CHECK(patch.removed == std::vector<int>{1});
    CHECK(patch.reordered);
    CHECK(CountPatch(patch).removed == 1);
}

TEST_CASE(UiRender_SurfacesReportAddedChangedAndRemoved) {
    ampify::UiModel model;
    model.SetPanel("react-panel", "react-panel", Parse(R"({"type":"text","text":"a"})"));
    SurfaceTracker tracker;
    SurfaceChanges first = tracker.Poll(model);
    CHECK(first.added.size() == 1);
    CHECK(first.added[0].kind == SurfaceKind::Panel);
    CHECK(first.added[0].nodeId == "panel-react-panel");
    CHECK(first.added[0].key == "panel|react-panel|react-panel");

    CHECK(tracker.Poll(model).Empty());

    model.SetPanel("react-panel", "react-panel", Parse(R"({"type":"text","text":"b"})"));
    SurfaceChanges second = tracker.Poll(model);
    CHECK(second.changed.size() == 1);
    CHECK(second.changed[0].revision > first.added[0].revision);

    model.SetTitle("react-panel", "My panel");
    CHECK(tracker.Poll(model).changed.size() == 1);

    model.RemoveElement("react-panel");
    SurfaceChanges third = tracker.Poll(model);
    CHECK(third.removed.size() == 1);
    CHECK(third.removed[0].key == "panel|react-panel|react-panel");
}

TEST_CASE(UiRender_SurfacesCoverPopupsPagesControlsAndRoute) {
    ampify::UiModel model;
    model.OpenPopup("x", Parse(R"({"type":"text","text":"1"})"));
    model.OpenPopup("x", Parse(R"({"type":"text","text":"2"})"));
    model.SetPopup("x", "main", Parse(R"({"type":"text","text":"3"})"));
    model.SetPage("app:stats", "/stats", Parse(R"({"type":"text","text":"p"})"));
    int control = model.AddControl("x", "playbar", "Paroles", "<svg/>");
    model.Navigate("app:stats", "/stats");

    SurfaceTracker tracker;
    SurfaceChanges changes = tracker.Poll(model);
    CHECK(changes.added.size() == 5);
    std::map<std::string, Surface> byKey;
    for (const Surface& surface : changes.added) byKey[surface.key] = surface;
    CHECK(byKey.count("popup|x|#1") == 1);
    CHECK(byKey["popup|x|#2"].nodeId == "popup-open-2");
    CHECK(byKey["popup|x|main"].nodeId == "popup-main");
    CHECK(byKey["page|app:stats|/stats"].nodeId == "page-stats");
    CHECK(byKey["control|x|" + std::to_string(control)].control.label == "Paroles");
    CHECK(changes.route && changes.route->second == "/stats");

    // The route is reported once, until another navigate.
    CHECK(!tracker.Poll(model).route);
    model.ClosePopup("x", "main");
    CHECK(tracker.Poll(model).removed.size() == 1);
}

TEST_CASE(UiRender_SettingsAnchorIsTheStackWithTheMostToggles) {
    FakeTree tree;
    int root = tree.Add(-1, "Microsoft.UI.Xaml.Controls.Grid", kProbePanel | kProbeGrid);
    int frame = tree.Add(root, "Microsoft.UI.Xaml.Controls.Frame", kProbeFrame);
    int page = tree.Add(frame, "AppleMusicWin.Views.PlaybackSettingsPage", kProbePage | kProbeUserControl);
    int outer = tree.Add(page, "Microsoft.UI.Xaml.Controls.StackPanel", kStack);
    int header = tree.Add(outer, "Microsoft.UI.Xaml.Controls.StackPanel", kStack);
    tree.Add(header, "Microsoft.UI.Xaml.Controls.TextBlock", kProbeTextBlock);
    int row1 = tree.Add(outer, "Microsoft.UI.Xaml.Controls.Border", kProbeBorder);
    tree.Add(row1, "Microsoft.UI.Xaml.Controls.ToggleSwitch", kProbeToggleSwitch);
    int row2 = tree.Add(outer, "Microsoft.UI.Xaml.Controls.Border", kProbeBorder);
    int inner = tree.Add(row2, "Microsoft.UI.Xaml.Controls.StackPanel", kStack);
    tree.Add(inner, "Microsoft.UI.Xaml.Controls.ToggleSwitch", kProbeToggleSwitch);
    int foreign = tree.Add(outer, "Microsoft.UI.Xaml.Controls.Border", kProbeBorder, "AMExclusiveModeCard");
    tree.Add(foreign, "Microsoft.UI.Xaml.Controls.ToggleSwitch", kProbeToggleSwitch, "AMExclusiveModeToggle");
    int ours = tree.Add(outer, "Microsoft.UI.Xaml.Controls.Border", kProbeBorder, "AMPIFY_ModsCard");
    int ourStack = tree.Add(ours, "Microsoft.UI.Xaml.Controls.StackPanel", kStack);
    for (int i = 0; i < 5; i++) tree.Add(ourStack, "Microsoft.UI.Xaml.Controls.ToggleSwitch", kProbeToggleSwitch);

    std::optional<int> found = FindByClass(tree.nodes, "PlaybackSettingsPage");
    CHECK(found && *found == page);
    std::optional<int> anchor = ChooseSettingsAnchor(tree.nodes, page);
    CHECK(anchor && *anchor == outer);  // not the kit's own stack, even with five toggles
    std::optional<int> row = ChooseTemplateRow(tree.nodes, outer);
    CHECK(row && *row == row2);  // the last native row, not AMExclusive's nor ours
    std::optional<int> toggle = LastNativeOf(tree.nodes, outer, kProbeToggleSwitch);
    CHECK(toggle && tree.nodes[*toggle].parent == inner);
    CHECK(InsideModElement(tree.nodes, foreign + 1));
    CHECK(!InsideModElement(tree.nodes, inner));
}

TEST_CASE(UiRender_PageWithoutTogglesHasNoAnchor) {
    FakeTree tree;
    int page = tree.Add(-1, "AppleMusicWin.Views.PlaybackSettingsPage", kProbePage);
    tree.Add(page, "Microsoft.UI.Xaml.Controls.StackPanel", kStack);
    CHECK(!ChooseSettingsAnchor(tree.nodes, page));
    CHECK(!FindByClass(tree.nodes, "GeneralSettingsPage"));
}

TEST_CASE(UiRender_DiscoveryDoesNotWalkIntoListsButtonsOrKitElements) {
    ProbeNode list;
    list.flags = kProbeItemsControl;
    ProbeNode button;
    button.flags = kProbeButtonBase;
    ProbeNode repeater;
    repeater.flags = kProbeItemsRepeater;
    ProbeNode ours;
    ours.name = "AMPIFY_PanelHost";
    ProbeNode grid;
    grid.flags = kProbePanel;
    ProbeNode foreign;
    foreign.name = "AMExclusiveModeCard";
    CHECK(!ShouldDescend(list));
    CHECK(!ShouldDescend(button));
    CHECK(!ShouldDescend(repeater));
    CHECK(!ShouldDescend(ours));
    CHECK(ShouldDescend(grid));
    CHECK(ShouldDescend(foreign));  // walked (read only), never changed
}

TEST_CASE(UiRender_PlaybarButtonGoesAfterTheTransportButtons) {
    FakeTree tree;
    int root = tree.Add(-1, "Grid", kProbePanel | kProbeGrid);
    int bar = tree.Add(root, "StackPanel", kStack | kProbeHorizontal);
    tree.Add(bar, "Button", kProbeButtonBase, "TransportControl_SkipBack");
    tree.Add(bar, "Button", kProbeButtonBase, "TransportControl_PlayPauseStop");
    int wrapper = tree.Add(bar, "ContentPresenter", 0);
    int skip = tree.Add(wrapper, "Button", kProbeButtonBase, "TransportControl_SkipForward");
    std::optional<Insertion> insertion = ChoosePlaybarInsertion(tree.nodes);
    CHECK(insertion && insertion->parent == bar);
    CHECK(insertion && insertion->after == wrapper);
    CHECK(insertion && insertion->styleFrom == skip);
}

TEST_CASE(UiRender_TopbarButtonGoesAfterAppleMusicsToggleButtons) {
    FakeTree tree;
    int root = tree.Add(-1, "Grid", kProbePanel | kProbeGrid, "TransportBar");
    tree.Add(root, "AutoSuggestBox", kProbeAutoSuggest);
    int toggles = tree.Add(root, "StackPanel", kStack | kProbeHorizontal, "NowPlayingToggleButtons");
    int volume = tree.Add(toggles, "ToggleButton", kProbeButtonBase, "VolumeControl");
    std::optional<Insertion> insertion = ChooseTopbarInsertion(tree.nodes);
    CHECK(insertion && insertion->parent == toggles);
    CHECK(insertion && insertion->after == -1);
    CHECK(insertion && insertion->styleFrom == volume);
}

TEST_CASE(UiRender_TopbarButtonGoesBesideTheSearchBox) {
    FakeTree tree;
    int root = tree.Add(-1, "Grid", kProbePanel | kProbeGrid);
    int nav = tree.Add(root, "NavigationView", kProbeNavigationView);
    int bar = tree.Add(nav, "StackPanel", kStack | kProbeHorizontal);
    tree.Add(bar, "AutoSuggestBox", kProbeAutoSuggest);
    int button = tree.Add(bar, "Button", kProbeButtonBase);
    std::optional<Insertion> insertion = ChooseTopbarInsertion(tree.nodes);
    CHECK(insertion && insertion->parent == bar);
    CHECK(insertion && insertion->styleFrom == button);

    FakeTree empty;
    empty.Add(-1, "Grid", kProbePanel);
    CHECK(!ChooseTopbarInsertion(empty.nodes));
    CHECK(!ChoosePlaybarInsertion(empty.nodes));
}

TEST_CASE(UiRender_BoundedWalkStopsAtTheLimit) {
    // A wide fake tree: 1 root with 2000 children. The walk sees at most 512 nodes.
    struct Node {
        int id;
        std::vector<Node*> children;
    };
    std::vector<std::unique_ptr<Node>> storage;
    storage.push_back(std::make_unique<Node>(Node{0, {}}));
    for (int i = 1; i <= 2000; i++) {
        storage.push_back(std::make_unique<Node>(Node{i, {}}));
        storage[0]->children.push_back(storage.back().get());
    }
    std::size_t visited = WalkBounded(
        storage[0].get(), [](Node* node) { return static_cast<std::uintptr_t>(node->id) + 1; },
        [](Node* node, auto push) {
            for (Node* child : node->children) {
                if (!push(child)) break;
            }
        });
    CHECK(visited == 512);

    std::size_t small = WalkBounded(
        storage[1].get(), [](Node* node) { return static_cast<std::uintptr_t>(node->id) + 1; },
        [](Node*, auto) {});
    CHECK(small == 1);
}

TEST_CASE(UiRender_RefreshTicketQueuesOnceAndCloses) {
    RefreshTicket ticket;
    std::uint64_t first = ticket.Request();
    CHECK(first != 0);
    CHECK(ticket.Request() == 0);  // already queued
    CHECK(!ticket.Take(first + 1));
    CHECK(ticket.Take(first));
    CHECK(!ticket.Take(first));  // a ticket is taken once
    std::uint64_t second = ticket.Request();
    CHECK(second > first);

    ticket.ArmLayout(1000);
    CHECK(ticket.OnLayout(1100));
    CHECK(ticket.OnLayout(1200));
    CHECK(ticket.OnLayout(1300));
    CHECK(ticket.OnLayout(1400));
    CHECK(!ticket.OnLayout(1500));  // four wake-ups at most
    ticket.ArmLayout(2000);
    CHECK(!ticket.OnLayout(2800));  // past 750 ms

    ticket.Close();
    CHECK(ticket.Closed());
    CHECK(!ticket.Take(second));
    CHECK(ticket.Request() == 0);
}

TEST_CASE(UiRender_ModsListsValidExtensionsWithTheirState) {
    fs::path kit = FreshKit("mods-list");
    AddExtension(kit, "react-panel", "React panel");
    AddExtension(kit, "hello-extension", "Hello");
    WriteFile(kit / "extensions" / "broken" / "manifest.json", "{");
    WriteFile(kit / "extensions" / ".tmp-install" / "manifest.json", "{}");
    WriteFile(kit / "settings.txt", "extensions.react-panel.enabled = true\n");

    std::vector<ModEntry> mods = ListMods(kit);
    CHECK(mods.size() == 2);
    if (mods.size() == 2) {
        CHECK(mods[0].id == "hello-extension");  // sorted by name: Hello, React panel
        CHECK(!mods[0].enabled);
        CHECK(mods[1].id == "react-panel");
        CHECK(mods[1].enabled);
        CHECK(mods[1].description == "Makes React panel");
    }
    CHECK(ListMods(FreshKit("mods-empty")).empty());
}

TEST_CASE(UiRender_ModsSwitchWritesTheSettingAndAReloadRequest) {
    fs::path kit = FreshKit("mods-switch");
    WriteFile(kit / "settings.txt", "theme.active = midnight\n");
    WriteFile(kit / "reload.txt", "1700000000\n");
    std::string error;
    CHECK(SwitchMod(kit, "react-panel", true, error));
    CHECK(SwitchMod(kit, "react-panel", false, error));
    CHECK(error.empty());
    std::string settings = ReadFile(kit / "settings.txt");
    CHECK(settings.find("extensions.react-panel.enabled") != std::string::npos);
    CHECK(settings.find("theme.active = midnight") != std::string::npos);
    CHECK(ReadFile(kit / "reload.txt") == "1700000000\nstart react-panel\nstop react-panel\n");

    CHECK(!SwitchMod(kit, "app:stats", true, error));
    CHECK(!error.empty());
    CHECK(!SwitchMod(kit, "../x", true, error));
}

TEST_CASE(UiRender_SvgIconKeepsViewBoxAndSafePaths) {
    SvgIcon icon = ParseSvgIcon(R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24"><path d="M8 5v14l11-7z"/></svg>)");
    CHECK(icon.Usable());
    CHECK(icon.width == 24 && icon.height == 24);
    CHECK(icon.paths == std::vector<std::string>{"M8 5v14l11-7z"});

    SvgIcon two = ParseSvgIcon(R"(<svg viewBox='0,0,48,32'><path id="a" d='M0 0h4'/><path d="M1 1"/></svg>)");
    CHECK(two.width == 48 && two.height == 32);
    CHECK(two.paths.size() == 2);

    // Markup or quotes in the data are dropped, so nothing can reach the XAML text.
    SvgIcon hostile = ParseSvgIcon(R"(<svg><path d="M0 0' Fill='Red"/><path d="M0 0&lt;"/></svg>)");
    CHECK(!hostile.Usable());
    CHECK(hostile.width == 24);
    CHECK(!ParseSvgIcon("not an icon").Usable());

    CHECK(IsGlyphIcon("\xEE\x9D\xA8"));  // U+E768, Play in Segoe Fluent Icons
    CHECK(!IsGlyphIcon("<svg/>"));
    CHECK(!IsGlyphIcon(""));
}
