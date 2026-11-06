// The conveniences of Ampify.player built on its state and setVolume: progress, skipForward/skipBack, formatTime, mute
// and volume steps, and the progress event. A fake host whose volume follows what setVolume asked, as Apple's does.

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

#include "../src/core/audio/audio_engine.hpp"
#include "../src/core/manifest.hpp"
#include "../src/core/player_modes.hpp"
#include "../src/core/script_extension.hpp"
#include "fake_host.hpp"
#include "test.hpp"

namespace {

namespace fs = std::filesystem;

struct VolumeHost : ampify::test::FakeHost, ampify::PlayerVolumeHost {
    ampify::audio::AudioEngine engine;
    double volume = -1;

    VolumeHost() { audio = &engine; }
    bool SetVolume(double value) override {
        volume = value;
        engine.PublishPlayerVolume(value);
        return true;
    }
    void* QueryCapability(std::string_view name) override {
        if (void* found = Offer<ampify::PlayerVolumeHost>(name, this)) return found;
        return FakeHost::QueryCapability(name);
    }
};

struct Fixture {
    VolumeHost host;
    std::ostringstream out;
    ampify::Journal journal{out, [] { return std::string("t"); }};

    std::unique_ptr<ampify::ScriptExtension> Make(const std::string& id, const std::string& permissions,
                                               const std::string& main) {
        fs::path dir = fs::temp_directory_path() / "ampify-tests" / ("extras-" + id);
        fs::remove_all(dir);
        fs::create_directories(dir);
        const std::string manifest =
            R"({"id":")" + id + R"(","name":"Demo","version":"1.0.0","kind":"extension","author":"Shirowwww",)" +
            R"("description":"Test.","kitApi":"1.0","main":"main.js","permissions":[)" + permissions + "]}";
        std::ofstream(dir / "main.js") << main;
        ampify::ManifestResult result = ampify::ParseManifest(manifest, id, 1, [](const std::string&) { return true; });
        return std::make_unique<ampify::ScriptExtension>(*result.manifest, dir, host, journal);
    }

    bool Logged(const std::string& text) const { return out.str().find(text) != std::string::npos; }

    void Play(int64_t positionMs, int64_t durationMs) {
        ampify::TrackInfo track;
        track.title = "Song";
        host.player.track = track;
        host.player.playing = true;
        host.player.positionMs = positionMs;
        host.player.durationMs = durationMs;
    }
};

const char kBoth[] = R"("player.read", "player.control")";

}  // namespace

TEST_CASE(PlayerExtras_FormatTimeWritesMinutesAndHours) {
    Fixture f;
    auto element = f.Make("format", "", R"(
        const t = Ampify.player.formatTime;
        Ampify.log([t(0), t(5_000), t(65_400), t(600_000), t(3_725_000), t(-61_000)].join(" "));
        try { t("soon"); } catch (e) { Ampify.log("bad " + e.name); })");
    CHECK(element->Start());
    CHECK(f.Logged("[format] 0:00 0:05 1:05 10:00 1:02:05 -1:01"));
    CHECK(f.Logged("[format] bad RangeError"));
}

TEST_CASE(PlayerExtras_SkipSeeksFromThePositionAndStaysInsideTheSong) {
    Fixture f;
    f.Play(100'000, 200'000);
    auto element = f.Make("skip", kBoth, R"(
        Ampify.log("a " + Ampify.player.skipForward() + " b " + Ampify.player.skipBack(150_000) + " c " +
                   Ampify.player.skipForward(500_000) + " p " + Ampify.player.progress());)");
    CHECK(element->Start());
    CHECK(f.Logged("[skip] a true b true c true p 0.5"));
    CHECK(f.host.commands.size() == 3);
}

TEST_CASE(PlayerExtras_SkipDoesNothingWithoutATrack) {
    Fixture f;
    auto element = f.Make("idle", kBoth, R"(
        Ampify.log("skip " + Ampify.player.skipForward() + " progress " + Ampify.player.progress());)");
    CHECK(element->Start());
    CHECK(f.Logged("[idle] skip false progress 0"));
    CHECK(f.host.commands.empty());
}

TEST_CASE(PlayerExtras_MuteGoesToTheBottomAndUnmutePutsTheLevelBack) {
    Fixture f;
    f.host.engine.PublishPlayerVolume(0.5);
    auto element = f.Make("mute", kBoth, R"(
        Ampify.log("before " + Ampify.player.muted());
        Ampify.log("mute " + Ampify.player.setMute(true) + " now " + Ampify.player.muted());
        Ampify.log("again " + Ampify.player.setMute(true));
        Ampify.log("toggle " + Ampify.player.toggleMute() + " now " + Ampify.player.muted());)");
    CHECK(element->Start());
    CHECK(f.Logged("[mute] before false"));
    CHECK(f.Logged("[mute] mute true now true"));
    CHECK(f.Logged("[mute] again true"));
    CHECK(f.Logged("[mute] toggle true now false"));
    CHECK_NEAR(f.host.volume, 0.5, 1e-9);  // the level before the first mute, not the muted one
}

TEST_CASE(PlayerExtras_UnmuteWithoutAKnownLevelGoesHalfWayUpTheSlider) {
    Fixture f;
    f.host.engine.PublishPlayerVolume(0.0);
    auto element = f.Make("unmute", kBoth, R"(Ampify.log("unmute " + Ampify.player.setMute(false));)");
    CHECK(element->Start());
    CHECK(f.Logged("[unmute] unmute true"));
    CHECK_NEAR(ampify::SliderPositionOfGain(f.host.volume), 0.5, 1e-9);
}

TEST_CASE(PlayerExtras_VolumeStepsMoveTheSliderAndStopAtItsEnds) {
    Fixture f;
    f.host.engine.PublishPlayerVolume(1.0);
    auto element = f.Make("steps", kBoth, R"(
        Ampify.log("down " + Ampify.player.decreaseVolume(0.25));
        Ampify.player.increaseVolume(0.5);
        Ampify.log("up " + Ampify.audio.volume());
        for (const bad of [0, -0.1, 2, "much"]) {
          try { Ampify.player.increaseVolume(bad); Ampify.log("accepted " + bad); } catch (e) { Ampify.log("step " + e.name); }
        })");
    CHECK(element->Start());
    CHECK(f.Logged("[steps] down true"));
    CHECK(f.Logged("[steps] up 1"));
    CHECK(!f.Logged("accepted"));
    CHECK(f.Logged("[steps] step RangeError"));
}

TEST_CASE(PlayerExtras_MuteAndStepsAnswerFalseWhileTheVolumeIsUnknown) {
    Fixture f;
    f.host.audio = nullptr;
    auto element = f.Make("unknown", kBoth, R"(
        const p = Ampify.player;
        Ampify.log([p.muted(), p.setMute(true), p.toggleMute(), p.increaseVolume(), p.decreaseVolume()].map(String).join(" "));)");
    CHECK(element->Start());
    CHECK(f.Logged("[unknown] null false false false false"));
    CHECK(f.host.volume < 0);
}

TEST_CASE(PlayerExtras_MuteNeedsBothPlayerPermissions) {
    Fixture f;
    f.host.engine.PublishPlayerVolume(0.5);
    auto readOnly = f.Make("readonly", R"("player.read")", R"(
        try { Ampify.player.setMute(true); } catch (e) { Ampify.log("mute " + e.name); })");
    auto none = f.Make("none", "", R"(try { Ampify.player.muted(); } catch (e) { Ampify.log("muted " + e.name); })");
    CHECK(readOnly->Start());
    CHECK(none->Start());
    CHECK(f.Logged("[readonly] mute PermissionDenied"));
    CHECK(f.Logged("[none] muted PermissionDenied"));
    CHECK(f.host.volume < 0);
}

TEST_CASE(PlayerExtras_TheProgressEventAndItsSpicetifyName) {
    Fixture f;
    auto element = f.Make("progress", R"("player.read")", R"(
        Ampify.player.on("progress", (s) => Ampify.log("progress " + s.positionMs));
        Ampify.player.on("onprogress", (s) => Ampify.log("onprogress " + s.positionMs));)");
    CHECK(element->Start());
    f.Play(42'000, 200'000);
    element->DeliverPlayer("progress", f.host.player);
    element->Pump();
    CHECK(f.Logged("[progress] progress 42000"));
    CHECK(f.Logged("[progress] onprogress 42000"));
}
