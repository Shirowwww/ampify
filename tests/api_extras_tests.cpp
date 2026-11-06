// The smaller additions of the extension API: buttons and menu entries taken out, greyed and kept to some kinds of
// item, named popups shown and closed by code, the title read and reset, storage.clear, the uri predicates, Apple
// Music's version and the active theme, ConfirmDialog and useArtworkColor.

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

#include "../src/core/app_info.hpp"
#include "../src/core/i18n.hpp"
#include "../src/core/manifest.hpp"
#include "../src/core/script_extension.hpp"
#include "../src/core/ui_model.hpp"
#include "fake_host.hpp"
#include "test.hpp"

namespace {

namespace fs = std::filesystem;

struct InfoHost : ampify::test::FakeHost, ampify::AppInfoHost {
    std::string theme = "ember";
    std::string AppVersion() override { return "1.1792.23042.0"; }
    std::string ActiveTheme() override { return theme; }
    void* QueryCapability(std::string_view name) override {
        if (void* found = Offer<ampify::AppInfoHost>(name, this)) return found;
        return FakeHost::QueryCapability(name);
    }
};

struct Fixture {
    InfoHost host;
    std::ostringstream out;
    ampify::Journal journal{out, [] { return std::string("t"); }};

    std::unique_ptr<ampify::ScriptExtension> Make(ampify::Host& on, const std::string& id, const std::string& permissions,
                                               const std::string& main) {
        fs::path dir = fs::temp_directory_path() / "ampify-tests" / ("extras-api-" + id);
        fs::remove_all(dir);
        fs::create_directories(dir);
        const std::string manifest =
            R"({"id":")" + id + R"(","name":"Demo","version":"1.0.0","kind":"extension","author":"Shirowwww",)" +
            R"("description":"Test.","kitApi":"1.0","main":"main.js","permissions":[)" + permissions + "]}";
        std::ofstream(dir / "main.js") << main;
        ampify::ManifestResult result = ampify::ParseManifest(manifest, id, 1, [](const std::string&) { return true; });
        return std::make_unique<ampify::ScriptExtension>(*result.manifest, dir, on, journal);
    }
    std::unique_ptr<ampify::ScriptExtension> Make(const std::string& id, const std::string& permissions,
                                               const std::string& main) {
        return Make(host, id, permissions, main);
    }

    bool Logged(const std::string& text) const { return out.str().find(text) != std::string::npos; }
    bool HasPopup(const std::string& id) {
        for (const auto& popup : host.ui.Popups()) {
            if (popup.id == id) return true;
        }
        return false;
    }
};

}  // namespace

// ---- Buttons and menu entries ----------------------------------------------------------------------------------

TEST_CASE(ApiExtras_AButtonIsTakenOutAndItsClickGoesWithIt) {
    Fixture f;
    auto element = f.Make("remove", R"("ui.topbar", "ui.playbar")", R"(
        const a = Ampify.ui.topbar.addButton({ label: "A", onClick: () => Ampify.log("a clicked") });
        Ampify.ui.playbar.addButton({ label: "B", onClick: () => {} });
        Ampify.log("removed " + Ampify.ui.topbar.remove(a) + " again " + Ampify.ui.topbar.remove(a));)");
    CHECK(element->Start());
    CHECK(f.Logged("[remove] removed true again false"));
    auto controls = f.host.ui.Controls();
    CHECK(controls.size() == 1 && controls[0].label == "B");
}

TEST_CASE(ApiExtras_RemovingASubmenuTakesItsEntries) {
    Fixture f;
    auto element = f.Make("submenu", R"("ui.menu")", R"(
        const id = Ampify.ui.menu.addItem("context", { label: "Share", items: [
          { label: "Copy", onClick: () => {} }, { label: "Send", onClick: () => {} }] });
        Ampify.ui.menu.addItem("context", { label: "Keep", onClick: () => {} });
        Ampify.log("removed " + Ampify.ui.menu.remove(id));)");
    CHECK(element->Start());
    CHECK(f.Logged("[submenu] removed true"));
    auto controls = f.host.ui.Controls();
    CHECK(controls.size() == 1 && controls[0].label == "Keep");
}

TEST_CASE(ApiExtras_AnElementTakesOutOrGreysOnlyWhatItAdded) {
    Fixture f;
    auto owner = f.Make("owner", R"("ui.topbar")", R"(Ampify.ui.topbar.addButton({ label: "Mine", onClick: () => {} });)");
    CHECK(owner->Start());
    int id = f.host.ui.Controls()[0].id;
    auto other = f.Make("other", R"("ui.topbar")", "Ampify.log(\"remove \" + Ampify.ui.topbar.remove(" +
                                                         std::to_string(id) + ") + \" grey \" + Ampify.ui.setDisabled(" +
                                                         std::to_string(id) + ", true));");
    CHECK(other->Start());
    CHECK(f.Logged("[other] remove false grey false"));
    CHECK(f.host.ui.Controls().size() == 1 && !f.host.ui.Controls()[0].disabled);
}

TEST_CASE(ApiExtras_AControlStartsGreyedAndSetDisabledChangesIt) {
    Fixture f;
    auto element = f.Make("grey", R"("ui.topbar", "ui.menu")", R"(
        const b = Ampify.ui.topbar.addButton({ label: "B", onClick: () => {}, disabled: true });
        Ampify.ui.menu.addItem("app", { label: "M", onClick: () => {} });
        globalThis.enable = () => Ampify.ui.setDisabled(b, false);
        Ampify.log("enabled " + enable());)");
    CHECK(element->Start());
    CHECK(f.Logged("[grey] enabled true"));
    for (const auto& control : f.host.ui.Controls()) CHECK(!control.disabled);
    auto again = f.Make("grey2", R"("ui.menu")", R"(Ampify.ui.menu.addItem("app", { label: "M", onClick: () => {}, disabled: true });)");
    CHECK(again->Start());
    bool greyed = false;
    for (const auto& control : f.host.ui.Controls()) greyed = greyed || (control.elementId == "grey2" && control.disabled);
    CHECK(greyed);
}

TEST_CASE(ApiExtras_ContextEntriesKeepToTheirKindsOfItem) {
    Fixture f;
    auto element = f.Make("kinds", R"("ui.menu")", R"(
        Ampify.ui.menu.addItem("context", { label: "Lyrics", onClick: () => {}, kinds: ["song"] });
        Ampify.ui.menu.addItem("context", { label: "Group", kinds: ["album", "playlist"], items: [{ label: "x", onClick: () => {} }] });
        for (const [where, kinds] of [["context", ["track"]], ["context", []], ["context", "song"], ["app", ["song"]]]) {
          try { Ampify.ui.menu.addItem(where, { label: "bad", onClick: () => {}, kinds }); Ampify.log("accepted"); }
          catch (e) { Ampify.log("refused " + e.name); }
        })");
    CHECK(element->Start());
    CHECK(!f.Logged("accepted"));
    CHECK(f.Logged("[kinds] refused RangeError"));  // an unknown kind
    CHECK(f.Logged("[kinds] refused TypeError"));
    auto controls = f.host.ui.Controls();
    CHECK(controls.size() == 3);  // Lyrics, Group and its entry: the refused ones left nothing behind
    int lyrics = 0;
    for (const auto& control : controls) {
        if (control.label == "Lyrics") {
            lyrics++;
            CHECK(ampify::ShowsForItem(control, "song"));
            CHECK(!ampify::ShowsForItem(control, "album"));
            CHECK(!ampify::ShowsForItem(control, ""));  // Apple's data says nothing: not shown
        }
        if (control.label == "Group") CHECK((control.kinds == std::vector<std::string>{"album", "playlist"}));
        if (control.label == "x") CHECK(control.kinds.empty());
    }
    CHECK(lyrics == 1);
}

TEST_CASE(ApiExtras_AnEntryWithoutKindsShowsInEveryMenu) {
    ampify::UiControl control;
    CHECK(ampify::ShowsForItem(control, "song"));
    CHECK(ampify::ShowsForItem(control, ""));
    CHECK(ampify::IsMenuItemKind("station"));
    CHECK(!ampify::IsMenuItemKind("track"));
}

// ---- Popups and title ----------------------------------------------------------------------------------------------

TEST_CASE(ApiExtras_ANamedPopupIsShownChangedInPlaceAndClosed) {
    Fixture f;
    auto element = f.Make("popup", R"("ui.popup")", R"(
        Ampify.ui.popup.show("ask", { type: "text", text: "one" });
        Ampify.ui.popup.show("ask", { type: "button", label: "two", onClick: () => Ampify.log("clicked") });)");
    CHECK(element->Start());
    auto popups = f.host.ui.Popups();
    CHECK(popups.size() == 1);
    CHECK(popups.size() == 1 && popups[0].id == "ask" && popups[0].tree.Dump().find("two") != std::string::npos);
    auto close = f.Make("closer", R"("ui.popup")", R"(
        Ampify.ui.popup.show("bye", { type: "text", text: "x" });
        Ampify.ui.popup.close("bye");
        try { Ampify.ui.popup.show("Not A Name", { type: "text", text: "x" }); } catch (e) { Ampify.log("name " + e.name); })");
    CHECK(close->Start());
    CHECK(f.Logged("[closer] name TypeError"));
    for (const auto& popup : f.host.ui.Popups()) CHECK(popup.elementId != "closer");
}

TEST_CASE(ApiExtras_TheTitleIsReadAndGoesBackToTheName) {
    Fixture f;
    auto element = f.Make("title", R"("ui.window")", R"(
        Ampify.log("start " + Ampify.ui.title.get());
        Ampify.ui.title.set("Now playing");
        Ampify.log("set " + Ampify.ui.title.get());
        Ampify.ui.title.reset();
        Ampify.log("reset " + Ampify.ui.title.get());)");
    CHECK(element->Start());
    CHECK(f.Logged("[title] start Demo"));
    CHECK(f.Logged("[title] set Now playing"));
    CHECK(f.Logged("[title] reset Demo"));
}

// ---- Storage, uri, app ---------------------------------------------------------------------------------------------

TEST_CASE(ApiExtras_StorageClearEmptiesTheElementsValues) {
    Fixture f;
    auto element = f.Make("store", R"("storage")", R"(
        Ampify.storage.set("a", 1);
        Ampify.storage.set("b", { c: 2 });
        Ampify.storage.clear();
        Ampify.log("after " + Ampify.storage.get("a", "none") + " " + Ampify.storage.get("b", "none"));)");
    CHECK(element->Start());
    CHECK(f.Logged("[store] after none none"));
    CHECK(f.host.storage["store"] == "{}");
    auto denied = f.Make("nostore", "", R"(try { Ampify.storage.clear(); } catch (e) { Ampify.log("clear " + e.name); })");
    CHECK(denied->Start());
    CHECK(f.Logged("[nostore] clear PermissionDenied"));
}

TEST_CASE(ApiExtras_UriReadsAppleMusicsOwnSchemesAndTellsKindsApart) {
    Fixture f;
    auto element = f.Make("uri", "", R"(
        const u = Ampify.uri;
        const song = "itmss://music.apple.com/fr/album/name/123?i=456";
        Ampify.log("itmss " + JSON.stringify(u.parse(song)));
        Ampify.log("geo " + JSON.stringify(u.parse("https://geo.music.apple.com/us/playlist/x/pl.u-abc")));
        Ampify.log("song " + u.isSong(song) + " album " + u.isAlbum(song) + " parsed " + u.isSong(u.parse(song)));
        Ampify.log("album " + u.isAlbum("music://music.apple.com/us/album/x/123") + " playlist " +
                   u.isPlaylist("https://music.apple.com/us/playlist/x/pl.u-abc") + " artist " +
                   u.isArtist("https://music.apple.com/us/artist/x/99") + " station " +
                   u.isStation("https://music.apple.com/us/station/x/ra.1") + " other " + u.isSong("https://example.org/"));)");
    CHECK(element->Start());
    CHECK(f.Logged(R"([uri] itmss {"kind":"album","storefront":"fr","id":"123","trackId":"456"})"));
    CHECK(f.Logged(R"([uri] geo {"kind":"playlist","storefront":"us","id":"pl.u-abc","trackId":null})"));
    CHECK(f.Logged("[uri] song true album false parsed true"));
    CHECK(f.Logged("[uri] album true playlist true artist true station true other false"));
}

TEST_CASE(ApiExtras_AppleMusicsVersionAndTheActiveTheme) {
    Fixture f;
    auto element = f.Make("app", "", R"(
        Ampify.log("version " + Ampify.appVersion + " theme " + Ampify.activeTheme());)");
    CHECK(element->Start());
    CHECK(f.Logged("[app] version 1.1792.23042.0 theme ember"));
    ampify::test::FakeHost plain;
    auto bare = f.Make(plain, "bare", "", R"(Ampify.log("version [" + Ampify.appVersion + "] theme " + Ampify.activeTheme());)");
    CHECK(bare->Start());
    CHECK(f.Logged("[bare] version [] theme null"));
}

// ---- Components ----------------------------------------------------------------------------------------------------

TEST_CASE(ApiExtras_ConfirmDialogShowsTwoButtonsInTheDisplayLanguage) {
    Fixture f;
    auto element = f.Make("confirm", R"("ui.popup")", R"(
        const { h } = Ampify.React;
        Ampify.ReactDOM.render(h(Ampify.components.ConfirmDialog, { isOpen: true, titleText: "Delete?",
          descriptionText: "It goes for good.", onConfirm: () => Ampify.log("yes"), onClose: () => Ampify.log("no") }),
          "popup:confirm");
        Ampify.ReactDOM.render(h(Ampify.components.ConfirmDialog, { isOpen: false }), "popup:closed");)");
    CHECK(element->Start());
    bool shown = false;
    for (const auto& popup : f.host.ui.Popups()) {
        if (popup.id != "confirm") continue;
        shown = true;
        std::string tree = popup.tree.Dump();
        CHECK(tree.find("Delete?") != std::string::npos);
        // The defaults, in the display language.
        CHECK(tree.find(ampify::i18n::Text("ui.confirm")) != std::string::npos);
        CHECK(tree.find(ampify::i18n::Text("ui.cancel")) != std::string::npos);
    }
    CHECK(shown);
    CHECK(!f.HasPopup("closed"));
}

TEST_CASE(ApiExtras_ConfirmDialogClosesWhenRenderedClosed) {
    Fixture f;
    auto element = f.Make("confirm", R"("ui.popup", "ui.menu")", R"(
        const { h } = Ampify.React;
        const show = (isOpen) => Ampify.ReactDOM.render(
          h(Ampify.components.ConfirmDialog, { isOpen, titleText: "Delete?" }), "popup:confirm");
        show(true);
        const drop = Ampify.ui.menu.addItem("app", { label: "Drop", onClick: () => show(false) });
        const ask = Ampify.ui.menu.addItem("app", { label: "Ask", onClick: () => show(true) });
        Ampify.log("entries " + drop + " " + ask);)");
    CHECK(element->Start());
    CHECK(f.HasPopup("confirm"));
    std::string log = f.out.str();
    std::size_t at = log.find("[confirm] entries ");
    CHECK(at != std::string::npos);
    int drop = 0;
    int ask = 0;
    std::istringstream(log.substr(at + 18)) >> drop >> ask;
    element->DeliverClick(drop);
    element->Pump();
    CHECK(!f.HasPopup("confirm"));
    element->DeliverClick(ask);
    element->Pump();
    CHECK(f.HasPopup("confirm"));
    element->Stop();
}

TEST_CASE(ApiExtras_UseArtworkColorIsNullWithoutATrack) {
    Fixture f;
    auto element = f.Make("colors", R"("ui.panel", "player.read")", R"(
        const { h } = Ampify.React;
        function Panel() {
          const colors = Ampify.hooks.useArtworkColor();
          Ampify.log("colors " + JSON.stringify(colors));
          return h("text", { text: "x" });
        }
        Ampify.ReactDOM.render(h(Panel), "panel:colors");)");
    CHECK(element->Start());
    CHECK(f.Logged("[colors] colors null"));
}
