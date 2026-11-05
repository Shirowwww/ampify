// The portable half of the native renderer: render trees, reconciliation, node names, surfaces, anchors in a fake
// XAML tree, the bounded walk, the refresh ticket and the Mods card files. The Windows half (src/win/ui_renderer.cpp,
// src/win/ui_service.cpp) follows these decisions; it is only compiled by the MSVC job with the WinUI projection.

#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "../src/core/address_order.hpp"
#include "../src/core/settings.hpp"
#include "../src/core/theme/theme_settings.hpp"
#include "../src/core/ui_model.hpp"
#include "../src/core/ui_render/apple_tweaks.hpp"
#include "../src/core/ui_render/mods.hpp"
#include "../src/core/ui_render/probe.hpp"
#include "../src/core/ui_render/render_tree.hpp"
#include "../src/core/ui_render/style.hpp"
#include "../src/core/ui_render/surfaces.hpp"
#include "../src/core/ui_render/svg_icon.hpp"
#include "test.hpp"

namespace {

namespace fs = std::filesystem;
using namespace ampify::uirender;
using ampify::Settings;

ampify::Json Parse(const std::string& text) {
    std::optional<ampify::Json> value = ampify::Json::Parse(text);
    return value ? *value : ampify::Json();
}

RenderNode Tree(const std::string& text) {
    return BuildRenderTree(Parse(text), "panel-test");
}

// A fake native widget tree that applies patches the way src/win/ui_renderer.cpp does: Keep and Update reuse the widget,
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

TEST_CASE(UiRender_AnApplePlayerRequestIsReportedOnce) {
    ampify::UiModel model;
    SurfaceTracker tracker;
    CHECK(!tracker.Poll(model).applePlayer);

    model.RequestApplePlayer();
    SurfaceChanges changes = tracker.Poll(model);
    CHECK(changes.applePlayer);
    CHECK(!changes.Empty());
    CHECK(!tracker.Poll(model).applePlayer);
}

TEST_CASE(UiRender_SwitchingAButtonOnOrOffIsAChangeOfItsSurface) {
    ampify::UiModel model;
    int control = model.AddControl("x", "playbar", "Probe", "\xEE\x9C\xB4");
    SurfaceTracker tracker;
    CHECK(tracker.Poll(model).added.size() == 1);
    CHECK(tracker.Poll(model).Empty());

    CHECK(model.SetControlActive("x", control, true));
    SurfaceChanges on = tracker.Poll(model);
    CHECK(on.changed.size() == 1);
    CHECK(on.changed.size() == 1 && on.changed[0].control.active);

    CHECK(model.SetControlActive("x", control, false));
    SurfaceChanges off = tracker.Poll(model);
    CHECK(off.changed.size() == 1);
    CHECK(off.changed.size() == 1 && !off.changed[0].control.active);
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

TEST_CASE(UiRender_AMenuIsAboutTheItemItsPlayEntryQuotes) {
    // Apple Music's French menus put a narrow no-break space inside the guillemets.
    CHECK(QuotedMenuName({"Ajouter \xC3\xA0 la playlist", "Lire \xC2\xAB\xE2\x80\xAFMerci\xE2\x80\xAF\xC2\xBB"}) == "Merci");
    CHECK(QuotedMenuName({"Lire \xC2\xAB\xC2\xA0Merci\xC2\xA0\xC2\xBB"}) == "Merci");
    CHECK(QuotedMenuName({"Play \xE2\x80\x9CMerci\xE2\x80\x9D"}) == "Merci");
    CHECK(QuotedMenuName({"\xE2\x80\x9EMerci\xE2\x80\x9C wiedergeben"}) == "Merci");
    CHECK(QuotedMenuName({"Play \"Merci\""}) == "Merci");
    // A name that holds quotes itself keeps them: from the first opening quote to the last closing one.
    CHECK(QuotedMenuName({"Lire \xC2\xAB Le \xC2\xAB grand \xC2\xBB soir \xC2\xBB"}) == "Le \xC2\xAB grand \xC2\xBB soir");
    // A menu of options quotes nothing; empty quotes do not name an item.
    CHECK(QuotedMenuName({"Trier par titre", "Trier par artiste"}).empty());
    CHECK(QuotedMenuName({"Lire \xC2\xAB  \xC2\xBB", "Partager \xE2\x80\x9CSurvival\xE2\x80\x9D"}) == "Survival");
    CHECK(QuotedMenuName({"Say \""}).empty());
    CHECK(QuotedMenuName({}).empty());
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

TEST_CASE(UiRender_PlaybarButtonJoinsTheToggleButtonsOn1792) {
    FakeTree tree;
    int root = tree.Add(-1, "Grid", kProbePanel | kProbeGrid, "TransportBar");
    int bar = tree.Add(root, "StackPanel", kStack | kProbeHorizontal);
    tree.Add(bar, "Button", kProbeButtonBase, "TransportControl_SkipForward");
    int toggles = tree.Add(root, "StackPanel", kStack | kProbeHorizontal, "NowPlayingToggleButtons");
    int lyrics = tree.Add(toggles, "ToggleButton", kProbeButtonBase, "LyricsToggleButton");
    std::optional<Insertion> insertion = ChoosePlaybarInsertion(tree.nodes);
    CHECK(insertion && insertion->parent == toggles);
    CHECK(insertion && insertion->after == -1);
    CHECK(insertion && insertion->styleFrom == lyrics);
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

TEST_CASE(UiRender_ModsShowTheTranslationOfTheLanguageElseTheEnglishText) {
    fs::path kit = FreshKit("mods-translated");
    WriteFile(kit / "extensions" / "clock" / "manifest.json",
              R"({"id":"clock","name":"Clock","version":"1.0.0","kind":"extension","author":"x","description":"Time.",)"
              R"("kitApi":"1.0","main":"main.js","translations":{"fr":{"name":"Horloge","description":"L'heure."}}})");
    WriteFile(kit / "extensions" / "clock" / "main.js", "");
    WriteFile(kit / "extensions" / "plain" / "manifest.json",
              R"({"id":"plain","name":"Plain","version":"1.0.0","kind":"extension","author":"x","description":"Text.",)"
              R"("kitApi":"1.0","main":"main.js"})");
    WriteFile(kit / "extensions" / "plain" / "main.js", "");

    std::vector<ModEntry> mods = ListMods(kit);
    CHECK(mods.size() == 2);
    if (mods.size() != 2) return;
    const ModEntry& clock = mods[0].id == "clock" ? mods[0] : mods[1];
    const ModEntry& plain = mods[0].id == "clock" ? mods[1] : mods[0];
    CHECK(clock.NameIn("fr") == "Horloge");
    CHECK(clock.DescriptionIn("fr") == "L'heure.");
    CHECK(clock.NameIn("en") == "Clock");
    CHECK(clock.DescriptionIn("en") == "Time.");
    CHECK(plain.NameIn("fr") == "Plain");
    CHECK(plain.DescriptionIn("fr") == "Text.");
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

TEST_CASE(UiRender_TheBuiltInLevelerIsReadAndSwitchedLikeAnExtension) {
    fs::path kit = FreshKit("mods-leveler");
    WriteFile(kit / "settings.txt", "theme.active = midnight\n");
    WriteFile(kit / "reload.txt", "");
    CHECK(!IsModOn(kit, "atmos-leveler"));  // off until asked for: it changes the sound
    std::string error;
    CHECK(SwitchMod(kit, "atmos-leveler", true, error));
    CHECK(IsModOn(kit, "atmos-leveler"));
    CHECK(ReadFile(kit / "reload.txt") == "start atmos-leveler\n");
    CHECK(SwitchMod(kit, "atmos-leveler", false, error));
    CHECK(!IsModOn(kit, "atmos-leveler"));
    CHECK(error.empty());
}

TEST_CASE(UiRender_SwitchingAnElementOnTurnsOnTheServicesItAsksFor) {
    fs::path kit = FreshKit("mods-services");
    WriteFile(kit / "extensions" / "presence" / "manifest.json",
              R"({"id":"presence","name":"Presence","version":"1.0.0","kind":"extension","author":"x",)"
              R"("description":"Discord.","kitApi":"1.0","main":"main.js","permissions":["ipc.discord","ui.tray"]})");
    WriteFile(kit / "extensions" / "presence" / "main.js", "");
    WriteFile(kit / "settings.txt", "tray.enabled = 0\n");
    std::string error;
    CHECK(SwitchMod(kit, "presence", true, error));
    Settings settings;
    settings.Load((kit / "settings.txt").string());
    CHECK(settings.GetBool("discord.enabled", false));
    CHECK(settings.GetBool("tray.enabled", false));
    CHECK(!settings.GetBool("midi.enabled", false));  // not asked for

    // Switching it off leaves the services as they are: another element may use them.
    CHECK(SwitchMod(kit, "presence", false, error));
    settings.Load((kit / "settings.txt").string());
    CHECK(settings.GetBool("discord.enabled", false));
}

TEST_CASE(UiRender_ModsCarryTheirDeclaredSettingsAndValues) {
    fs::path kit = FreshKit("mods-settings");
    WriteFile(kit / "extensions" / "clock" / "manifest.json",
              R"({"id":"clock","name":"Clock","version":"1.0.0","kind":"extension","author":"x","description":"Time.",)"
              R"("kitApi":"1.0","main":"main.js","settings":[)"
              R"({"key":"seconds","label":"Seconds","type":"toggle","default":true},)"
              R"({"key":"side","label":"Side","type":"choice","options":["Left","Right"],"default":"Left"}]})");
    WriteFile(kit / "extensions" / "clock" / "main.js", "");
    WriteFile(kit / "settings.txt", "extensions.clock.side = Right\n");

    std::vector<ModEntry> mods = ListMods(kit);
    CHECK(mods.size() == 1);
    if (mods.size() != 1) return;
    CHECK(mods[0].settings.size() == 2);
    CHECK(mods[0].values == std::vector<std::string>({"true", "Right"}));  // the default, then the stored value
}

TEST_CASE(UiRender_ModsSettingIsWrittenAndRestartsTheExtension) {
    fs::path kit = FreshKit("mods-setting");
    WriteFile(kit / "settings.txt", "extensions.clock.enabled = true\n");
    WriteFile(kit / "reload.txt", "");
    std::string error;
    CHECK(SetModSetting(kit, "clock", "side", "Right", error));
    CHECK(SetModSetting(kit, "clock", "greeting", "two\nlines", error));
    CHECK(error.empty());
    std::string settings = ReadFile(kit / "settings.txt");
    CHECK(settings.find("extensions.clock.side = Right") != std::string::npos);
    CHECK(settings.find("extensions.clock.greeting = two lines") != std::string::npos);
    CHECK(settings.find("extensions.clock.enabled = true") != std::string::npos);
    CHECK(ReadFile(kit / "reload.txt") == "restart clock\nrestart clock\n");

    CHECK(!SetModSetting(kit, "clock", "enabled", "false", error));
    CHECK(!SetModSetting(kit, "clock", "a.b", "1", error));
    CHECK(!SetModSetting(kit, "app:clock", "side", "Left", error));
    CHECK(!SetModSetting(kit, "../x", "side", "Left", error));
    CHECK(ReadFile(kit / "settings.txt").find("extensions.clock.enabled = true") != std::string::npos);
}

TEST_CASE(UiRender_ModsSettingLeavesAnUnreadableSettingsFileAlone) {
    fs::path kit = FreshKit("mods-unreadable");
    fs::create_directories(kit / "settings.txt");  // exists, but cannot be read as a file
    std::string error;
    CHECK(!SetModSetting(kit, "clock", "side", "Right", error));
    CHECK(error.rfind("cannot read: ", 0) == 0);
    error.clear();
    CHECK(!SwitchMod(kit, "clock", true, error));
    CHECK(error.rfind("cannot read: ", 0) == 0);
    CHECK(!fs::exists(kit / "reload.txt"));
}

TEST_CASE(UiRender_TheActiveThemeCarriesItsSettings) {
    fs::path kit = FreshKit("theme-settings");
    WriteFile(kit / "themes" / "night" / "manifest.json",
              R"({"id":"night","name":"Night","version":"1.0.0","kind":"theme","author":"x","description":"Dark.",)"
              R"("kitApi":"1.0","colors":"colors.ini","settings":[)"
              R"({"key":"radius","label":"Corners","type":"number","min":0,"max":24,"default":8},)"
              R"({"key":"hide-radio","label":"Hide Radio","type":"toggle","default":false}]})");
    WriteFile(kit / "themes" / "night" / "colors.ini", "[Base]\nmain = 000000\n");
    WriteFile(kit / "themes" / "plain" / "colors.ini", "[Base]\nmain = 000000\n");
    CHECK(!ActiveThemeSettings(kit));  // no theme chosen

    WriteFile(kit / "settings.txt", "theme.active = night\nthemes.night.radius = 30\nthemes.night.hide-radio = 1\n");
    std::optional<ModEntry> theme = ActiveThemeSettings(kit);
    CHECK(theme && theme->id == "night" && theme->name == "Night" && theme->theme);
    if (theme) CHECK(theme->values == std::vector<std::string>({"8", "true"}));  // 30 is out of bounds

    WriteFile(kit / "settings.txt", "theme.active = plain\n");
    CHECK(!ActiveThemeSettings(kit));  // a theme without settings shows nothing
}

TEST_CASE(UiRender_TheActiveThemeCarriesTheTranslationOfItsSettings) {
    fs::path kit = FreshKit("theme-settings-fr");
    WriteFile(kit / "themes" / "night" / "manifest.json",
              R"({"id":"night","name":"Night","version":"1.0.0","kind":"theme","author":"x","description":"Dark.",)"
              R"("kitApi":"1.0","colors":"colors.ini","settings":[)"
              R"({"key":"play","label":"Play button","type":"choice","options":["White","Green"],"default":"White"}],)"
              R"("translations":{"fr":{"name":"Nuit","description":"Sombre.","settings":{"play":"Bouton Lecture"},)"
              R"("options":{"White":"Blanc","Green":"Vert"}}}})");
    WriteFile(kit / "themes" / "night" / "colors.ini", "[Base]\nmain = 000000\n");
    WriteFile(kit / "settings.txt", "theme.active = night\n");
    std::optional<ModEntry> theme = ActiveThemeSettings(kit);
    CHECK(theme && theme->NameIn("fr") == "Nuit");
    if (!theme || !theme->translations.count("fr")) return;
    const ampify::ManifestText& fr = theme->translations.at("fr");
    CHECK(fr.settings.at("play") == "Bouton Lecture");
    CHECK(fr.options.at("Green") == "Vert");
}

TEST_CASE(UiRender_AThemeSettingIsWrittenWithoutAReloadRequest) {
    fs::path kit = FreshKit("theme-setting");
    WriteFile(kit / "settings.txt", "theme.active = night\n");
    std::string error;
    CHECK(SetThemeSetting(kit, "night", "radius", "12", error));
    CHECK(error.empty());
    CHECK(ReadFile(kit / "settings.txt").find("themes.night.radius = 12") != std::string::npos);
    CHECK(ReadFile(kit / "settings.txt").find("theme.active = night") != std::string::npos);
    CHECK(!fs::exists(kit / "reload.txt"));
    CHECK(!SetThemeSetting(kit, "../x", "radius", "1", error));
    CHECK(!SetThemeSetting(kit, "night", "a.b", "1", error));
}

TEST_CASE(UiRender_ThemesAreListedByNameWithTheirTranslation) {
    fs::path kit = FreshKit("theme-list");
    WriteFile(kit / "themes" / "rose" / "manifest.json",
              R"({"id":"rose","name":"Rose gold","version":"1.0.0","kind":"theme","author":"x","description":"Warm.",)"
              R"("kitApi":"1.0","colors":"colors.ini","translations":{"fr":{"name":"Or rose","description":"Chaud."}}})");
    WriteFile(kit / "themes" / "rose" / "colors.ini", "[Base]\nmain = 000000\n");
    WriteFile(kit / "themes" / "aurora" / "manifest.json",
              R"({"id":"aurora","name":"Aurora","version":"1.0.0","kind":"theme","author":"x","description":"Cold.",)"
              R"("kitApi":"1.0","colors":"colors.ini"})");
    WriteFile(kit / "themes" / "aurora" / "colors.ini", "[Base]\nmain = 000000\n");
    WriteFile(kit / "themes" / "snippets" / "readme.txt", "not a theme");
    WriteFile(kit / "themes" / "broken" / "manifest.json", "{");

    std::vector<ModEntry> themes = ListThemes(kit);
    CHECK(themes.size() == 2);
    if (themes.size() != 2) return;
    CHECK(themes[0].id == "aurora" && themes[1].id == "rose");
    CHECK(themes[1].NameIn("fr") == "Or rose");
    CHECK(themes[1].theme);
}

TEST_CASE(UiRender_ChoosingAThemeKeepsOnlyASchemeThatStillMeansSomething) {
    fs::path kit = FreshKit("theme-choose");
    WriteFile(kit / "settings.txt", "theme.active = night\ntheme.scheme = Dark\nextensions.clock.enabled = true\n");
    std::string error;
    CHECK(ChooseTheme(kit, "night", error));  // the same theme keeps its scheme
    CHECK(ReadFile(kit / "settings.txt").find("theme.scheme = Dark") != std::string::npos);
    CHECK(ChooseTheme(kit, "rose", error));  // another theme starts on its own default scheme
    std::string settings = ReadFile(kit / "settings.txt");
    CHECK(settings.find("theme.active = rose") != std::string::npos);
    CHECK(settings.find("theme.scheme") == std::string::npos);
    CHECK(settings.find("extensions.clock.enabled = true") != std::string::npos);

    WriteFile(kit / "settings.txt", "theme.active = rose\ntheme.scheme = auto\n");
    CHECK(ChooseTheme(kit, "", error));  // none; "auto" follows Windows whatever the theme
    settings = ReadFile(kit / "settings.txt");
    CHECK(settings.find("theme.active = none") != std::string::npos);
    CHECK(settings.find("theme.scheme = auto") != std::string::npos);
    CHECK(!ChooseTheme(kit, "../x", error));
}

TEST_CASE(UiRender_AccentCanFollowTheArtwork) {
    fs::path kit = FreshKit("accent-artwork");
    WriteFile(kit / "settings.txt", "theme.active = night\n");
    std::string error;
    CHECK(ChooseAccent(kit, "artwork", error));
    CHECK(ReadFile(kit / "settings.txt").find("theme.accent = artwork") != std::string::npos);
    CHECK(ChosenAccent(kit) == "artwork");
    CHECK(ChooseAccent(kit, "0A84FF", error));
    CHECK(ChosenAccent(kit) == "0A84FF");
    CHECK(!ChooseAccent(kit, "artwrk", error));  // refused: the value stays
    CHECK(ChosenAccent(kit) == "0A84FF");
}

// The engine and the settings window each keep the word; a test is the only thing that keeps them equal.
TEST_CASE(UiRender_ArtworkAccentWordMatchesTheEngine) {
    CHECK(std::string_view(ampify::uirender::kArtworkAccent) == ampify::theme::kArtworkAccent);
}

TEST_CASE(UiRender_AChosenAccentIsWrittenAsSixHexDigitsOrRemoved) {
    fs::path kit = FreshKit("accent-choose");
    WriteFile(kit / "settings.txt", "theme.active = night\n");
    CHECK(ChosenAccent(kit).empty());  // the theme's own

    std::string error;
    CHECK(ChooseAccent(kit, "#1ed760", error));
    CHECK(ReadFile(kit / "settings.txt").find("theme.accent = 1ED760") != std::string::npos);
    CHECK(ChosenAccent(kit) == "1ED760");
    CHECK(ChooseAccent(kit, "0A84FF", error));
    CHECK(ChosenAccent(kit) == "0A84FF");

    CHECK(!ChooseAccent(kit, "blue", error));  // refused: the value stays
    CHECK(!ChooseAccent(kit, "#12345", error));
    CHECK(!ChooseAccent(kit, "1234567", error));
    CHECK(ChosenAccent(kit) == "0A84FF");

    CHECK(ChooseAccent(kit, "", error));  // back to the theme's
    CHECK(ReadFile(kit / "settings.txt").find("theme.accent") == std::string::npos);
    CHECK(ReadFile(kit / "settings.txt").find("theme.active = night") != std::string::npos);
    CHECK(!AccentPresets().empty() && AccentPresets().front().value == std::string("FA2D48"));
}

TEST_CASE(UiRender_AppleTweaksReadTheirValuesAndFallBackToApplesBehaviour) {
    Settings settings;
    AppleTweaks none = ReadAppleTweaks(settings);
    CHECK(none.notifications == NotificationRule::Apple);
    CHECK(none.player == PlayerMode::Window);  // what the Now Playing button did before the setting existed
    CHECK(!none.closeToTray);                   // closing quits, as Apple Music does
    CHECK(none.playerControls);                 // shuffle and repeat in the full-screen player
    CHECK(none.sharpArtwork);                   // a sharper cover where Apple's is stretched
    CHECK(none.textMarquee);                    // cut texts scroll under the pointer
    CHECK(!none.sidebarOnHover);                // the sidebar as Apple Music shows it

    settings.Set("apple.notifications", "Background");
    settings.Set("apple.fullscreen", "replace");
    settings.Set("apple.close", "tray");
    settings.Set("apple.playerControls", "hidden");
    settings.Set("apple.artwork", "Apple");
    settings.Set("apple.marquee", "off");
    settings.Set("apple.sidebar", "Hover");
    AppleTweaks set = ReadAppleTweaks(settings);
    CHECK(!set.playerControls);
    CHECK(!set.sharpArtwork);
    CHECK(!set.textMarquee);
    CHECK(set.sidebarOnHover);
    CHECK(set.notifications == NotificationRule::Background);
    CHECK(set.player == PlayerMode::Replace);
    CHECK(set.closeToTray);

    settings.Set("apple.notifications", "sometimes");
    settings.Set("apple.fullscreen", "work-area");
    settings.Set("apple.close", "away");
    AppleTweaks odd = ReadAppleTweaks(settings);
    CHECK(odd.notifications == NotificationRule::Apple);
    CHECK(odd.player == PlayerMode::WorkArea);
    CHECK(!odd.closeToTray);
}

TEST_CASE(UiRender_KitUpdateInstallsOnItsOwnUnlessToldOtherwise) {
    Settings settings;
    CHECK(ReadKitUpdate(settings) == KitUpdate::Auto);
    settings.Set("kit_update", "Ask");
    CHECK(ReadKitUpdate(settings) == KitUpdate::Ask);
    settings.Set("kit_update", "off");
    CHECK(ReadKitUpdate(settings) == KitUpdate::Off);
    settings.Set("kit_update", "weekly");
    CHECK(ReadKitUpdate(settings) == KitUpdate::Auto);

    fs::path kit = FreshKit("kit-update");
    WriteFile(kit / "settings.txt", "theme.active = night\n");
    std::string error;
    CHECK(SetAppleTweak(kit, "kit_update", "ask", error));
    CHECK(!SetAppleTweak(kit, "kit_update", "weekly", error));
    TweakChoice choice = KitUpdateChoice(kit);
    CHECK(choice.key == "kit_update" && choice.value == "ask");
    CHECK(choice.options.size() == 3 && choice.options.front().first == "auto");
    CHECK(ReadFile(kit / "settings.txt").find("theme.active = night") != std::string::npos);
}

TEST_CASE(UiRender_TheGlassLevelIsChosenFromThreeValues) {
    fs::path kit = FreshKit("glass-choice");
    WriteFile(kit / "settings.txt", "theme.active = night\ntheme.glass = Balanced\n");
    TweakChoice choice = GlassChoice(kit);
    CHECK(choice.key == "theme.glass" && choice.value == "balanced");
    CHECK(choice.options.size() == 3 && choice.options.front().first == "full");

    std::string error;
    CHECK(SetAppleTweak(kit, "theme.glass", "eco", error));
    CHECK(!SetAppleTweak(kit, "theme.glass", "ultra", error));
    std::string text = ReadFile(kit / "settings.txt");
    CHECK(text.find("theme.glass = eco") != std::string::npos);
    CHECK(text.find("theme.active = night") != std::string::npos);

    WriteFile(kit / "settings.txt", "theme.active = night\n");
    CHECK(GlassChoice(kit).value == "full");
}

TEST_CASE(UiRender_AmpifysEntryGoesInTheHeaderOnlyWhenChosen) {
    Settings settings;
    CHECK(!ReadEntryInHeader(settings));
    settings.Set("ampify.entry", "Header");
    CHECK(ReadEntryInHeader(settings));
    settings.Set("ampify.entry", "top");
    CHECK(!ReadEntryInHeader(settings));

    fs::path kit = FreshKit("entry-choice");
    WriteFile(kit / "settings.txt", "theme.active = night\n");
    CHECK(EntryChoice(kit).value == "sidebar");
    std::string error;
    CHECK(SetAppleTweak(kit, "ampify.entry", "header", error));
    CHECK(!SetAppleTweak(kit, "ampify.entry", "top", error));
    CHECK(EntryChoice(kit).value == "header");
}

TEST_CASE(UiRender_TheCardListsTheValuesOfEveryKeyItKnows) {
    for (const char* key : {"apple.notifications", "apple.fullscreen", "apple.playerControls", "apple.close",
                            "apple.network", "apple.artwork", "apple.marquee", "apple.sidebar", "theme.glass", "ampify.entry",
                            "kit_update", "apple.watchdog", "apple.orphanAgent"}) {
        CHECK(!AppleTweakValues(key).empty());
        CHECK(!IsAppleTweakValue(key, "not-a-value"));
    }
    CHECK(AppleTweakValues("apple.watchdog") == "off, on");
    CHECK(AppleTweakValues("apple.orphanAgent") == "off, on");
    CHECK(AppleTweakValues("theme.glass") == "full, balanced, eco");
    CHECK(AppleTweakValues("ampify.entry") == "sidebar, header");
    CHECK(AppleTweakValues("theme.active").empty());  // not a key of the card
}

TEST_CASE(UiRender_FreezeLogAndOrphanAgentAreOffUntilChosen) {
    Settings settings;
    CHECK(!ReadAppleTweaks(settings).freezeLog);
    CHECK(!ReadAppleTweaks(settings).orphanAgent);
    settings.Set("apple.watchdog", "on");
    settings.Set("apple.orphanAgent", "On");
    CHECK(ReadAppleTweaks(settings).freezeLog);
    CHECK(ReadAppleTweaks(settings).orphanAgent);
    settings.Set("apple.watchdog", "maybe");
    CHECK(!ReadAppleTweaks(settings).freezeLog);
}

TEST_CASE(UiRender_ResolvedAddressesListIpv4FirstOnlyWhenSomethingMoves) {
    using ampify::Ipv4FirstOrder;
    constexpr int v4 = ampify::kFamilyIpv4, v6 = ampify::kFamilyIpv6;
    CHECK((Ipv4FirstOrder({v6, v6, v4, v6, v4}) == std::vector<size_t>{2, 4, 0, 1, 3}));
    CHECK(Ipv4FirstOrder({v4, v6, v6}).empty());  // already first
    CHECK(Ipv4FirstOrder({v4, v4}).empty());
    CHECK(Ipv4FirstOrder({v6, v6}).empty());      // IPv6 only (NAT64): nothing to fall back to
    CHECK(Ipv4FirstOrder({}).empty());
    CHECK((Ipv4FirstOrder({v6, v4, v6}) == std::vector<size_t>{1, 0, 2}));
}

TEST_CASE(UiRender_TheNetworkFixIsOnUnlessSystemOrderIsChosen) {
    Settings settings;
    CHECK(ReadAppleTweaks(settings).ipv4First);
    settings.Set("apple.network", "System");
    CHECK(!ReadAppleTweaks(settings).ipv4First);
    settings.Set("apple.network", "whatever");
    CHECK(ReadAppleTweaks(settings).ipv4First);
}

TEST_CASE(UiRender_AppleTweaksAreWrittenOnlyWithAKnownValue) {
    fs::path kit = FreshKit("apple-tweaks");
    WriteFile(kit / "settings.txt", "theme.active = night\n");
    std::string error;
    CHECK(SetAppleTweak(kit, "apple.notifications", "never", error));
    CHECK(SetAppleTweak(kit, "apple.fullscreen", "screen", error));
    CHECK(SetAppleTweak(kit, "apple.close", "tray", error));
    std::string text = ReadFile(kit / "settings.txt");
    CHECK(text.find("apple.notifications = never") != std::string::npos);
    CHECK(text.find("apple.fullscreen = screen") != std::string::npos);
    CHECK(text.find("theme.active = night") != std::string::npos);

    CHECK(!SetAppleTweak(kit, "apple.notifications", "loud", error));
    CHECK(!SetAppleTweak(kit, "theme.active", "screen", error));

    // The player, its cover, the long texts and the sidebar first, then the close button; the notifications, where
    // "never" (Apple's own switch does it) only shows for someone who chose it before; the network fix, then the
    // stuck library agent and the freeze log, last.
    std::vector<TweakChoice> choices = AppleTweakChoices(kit);
    CHECK(choices.size() == 10);
    if (choices.size() == 10) {
        CHECK(choices[8].key == "apple.orphanAgent" && choices[8].value == "off");
        CHECK(choices[9].key == "apple.watchdog" && choices[9].value == "off");
        CHECK(choices[9].options.size() == 2);
        CHECK(choices[0].key == "apple.fullscreen" && choices[0].value == "screen");
        CHECK(choices[0].options.size() == 4);
        CHECK(choices[1].key == "apple.playerControls" && choices[1].value == "shown");
        CHECK(choices[2].key == "apple.artwork" && choices[2].value == "sharp");
        CHECK(choices[3].key == "apple.marquee" && choices[3].value == "hover");
        CHECK(choices[4].key == "apple.sidebar" && choices[4].value == "open");
        CHECK(choices[5].key == "apple.close" && choices[5].value == "tray");
        CHECK(choices[5].options.size() == 2);
        CHECK(choices[6].key == "apple.notifications" && choices[6].value == "never");
        CHECK(choices[6].options.size() == 3);
        CHECK(choices[7].key == "apple.network" && choices[7].value == "ipv4-first");
        CHECK(choices[7].options.size() == 2);
    }

    CHECK(SetAppleTweak(kit, "apple.notifications", "background", error));
    CHECK(SetAppleTweak(kit, "apple.network", "system", error));
    CHECK(SetAppleTweak(kit, "apple.artwork", "apple", error));
    CHECK(SetAppleTweak(kit, "apple.marquee", "off", error));
    CHECK(!SetAppleTweak(kit, "apple.marquee", "always", error));
    CHECK(SetAppleTweak(kit, "apple.sidebar", "hover", error));
    CHECK(!SetAppleTweak(kit, "apple.sidebar", "hidden", error));
    choices = AppleTweakChoices(kit);
    CHECK(choices.size() == 10 && choices[6].options.size() == 2 && choices[6].value == "background");
    CHECK(choices.size() == 10 && choices[7].value == "system");
    CHECK(choices.size() == 10 && choices[2].value == "apple" && choices[3].value == "off");
    CHECK(choices.size() == 10 && choices[4].value == "hover");
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

TEST_CASE(UiRender_ColorsAndBackgroundsKeepTheAlphaFirst) {
    std::optional<Rgba> solid = ParseColor("#1E90FF");
    CHECK(solid && solid->a == 255 && solid->r == 0x1E && solid->g == 0x90 && solid->b == 0xFF);
    std::optional<Rgba> faded = ParseColor("#80ffffff");
    CHECK(faded && faded->a == 0x80 && faded->r == 0xFF);
    CHECK(!ParseColor("1E90FF"));
    CHECK(!ParseColor("#1E90F"));
    CHECK(!ParseColor("#GG90FF"));

    std::optional<Background> acrylic = ParseBackground("acrylic(#CC101010, 0.6)");
    CHECK(acrylic && acrylic->acrylic && !acrylic->themed && acrylic->color.a == 0xCC && acrylic->tintOpacity == 0.6);
    std::optional<Background> themed = ParseBackground("acrylic");
    CHECK(themed && themed->acrylic && themed->themed);
    std::optional<Background> tintOnly = ParseBackground("acrylic(#101010)");
    CHECK(tintOnly && tintOnly->tintOpacity == 0.6);
    std::optional<Background> color = ParseBackground("#FF000000");
    CHECK(color && !color->acrylic);
    CHECK(!ParseBackground("acrylic(#101010, 1.5)"));
    CHECK(!ParseBackground("acrylic(red)"));
    CHECK(!ParseBackground("blur"));
}

TEST_CASE(UiRender_PaddingReadsTopRightBottomLeft) {
    std::optional<Edges> four = ParsePadding(ampify::Json(std::string("1,2,3,4")));
    CHECK(four && four->top == 1 && four->right == 2 && four->bottom == 3 && four->left == 4);
    std::optional<Edges> one = ParsePadding(ampify::Json(16.0));
    CHECK(one && one->top == 16 && one->left == 16);
    CHECK(!ParsePadding(ampify::Json(std::string("1,2"))));
    CHECK(!ParsePadding(ampify::Json(std::string("1,2,3,x"))));
    CHECK(!ParsePadding(ampify::Json(-1.0)));
    CHECK(!ParsePadding(ampify::Json(true)));
}

TEST_CASE(UiRender_TransitionsNameTheirPropertiesAndDurations) {
    std::optional<Transitions> fade = ParseTransition("opacity 400ms");
    CHECK(fade && fade->opacityMs == 400.0 && !fade->scaleMs && !fade->translationMs);
    std::optional<Transitions> all = ParseTransition("all 300ms");
    CHECK(all && all->opacityMs == 300.0 && all->scaleMs == 300.0 && all->translationMs == 300.0);
    std::optional<Transitions> two = ParseTransition(" opacity 400ms , scale 0.25s ");
    CHECK(two && two->opacityMs == 400.0 && two->scaleMs == 250.0);
    std::optional<Transitions> none = ParseTransition("none");
    CHECK(none && !none->opacityMs && !none->scaleMs);
    CHECK(!ParseTransition("opacity"));
    CHECK(!ParseTransition("color 300ms"));
    CHECK(!ParseTransition("opacity 300"));
    CHECK(!ParseTransition("opacity -3ms"));
    CHECK(!ParseTransition("opacity 11s"));
    CHECK(!ParseTransition(""));
}

TEST_CASE(UiRender_LayoutFollowsTheChildrenThatGrow) {
    CHECK(LayoutOf(Tree(R"({"type":"layer","children":[{"type":"image"},{"type":"text"}]})")) == Layout::Layer);
    CHECK(LayoutOf(Tree(R"({"type":"row","children":[{"type":"text"},{"type":"toggle"}]})")) == Layout::Pair);
    CHECK(LayoutOf(Tree(R"({"type":"row","children":[{"type":"text"}]})")) == Layout::Panel);
    CHECK(LayoutOf(Tree(R"({"type":"row","children":[{"type":"text","grow":true},{"type":"button"}]})")) == Layout::Grid);
    CHECK(LayoutOf(Tree(R"({"type":"stack","children":[{"type":"text"},{"type":"stack","grow":true}]})")) == Layout::Grid);
    CHECK(LayoutOf(Tree(R"({"type":"list","children":[{"type":"text","grow":true}]})")) == Layout::Panel);
    CHECK(LayoutOf(Tree(R"({"type":"card","children":[]})")) == Layout::Panel);
    CHECK(LayoutOf(Tree(R"({"type":"text"})")) == Layout::None);
}

TEST_CASE(UiRender_GridTabsAndMenusHaveTheirOwnLayouts) {
    CHECK(LayoutOf(Tree(R"({"type":"grid","columns":3,"children":[{"type":"text"}]})")) == Layout::Columns);
    CHECK(LayoutOf(Tree(R"({"type":"tabs","children":[{"type":"tab","label":"A"}]})")) == Layout::Panel);
    CHECK(LayoutOf(Tree(R"({"type":"menu","label":"More","children":[{"type":"item","label":"A"}]})")) == Layout::None);
}

TEST_CASE(UiRender_AGridHasAsManyColumnsAsAskedAndFillsRowByRow) {
    CHECK(GridColumns(Tree(R"({"type":"grid"})")) == 2);
    CHECK(GridColumns(Tree(R"({"type":"grid","columns":4})")) == 4);
    CHECK(GridColumns(Tree(R"({"type":"grid","columns":12})")) == 12);
    CHECK(GridColumns(Tree(R"({"type":"grid","columns":0})")) == 1);
    CHECK(GridColumns(Tree(R"({"type":"grid","columns":99})")) == 12);
    CHECK(GridColumns(Tree(R"({"type":"grid","columns":"x"})")) == 2);
    CHECK(GridCellOf(0, 4).row == 0 && GridCellOf(0, 4).column == 0);
    CHECK(GridCellOf(5, 4).row == 1 && GridCellOf(5, 4).column == 1);
    CHECK(GridCellOf(7, 1).row == 7 && GridCellOf(7, 1).column == 0);
    CHECK(GridCellOf(3, 0).column == 0);
}

TEST_CASE(UiRender_TabsAndMenusHaveTheirButtonVariant) {
    CHECK(ButtonVariantOf(Tree(R"({"type":"tab","selected":true})")) == "primary");
    CHECK(ButtonVariantOf(Tree(R"({"type":"tab"})")) == "tertiary");
    CHECK(ButtonVariantOf(Tree(R"({"type":"menu","label":"More"})")) == "secondary");
    CHECK(ButtonVariantOf(Tree(R"({"type":"button"})")) == "secondary");
    CHECK(ButtonVariantOf(Tree(R"({"type":"button","variant":"primary"})")) == "primary");
}

TEST_CASE(UiRender_EachFullscreenSessionIsANewSurface) {
    ampify::UiModel model;
    std::uint64_t session = model.SetFullscreen("lyrics", Parse(R"({"type":"text","text":"a"})"), 0);
    SurfaceTracker tracker;
    SurfaceChanges first = tracker.Poll(model);
    CHECK(first.added.size() == 1);
    CHECK(first.added[0].kind == SurfaceKind::Fullscreen);
    CHECK(first.added[0].id == std::to_string(session));
    CHECK(first.added[0].key == "fullscreen|lyrics|" + std::to_string(session));
    CHECK(first.added[0].nodeId == "fullscreen-main");

    model.SetFullscreen("lyrics", Parse(R"({"type":"text","text":"b"})"), session);
    CHECK(tracker.Poll(model).changed.size() == 1);

    // Closed then opened again: the old surface goes and a new one comes, with the same node ids.
    model.CloseFullscreen("lyrics", session);
    model.SetFullscreen("lyrics", Parse(R"({"type":"text","text":"c"})"), 0);
    SurfaceChanges again = tracker.Poll(model);
    CHECK(again.removed.size() == 1 && again.added.size() == 1);
    CHECK(again.added.size() == 1 && again.added[0].key != first.added[0].key);
    CHECK(again.added.size() == 1 && again.added[0].nodeId == "fullscreen-main");
}

TEST_CASE(UiRender_ElementSettingsListWhatAnInstalledElementDeclaresWithItsValues) {
    fs::path kit = FreshKit("element-settings");
    WriteFile(kit / "snippets" / "icons" / "manifest.json",
              R"({"id":"icons","name":"Icons","version":"1.0.0","kind":"snippet","author":"x","description":"d",)"
              R"("kitApi":"1.0","settings":[{"key":"shape","label":"Shape","type":"choice",)"
              R"("options":["Round","Square"],"default":"Round"}]})");
    WriteFile(kit / "snippets" / "icons" / "user.css", "Page { opacity: 1; }\n");
    WriteFile(kit / "settings.txt", "snippets.icons.shape = Square\n");

    std::optional<ModEntry> icons = ElementSettings(kit, "snippet", "icons");
    CHECK(icons && icons->snippet && icons->settings.size() == 1);
    CHECK(icons && icons->values.size() == 1 && icons->values[0] == "Square");
    CHECK(!ElementSettings(kit, "snippet", "missing"));
    CHECK(!ElementSettings(kit, "theme", "icons"));
}

TEST_CASE(UiRender_ChangingAnElementSettingChecksItAgainstTheManifest) {
    fs::path kit = FreshKit("element-setting-change");
    WriteFile(kit / "extensions" / "clock" / "manifest.json",
              R"({"id":"clock","name":"Clock","version":"1.0.0","kind":"extension","author":"x","description":"d",)"
              R"("kitApi":"1.0","main":"main.js","settings":[)"
              R"({"key":"size","label":"Size","type":"number","min":1,"max":10,"default":4},)"
              R"({"key":"seconds","label":"Seconds","type":"toggle","default":false}]})");
    WriteFile(kit / "extensions" / "clock" / "main.js", "");
    std::string error;

    CHECK(ChangeElementSetting(kit, "extension", "clock", "size", "7", error));
    CHECK(ReadFile(kit / "settings.txt").find("extensions.clock.size = 7") != std::string::npos);
    CHECK(ReadFile(kit / "reload.txt").find("restart clock") != std::string::npos);
    CHECK(ChangeElementSetting(kit, "extension", "clock", "seconds", "on", error));
    CHECK(ReadFile(kit / "settings.txt").find("extensions.clock.seconds = true") != std::string::npos);

    CHECK(!ChangeElementSetting(kit, "extension", "clock", "size", "11", error));
    CHECK(error.find("size") != std::string::npos);
    CHECK(!ChangeElementSetting(kit, "extension", "clock", "seconds", "maybe", error));
    CHECK(!ChangeElementSetting(kit, "extension", "clock", "colour", "red", error));
    CHECK(!ChangeElementSetting(kit, "extension", "nothing", "size", "2", error));
    CHECK(ReadFile(kit / "settings.txt").find("extensions.clock.size = 7") != std::string::npos);
}

TEST_CASE(UiRender_ChangingSeveralElementValuesWritesAllOfThemOnceOrNone) {
    fs::path kit = FreshKit("element-values-change");
    WriteFile(kit / "extensions" / "clock" / "manifest.json",
              R"({"id":"clock","name":"Clock","version":"1.0.0","kind":"extension","author":"x","description":"d",)"
              R"("kitApi":"1.0","main":"main.js","settings":[)"
              R"({"key":"size","label":"Size","type":"number","min":1,"max":10,"default":4},)"
              R"({"key":"seconds","label":"Seconds","type":"toggle","default":false}]})");
    WriteFile(kit / "extensions" / "clock" / "main.js", "");
    std::string error;

    CHECK(ChangeElementValues(kit, "extension", "clock", {{"size", "7"}, {"seconds", "true"}}, error));
    std::string settings = ReadFile(kit / "settings.txt");
    CHECK(settings.find("extensions.clock.size = 7") != std::string::npos);
    CHECK(settings.find("extensions.clock.seconds = true") != std::string::npos);
    CHECK(ReadFile(kit / "reload.txt") == "restart clock\n");

    CHECK(!ChangeElementValues(kit, "extension", "clock", {{"size", "9"}, {"colour", "red"}}, error));
    CHECK(error.find("colour") != std::string::npos);
    CHECK(!ChangeElementValues(kit, "extension", "clock", {{"size", "11"}}, error));
    CHECK(ReadFile(kit / "settings.txt").find("extensions.clock.size = 7") != std::string::npos);
    CHECK(ReadFile(kit / "reload.txt") == "restart clock\n");
}
