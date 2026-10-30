// AMK.tray against a host that records the menu: permission, setting, the items sent to the host, and the clicks
// that come back through dispatch.tray.

#include <string>

#include "service_host.hpp"
#include "test.hpp"

TEST_CASE(TrayModule_NeedsPermissionAndSetting) {
    amk::test::ServiceFixture<> f;
    auto without = f.Make("tray-none", R"(try { AMK.tray.setMenu([]); } catch (e) { AMK.log("refus " + e.name); })");
    CHECK(without->Start());
    CHECK(f.Logged("refus PermissionDenied"));

    auto off = f.Make("tray-off", R"(AMK.log("montré " + AMK.tray.setMenu([{ label: "A", onClick() {} }]));)",
                      R"("ui.tray")");
    CHECK(off->Start());
    CHECK(f.Logged("montré false"));
    CHECK(f.Logged("Icône de notification : désactivé (réglage tray.enabled à 0)"));
    CHECK(f.host.trayMenus.empty());
}

TEST_CASE(TrayModule_SendsItemsAndReceivesClicks) {
    amk::test::ServiceFixture<> f;
    f.host.settings["tray.enabled"] = "1";
    auto element = f.Make("tray-demo", R"(
        AMK.log("montré " + AMK.tray.setMenu([
          { label: "Mode calme", checked: true, onClick: () => AMK.log("calme") },
          "-",
          { label: "Grisé", enabled: false, onClick: () => AMK.log("jamais") },
        ]));
        AMK.tray.onClick(() => AMK.log("icône"));
        try { AMK.tray.setMenu([{ label: "sans fonction" }]); } catch (e) { AMK.log("entrée " + e.name); }
        try { AMK.tray.setMenu(new Array(21).fill("-")); } catch (e) { AMK.log("trop " + e.name); }
        try { AMK.tray.setMenu([{ label: "", onClick() {} }]); } catch (e) { AMK.log("vide " + e.name); }
    )", R"("ui.tray")");
    CHECK(element->Start());
    CHECK(f.Logged("montré true"));
    CHECK(f.Logged("entrée TypeError"));
    CHECK(f.Logged("trop RangeError"));
    CHECK(f.Logged("vide TypeError"));

    const auto& items = f.host.trayMenus["tray-demo"];
    CHECK(items.size() == 3);
    if (items.size() == 3) {
        CHECK(items[0].id == 1 && items[0].label == "Mode calme" && items[0].checked && items[0].enabled);
        CHECK(items[1].separator);
        CHECK(items[2].id == 3 && !items[2].enabled);
    }

    element->DeliverEvent("tray", R"({"to":"tray-demo","item":1})");
    element->DeliverEvent("tray", R"({"to":"autre","item":1})");  // another element's item
    element->DeliverEvent("tray", R"({"click":true})");
    element->Pump();
    CHECK(f.out.str().find("[tray-demo] calme") != std::string::npos);
    CHECK(f.out.str().find("[tray-demo] calme", f.out.str().find("[tray-demo] calme") + 1) == std::string::npos);
    CHECK(f.Logged("[tray-demo] icône"));

    element->Stop();
    CHECK(f.host.trayMenus.count("tray-demo") == 0);
    CHECK(!f.host.trayRemoved.empty() && f.host.trayRemoved[0] == "tray-demo");
}
