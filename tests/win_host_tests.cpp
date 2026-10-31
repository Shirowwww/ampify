// Windows only: the Host's files (settings.txt and storage\<id>.json), checked under Wine in the test run.
#include <windows.h>

#include <filesystem>
#include <string>

#include "../core/settings.hpp"
#include "../win/win_host.hpp"
#include "test.hpp"

namespace {

namespace fs = std::filesystem;

fs::path KitForTest(const std::string& name) {
    fs::path kit = fs::temp_directory_path() / "ampify-tests" / ("win-host-" + name);
    fs::remove_all(kit);
    fs::create_directories(kit);
    return kit;
}

}  // namespace

TEST_CASE(WinHost_StorageRoundTripsThroughTheKitFolder) {
    fs::path kit = KitForTest("storage");
    ampify::Settings settings;
    std::ostringstream log;
    ampify::Journal journal(log, [] { return std::string("t"); });
    ampify::WinHost host("engine", kit, settings, journal);

    CHECK(!host.ReadStorage("demo").has_value());
    CHECK(host.WriteStorage("demo", R"({"launches":2})"));
    CHECK(host.ReadStorage("demo").value_or("") == R"({"launches":2})");
    CHECK(fs::exists(kit / "storage" / "demo.json"));
    CHECK(!fs::exists(kit / "storage" / "demo.json.tmp"));
}

TEST_CASE(WinHost_SettingsArePersistedToSettingsTxt) {
    fs::path kit = KitForTest("settings");
    ampify::Settings settings;
    std::ostringstream log;
    ampify::Journal journal(log, [] { return std::string("t"); });
    ampify::WinHost host("ui", kit, settings, journal);

    host.WriteSetting("extensions.demo.volume", "7");
    CHECK(host.ReadSetting("extensions.demo.volume").value_or("") == "7");

    ampify::Settings reloaded;
    CHECK(reloaded.Load((kit / "settings.txt").string()));
    CHECK(reloaded.GetInt("extensions.demo.volume", 0) == 7);
    CHECK(host.Process() == "ui");
    CHECK(!host.Locale().empty());
}

TEST_CASE(WinHost_ShortcutPressIsDeliveredToTheRegisteringThread) {
    fs::path kit = KitForTest("hotkeys");
    ampify::Settings settings;
    std::ostringstream log;
    ampify::Journal journal(log, [] { return std::string("t"); });
    ampify::WinHost host("ui", kit, settings, journal);

    CHECK(host.RegisterKey("hk", "Ctrl+Alt+F12"));
    CHECK(!host.RegisterKey("hk", "Ctrl+Alt+Nope"));  // unknown key name

    // The system posts WM_HOTKEY with the registration id (the first one, in a fresh host) to the thread.
    PostThreadMessageW(GetCurrentThreadId(), WM_HOTKEY, 1, 0);
    std::string delivered;
    host.Service([&](const std::string& elementId, const std::string& combo) { delivered = elementId + " " + combo; });
    CHECK(delivered == "hk Ctrl+Alt+F12");

    host.UnregisterKeys("hk");
    delivered.clear();
    PostThreadMessageW(GetCurrentThreadId(), WM_HOTKEY, 1, 0);
    host.Service([&](const std::string& elementId, const std::string& combo) { delivered = elementId + " " + combo; });
    CHECK(delivered.empty());  // the registration is gone, so the id is ignored
}

TEST_CASE(WinHost_HttpFailureComesBackAsAnErrorValue) {
    fs::path kit = KitForTest("http");
    ampify::Settings settings;
    std::ostringstream log;
    ampify::Journal journal(log, [] { return std::string("t"); });
    ampify::WinHost host("engine", kit, settings, journal);

    ampify::HttpResponse response = host.HttpGet("https://nonexistent.invalid/x");
    CHECK(response.status == 0);
    CHECK(!response.text.empty());
}

TEST_CASE(WinHost_NotifyWritesTheJournalLine) {
    fs::path kit = KitForTest("notify");
    ampify::Settings settings;
    std::ostringstream log;
    ampify::Journal journal(log, [] { return std::string("t"); });
    ampify::WinHost host("ui", kit, settings, journal);

    host.Notify("hk", "Title", "Text");
    CHECK(log.str().find("[hk] notification: Title - Text") != std::string::npos);
}
