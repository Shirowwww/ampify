// Ampify.tray against a host that records the menu: permission, setting, the items sent to the host, and the clicks
// that come back through dispatch.tray.

#include <string>

#include "service_host.hpp"
#include "test.hpp"

TEST_CASE(TrayModule_NeedsPermissionAndSetting) {
    ampify::test::ServiceFixture<> f;
    auto without = f.Make("tray-none", R"(try { Ampify.tray.setMenu([]); } catch (e) { Ampify.log("refused " + e.name); })");
    CHECK(without->Start());
    CHECK(f.Logged("refused PermissionDenied"));

    auto off = f.Make("tray-off", R"(Ampify.log("shown " + Ampify.tray.setMenu([{ label: "A", onClick() {} }]));)",
                      R"("ui.tray")");
    CHECK(off->Start());
    CHECK(f.Logged("shown false"));
    CHECK(f.Logged("Notification area icon: off (setting tray.enabled is 0)"));
    CHECK(f.host.trayMenus.empty());
}

TEST_CASE(TrayModule_SendsItemsAndReceivesClicks) {
    ampify::test::ServiceFixture<> f;
    f.host.settings["tray.enabled"] = "1";
    auto element = f.Make("tray-demo", R"(
        Ampify.log("shown " + Ampify.tray.setMenu([
          { label: "Mode calm", checked: true, onClick: () => Ampify.log("calm") },
          "-",
          { label: "Greyed", enabled: false, onClick: () => Ampify.log("never") },
        ]));
        Ampify.tray.onClick(() => Ampify.log("icon"));
        try { Ampify.tray.setMenu([{ label: "no function" }]); } catch (e) { Ampify.log("entry " + e.name); }
        try { Ampify.tray.setMenu(new Array(21).fill("-")); } catch (e) { Ampify.log("too many " + e.name); }
        try { Ampify.tray.setMenu([{ label: "", onClick() {} }]); } catch (e) { Ampify.log("empty " + e.name); }
    )", R"("ui.tray")");
    CHECK(element->Start());
    CHECK(f.Logged("shown true"));
    CHECK(f.Logged("entry TypeError"));
    CHECK(f.Logged("too many RangeError"));
    CHECK(f.Logged("empty TypeError"));

    const auto& items = f.host.trayMenus["tray-demo"];
    CHECK(items.size() == 3);
    if (items.size() == 3) {
        CHECK(items[0].id == 1 && items[0].label == "Mode calm" && items[0].checked && items[0].enabled);
        CHECK(items[1].separator);
        CHECK(items[2].id == 3 && !items[2].enabled);
    }

    element->DeliverEvent("tray", R"({"to":"tray-demo","item":1})");
    element->DeliverEvent("tray", R"({"to":"other","item":1})");  // another element's item
    element->DeliverEvent("tray", R"({"click":true})");
    element->Pump();
    CHECK(f.out.str().find("[tray-demo] calm") != std::string::npos);
    CHECK(f.out.str().find("[tray-demo] calm", f.out.str().find("[tray-demo] calm") + 1) == std::string::npos);
    CHECK(f.Logged("[tray-demo] icon"));

    element->Stop();
    CHECK(f.host.trayMenus.count("tray-demo") == 0);
    CHECK(!f.host.trayRemoved.empty() && f.host.trayRemoved[0] == "tray-demo");
}
