// Ampify.clipboard (copy and paste, each behind its own permission) and Ampify.keyboard.unbind, with a fake host.

#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>

#include "../src/core/manifest.hpp"
#include "../src/core/natives/clipboard.hpp"
#include "../src/core/script_extension.hpp"
#include "fake_host.hpp"
#include "test.hpp"

namespace {

namespace fs = std::filesystem;

struct ClipboardFakeHost : ampify::test::FakeHost, ampify::ClipboardHost {
    std::optional<std::string> clipboard;
    int copies = 0;

    bool CopyText(const std::string& text) override {
        copies++;
        clipboard = text;
        return true;
    }
    std::optional<std::string> PasteText() override { return clipboard; }
    void* QueryCapability(std::string_view name) override {
        if (void* found = Offer<ampify::ClipboardHost>(name, this)) return found;
        return FakeHost::QueryCapability(name);
    }
};

struct Fixture {
    ClipboardFakeHost host;
    std::ostringstream out;
    ampify::Journal journal{out, [] { return std::string("t"); }};

    std::unique_ptr<ampify::ScriptExtension> Make(ampify::Host& on, const std::string& id, const std::string& permissions,
                                               const std::string& main) {
        fs::path dir = fs::temp_directory_path() / "ampify-tests" / ("clip-" + id);
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
};

}  // namespace

TEST_CASE(Clipboard_CopiesAndPastesText) {
    Fixture f;
    auto element = f.Make("both", R"("clipboard.read", "clipboard.write")", R"(
        Ampify.log("copied " + Ampify.clipboard.copy("Song - Artist ✓"));
        Ampify.log("pasted " + Ampify.clipboard.paste());
        Ampify.clipboard.copy(42);
        Ampify.log("number " + Ampify.clipboard.paste());)");
    CHECK(element->Start());
    CHECK(f.Logged("[both] copied true"));
    CHECK(f.Logged("[both] pasted Song - Artist ✓"));
    CHECK(f.Logged("[both] number 42"));
}

TEST_CASE(Clipboard_ReadingAndWritingAreTwoPermissions) {
    Fixture f;
    f.host.clipboard = "a password the user copied";
    auto writer = f.Make("writer", R"("clipboard.write")", R"(
        try { Ampify.clipboard.paste(); Ampify.log("read allowed"); } catch (e) { Ampify.log("paste " + e.name); })");
    auto reader = f.Make("reader", R"("clipboard.read")", R"(
        try { Ampify.clipboard.copy("x"); Ampify.log("write allowed"); } catch (e) { Ampify.log("copy " + e.name); })");
    CHECK(writer->Start());
    CHECK(reader->Start());
    CHECK(f.Logged("[writer] paste PermissionDenied"));
    CHECK(f.Logged("[reader] copy PermissionDenied"));
    CHECK(!f.Logged("allowed"));
    CHECK(f.host.copies == 0);
}

TEST_CASE(Clipboard_NoTextIsNullAndTooMuchTextIsRefused) {
    Fixture f;
    auto element = f.Make("limits", R"("clipboard.read", "clipboard.write")", R"(
        Ampify.log("empty " + Ampify.clipboard.paste());
        try { Ampify.clipboard.copy("x".repeat(1024 * 1024 + 1)); } catch (e) { Ampify.log("big " + e.name); })");
    CHECK(element->Start());
    CHECK(f.Logged("[limits] empty null"));
    CHECK(f.Logged("[limits] big RangeError"));
    CHECK(f.host.copies == 0);
}

TEST_CASE(Clipboard_WithoutTheServiceCopyIsFalseAndPasteNull) {
    Fixture f;
    ampify::test::FakeHost plain;
    auto element = f.Make(plain, "plain", R"("clipboard.read", "clipboard.write")", R"(
        Ampify.log("copy " + Ampify.clipboard.copy("x") + " paste " + Ampify.clipboard.paste());)");
    CHECK(element->Start());
    CHECK(f.Logged("[plain] copy false paste null"));
}

TEST_CASE(Keyboard_UnbindGivesTheShortcutBackAndBindTakesItAgain) {
    Fixture f;
    auto element = f.Make("unbind", R"("ui.keyboard")", R"(
        Ampify.keyboard.bind("Ctrl+Alt+K", () => Ampify.log("first"));
        Ampify.log("gone " + Ampify.keyboard.unbind("ctrl+alt+k") + " again " + Ampify.keyboard.unbind("Ctrl+Alt+K"));
        Ampify.log("never " + Ampify.keyboard.unbind("Ctrl+Alt+J"));
        Ampify.keyboard.bind("Ctrl+Alt+K", () => Ampify.log("second"));)");
    CHECK(element->Start());
    CHECK(f.Logged("[unbind] gone true again false"));
    CHECK(f.Logged("[unbind] never false"));
    CHECK(f.host.keys.count("Ctrl+Alt+K") == 1);  // given back, then taken again
    element->DeliverKey("Ctrl+Alt+K");
    element->Pump();
    CHECK(f.Logged("[unbind] second"));
    CHECK(!f.Logged("[unbind] first"));
}

TEST_CASE(Keyboard_UnbindLeavesOtherElementsShortcutsAlone) {
    Fixture f;
    auto owner = f.Make("owner", R"("ui.keyboard")", R"(Ampify.keyboard.bind("Ctrl+Alt+M", () => {});)");
    auto other = f.Make("other", R"("ui.keyboard")", R"(Ampify.log("other " + Ampify.keyboard.unbind("Ctrl+Alt+M"));)");
    CHECK(owner->Start());
    CHECK(other->Start());
    CHECK(f.Logged("[other] other false"));
    CHECK(f.host.keys.count("Ctrl+Alt+M") == 1);
}
