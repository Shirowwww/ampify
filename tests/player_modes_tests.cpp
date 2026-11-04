// Ampify.player.shuffle(), repeat(), setShuffle(), setRepeat(), the toggles and setVolume(), with a fake host.

#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

#include "../src/core/manifest.hpp"
#include "../src/core/player_modes.hpp"
#include "../src/core/script_extension.hpp"
#include "fake_host.hpp"
#include "test.hpp"

namespace {

namespace fs = std::filesystem;

struct ModesFakeHost : ampify::test::FakeHost, ampify::PlayerModesHost, ampify::PlayerVolumeHost {
    ampify::PlayerModes modes;
    double volume = -1;
    bool canSetVolume = true;

    ampify::PlayerModes Modes() override { return modes; }
    bool SetShuffle(bool on) override {
        if (!modes.shuffle) return false;
        modes.shuffle = on;
        return true;
    }
    bool SetRepeat(const std::string& mode) override {
        if (modes.repeat.empty()) return false;
        modes.repeat = mode;
        return true;
    }
    bool SetVolume(double value) override {
        if (!canSetVolume) return false;
        volume = value;
        return true;
    }
    void* QueryCapability(std::string_view name) override {
        if (void* found = Offer<ampify::PlayerModesHost>(name, this)) return found;
        if (void* found = Offer<ampify::PlayerVolumeHost>(name, this)) return found;
        return FakeHost::QueryCapability(name);
    }
};

std::unique_ptr<ampify::ScriptExtension> Make(ampify::Host& host, ampify::Journal& journal, const std::string& id,
                                           const std::string& permissions, const std::string& main) {
    fs::path dir = fs::temp_directory_path() / "ampify-tests" / ("modes-" + id);
    fs::remove_all(dir);
    fs::create_directories(dir);
    const std::string manifest =
        R"({"id":")" + id + R"(","name":"Demo","version":"1.0.0","kind":"extension","author":"Shirowwww",)" +
        R"("description":"Test.","kitApi":"1.0","main":"main.js","permissions":[)" + permissions + "]}";
    std::ofstream(dir / "main.js") << main;
    ampify::ManifestResult result = ampify::ParseManifest(manifest, id, 1, [](const std::string&) { return true; });
    return std::make_unique<ampify::ScriptExtension>(*result.manifest, dir, host, journal);
}

bool Has(const std::ostringstream& out, const std::string& text) { return out.str().find(text) != std::string::npos; }

}  // namespace

TEST_CASE(PlayerModes_ReadsShuffleAndRepeatFromTheHost) {
    ModesFakeHost host;
    host.modes.shuffle = true;
    host.modes.repeat = "one";
    std::ostringstream out;
    ampify::Journal journal(out, [] { return std::string("t"); });
    auto element = Make(host, journal, "reader", R"("player.read")",
                        R"(Ampify.log("shuffle " + Ampify.player.shuffle() + " repeat " + Ampify.player.repeat());)");
    CHECK(element->Start());
    CHECK(Has(out, "[reader] shuffle true repeat one"));
}

TEST_CASE(PlayerModes_AreNullWhenTheHostDoesNotKnowThem) {
    ModesFakeHost known;  // the buttons are not on screen: nothing is known
    ampify::test::FakeHost plain;  // a host without the service
    std::ostringstream out;
    ampify::Journal journal(out, [] { return std::string("t"); });
    const std::string script = R"(Ampify.log("shuffle " + Ampify.player.shuffle() + " repeat " + Ampify.player.repeat());)";
    auto a = Make(known, journal, "unknown", R"("player.read")", script);
    auto b = Make(plain, journal, "absent", R"("player.read")", script);
    CHECK(a->Start());
    CHECK(b->Start());
    CHECK(Has(out, "[unknown] shuffle null repeat null"));
    CHECK(Has(out, "[absent] shuffle null repeat null"));
}

TEST_CASE(PlayerModes_SettersReachTheHostAndAnswerWhetherItWorked) {
    ModesFakeHost host;
    host.modes.shuffle = false;
    host.modes.repeat = "off";
    std::ostringstream out;
    ampify::Journal journal(out, [] { return std::string("t"); });
    auto element = Make(host, journal, "setter", R"("player.control")", R"(
        Ampify.log("a " + Ampify.player.setShuffle(true));
        Ampify.log("b " + Ampify.player.setRepeat("all"));)");
    CHECK(element->Start());
    CHECK(Has(out, "[setter] a true"));
    CHECK(Has(out, "[setter] b true"));
    CHECK(host.modes.shuffle.value_or(false));
    CHECK(host.modes.repeat == "all");

    ampify::test::FakeHost plain;
    auto without = Make(plain, journal, "without", R"("player.control")", R"(
        Ampify.log("c " + Ampify.player.setShuffle(true) + " " + Ampify.player.setRepeat("one") + " " +
                   Ampify.player.setVolume(0.5));)");
    CHECK(without->Start());
    CHECK(Has(out, "[without] c false false false"));
}

TEST_CASE(PlayerModes_TheTogglesFlipShuffleAndCycleRepeat) {
    ModesFakeHost host;
    host.modes.shuffle = false;
    host.modes.repeat = "off";
    std::ostringstream out;
    ampify::Journal journal(out, [] { return std::string("t"); });
    auto element = Make(host, journal, "toggler", R"("player.read", "player.control")", R"(
        Ampify.player.toggleShuffle();
        Ampify.log("shuffle " + Ampify.player.shuffle());
        const seen = [];
        for (let i = 0; i < 3; i++) { Ampify.player.toggleRepeat(); seen.push(Ampify.player.repeat()); }
        Ampify.log("repeat " + seen.join(">"));)");
    CHECK(element->Start());
    CHECK(Has(out, "[toggler] shuffle true"));
    CHECK(Has(out, "[toggler] repeat all>one>off"));
}

TEST_CASE(PlayerModes_ATogglePressesNothingWhileTheStateIsUnknown) {
    ModesFakeHost host;  // modes unknown
    std::ostringstream out;
    ampify::Journal journal(out, [] { return std::string("t"); });
    auto element = Make(host, journal, "blind", R"("player.read", "player.control")", R"(
        Ampify.log("s " + Ampify.player.toggleShuffle() + " r " + Ampify.player.toggleRepeat());)");
    CHECK(element->Start());
    CHECK(Has(out, "[blind] s false r false"));
}

TEST_CASE(PlayerModes_RefuseAnUnknownRepeatModeAndAVolumeOutsideZeroToOne) {
    ModesFakeHost host;
    host.modes.repeat = "off";
    std::ostringstream out;
    ampify::Journal journal(out, [] { return std::string("t"); });
    auto element = Make(host, journal, "strict", R"("player.control")", R"(
        for (const bad of ["twice", "", 3]) {
          try { Ampify.player.setRepeat(bad); Ampify.log("repeat accepted " + bad); } catch (e) { Ampify.log("repeat " + e.name); }
        }
        for (const bad of [-0.1, 1.5, NaN, "loud", Infinity]) {
          try { Ampify.player.setVolume(bad); Ampify.log("volume accepted " + bad); } catch (e) { Ampify.log("volume " + e.name); }
        }
        Ampify.log("edge " + Ampify.player.setVolume(0) + " " + Ampify.player.setVolume(1));)");
    CHECK(element->Start());
    CHECK(!Has(out, "accepted"));
    CHECK(Has(out, "[strict] repeat RangeError"));
    CHECK(Has(out, "[strict] volume RangeError"));
    CHECK(Has(out, "[strict] edge true true"));
    CHECK(host.volume == 1.0);
}

TEST_CASE(PlayerModes_VolumeAnswersFalseUntilAppleHasAPlayer) {
    ModesFakeHost host;
    host.canSetVolume = false;
    std::ostringstream out;
    ampify::Journal journal(out, [] { return std::string("t"); });
    auto element = Make(host, journal, "early", R"("player.control")", R"(Ampify.log("v " + Ampify.player.setVolume(0.3));)");
    CHECK(element->Start());
    CHECK(Has(out, "[early] v false"));
    CHECK(host.volume < 0);
}

TEST_CASE(PlayerModes_NeedThePlayerReadAndControlPermissions) {
    ModesFakeHost host;
    host.modes.shuffle = true;
    host.modes.repeat = "off";
    std::ostringstream out;
    ampify::Journal journal(out, [] { return std::string("t"); });
    auto element = Make(host, journal, "denied", "", R"(
        for (const [name, call] of [["shuffle", () => Ampify.player.shuffle()], ["repeat", () => Ampify.player.repeat()],
                                    ["setShuffle", () => Ampify.player.setShuffle(false)],
                                    ["setRepeat", () => Ampify.player.setRepeat("all")],
                                    ["setVolume", () => Ampify.player.setVolume(0.2)]]) {
          try { call(); Ampify.log(name + " allowed"); } catch (e) { Ampify.log(name + " " + e.name); }
        }
    )");
    CHECK(element->Start());
    CHECK(!Has(out, "allowed"));
    CHECK(Has(out, "[denied] shuffle PermissionDenied"));
    CHECK(Has(out, "[denied] setVolume PermissionDenied"));
    CHECK(host.modes.shuffle.value_or(false));
    CHECK(host.modes.repeat == "off");
}

TEST_CASE(PlayerVolume_TheSliderIsInDecibelsSoGainAndPositionAreNotTheSameNumber) {
    using ampify::SliderPositionOfGain;
    CHECK(SliderPositionOfGain(1.0) == 1.0);     // full gain is the end of the slider
    CHECK(SliderPositionOfGain(0.0) == 0.0);     // silence is its start
    CHECK(SliderPositionOfGain(-0.5) == 0.0);    // never below it
    CHECK(SliderPositionOfGain(2.0) == 1.0);     // never past it
    // 20 % of the slider is 24 dB down: a gain of 0.0631, the "20 %" the volume-percentage example shows for it.
    CHECK(std::abs(SliderPositionOfGain(std::pow(10.0, -24.0 / 20.0)) - 0.2) < 1e-9);
    CHECK(std::abs(SliderPositionOfGain(0.5) - 0.7993) < 1e-3);  // -6.02 dB, 6.02 / 30 below the end
}
