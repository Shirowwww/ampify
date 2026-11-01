// Windows only: the Host's files (settings.txt and storage\<id>.json).
#include <windows.h>

#include <filesystem>
#include <string>

#include "../src/core/settings.hpp"
#include "../src/win/win_host.hpp"
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

TEST_CASE(WinHost_SettingWriteKeepsAChangeMadeSinceTheKernelRead) {
    fs::path kit = KitForTest("settings-merge");
    ampify::Settings settings;  // the kernel's copy, read before the Ampify card writes
    std::ostringstream log;
    ampify::Journal journal(log, [] { return std::string("t"); });
    ampify::WinHost host("ui", kit, settings, journal);
    std::string error;
    CHECK(ampify::Settings::UpdateFile((kit / "settings.txt").string(), "extensions.demo.side", "Right", error));

    host.WriteSetting("extensions.demo.volume", "7");

    ampify::Settings reloaded;
    CHECK(reloaded.Load((kit / "settings.txt").string()));
    CHECK(reloaded.GetString("extensions.demo.side", "") == "Right");
    CHECK(reloaded.GetInt("extensions.demo.volume", 0) == 7);
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

TEST_CASE(WinHost_DecodesAnImageScaledToFit) {
    fs::path kit = KitForTest("image");
    ampify::Settings settings;
    std::ostringstream log;
    ampify::Journal journal(log, [] { return std::string("t"); });
    ampify::WinHost host("ui", kit, settings, journal);
    ampify::ImageHost* images = host.Capability<ampify::ImageHost>();
    CHECK(images != nullptr);
    if (!images) return;

    // A 4 x 2 PNG: two red columns, then two blue ones.
    const unsigned char png[] = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00,
        0x00, 0x04, 0x00, 0x00, 0x00, 0x02, 0x08, 0x06, 0x00, 0x00, 0x00, 0x7F, 0xA8, 0x7D, 0x63, 0x00, 0x00, 0x00,
        0x14, 0x49, 0x44, 0x41, 0x54, 0x78, 0xDA, 0x63, 0xF8, 0xCF, 0xC0, 0xF0, 0x1F, 0x84, 0xA1, 0xD4, 0x7F, 0x06,
        0x74, 0x01, 0x00, 0x07, 0x29, 0x0F, 0xF1, 0x27, 0xDA, 0x74, 0xA6, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E,
        0x44, 0xAE, 0x42, 0x60, 0x82};
    const std::string bytes(reinterpret_cast<const char*>(png), sizeof png);

    std::optional<ampify::RgbaImage> whole = images->DecodeImage(bytes, 128);
    CHECK(whole && whole->width == 4 && whole->height == 2 && whole->pixels.size() == 32);
    if (whole) {
        CHECK(whole->pixels[0] == 255 && whole->pixels[1] == 0 && whole->pixels[2] == 0 && whole->pixels[3] == 255);
        CHECK(whole->pixels[12] == 0 && whole->pixels[13] == 0 && whole->pixels[14] == 255);
    }
    std::optional<ampify::RgbaImage> small = images->DecodeImage(bytes, 2);
    CHECK(small && small->width == 2 && small->height == 1);
    if (small) {
        CHECK(small->pixels[0] > 200 && small->pixels[2] < 50);  // the red half
        CHECK(small->pixels[4] < 50 && small->pixels[6] > 200);  // the blue half
    }
    CHECK(!images->DecodeImage("not an image", 128).has_value());
    CHECK(!images->DecodeImage("", 128).has_value());
}
