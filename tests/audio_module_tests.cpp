// Tests of Ampify.audio's analysis, equalizer, loudness and volume, and of Ampify.color, as an extension sees them: a
// script runs in the kernel's engine against the fake host, whose AudioHost is a real AudioEngine fed with
// synthetic signals.

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <numbers>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "../src/core/audio/audio_engine.hpp"
#include "../src/core/manifest.hpp"
#include "../src/core/script_extension.hpp"
#include "fake_host.hpp"
#include "pump.hpp"
#include "test.hpp"

namespace {

namespace fs = std::filesystem;

struct Fixture {
    ampify::test::FakeHost host;
    ampify::audio::AudioEngine engine;
    std::ostringstream out;
    ampify::Journal journal{out, [] { return std::string("t"); }};

    explicit Fixture(bool withAudio = true) {
        host.process = "engine";  // the audio is rendered in AMPLibraryAgent.exe
        if (withAudio) host.audio = &engine;
    }

    std::unique_ptr<ampify::ScriptExtension> Make(const std::string& id, const std::string& main,
                                               const std::string& permissions) {
        fs::path dir = fs::temp_directory_path() / "ampify-tests" / ("audio-" + id);
        fs::remove_all(dir);
        fs::create_directories(dir);
        const std::string manifest =
            R"({"id":")" + id + R"(","name":"Demo","version":"1.0.0","kind":"extension","author":"Shirowwww",)" +
            R"("description":"Test.","kitApi":"1.0","main":"main.js","permissions":[)" + permissions + "]}";
        std::ofstream(dir / "manifest.json") << manifest;
        std::ofstream(dir / "main.js") << main;
        ampify::ManifestResult result = ampify::ParseManifest(manifest, id, 1, [](const std::string&) { return true; });
        return std::make_unique<ampify::ScriptExtension>(*result.manifest, dir, host, journal);
    }

    bool Logged(const std::string& text) const { return out.str().find(text) != std::string::npos; }

    size_t Count(const std::string& text) const {
        size_t count = 0;
        for (size_t at = out.str().find(text); at != std::string::npos; at = out.str().find(text, at + 1)) count++;
        return count;
    }

    // `seconds` of a 1 kHz tone with a click every 0.5 s (120 BPM), stereo at 48 kHz.
    void Play(double seconds, size_t offset = 0) {
        std::vector<float> block(960);
        for (size_t start = 0; start < static_cast<size_t>(seconds * 48000); start += 480) {
            for (size_t i = 0; i < 480; i++) {
                size_t n = offset + start + i;
                double tone = 0.25 * std::sin(2 * std::numbers::pi * 1000.0 * static_cast<double>(n) / 48000.0);
                double click = (n % 24000) < 96 ? 0.6 : 0.0;
                block[2 * i] = block[2 * i + 1] = static_cast<float>(tone + click);
            }
            engine.FeedAnalysis(block.data(), 480, 2, 48000);
        }
    }
};

}  // namespace

TEST_CASE(AudioModule_OnFrameGetsBandsRmsAndPeak) {
    Fixture f;
    auto element = f.Make("frames", R"(
        Ampify.log("subscribed " + Ampify.audio.onFrame((frame) => {
            const loudest = frame.bands.indexOf(Math.max(...frame.bands));
            Ampify.log("frame " + frame.bands.length + " bands, max " + loudest + ", peak " + frame.peak.toFixed(2) +
                    ", rms>0 " + (frame.rms > 0) + ", between 0 and 1 " + frame.bands.every((b) => b >= 0 && b <= 1));
        }));
    )", R"("audio.analyze")");
    CHECK(element->Start());
    CHECK(f.Logged("[frames] subscribed true"));
    f.Play(0.5);
    f.engine.DeliverPending();
    element->Pump();
    CHECK(f.Logged("[frames] frame 16 bands, max 8, peak 0.25, rms>0 true, between 0 and 1 true"));
    element->Stop();
}

TEST_CASE(AudioModule_SlowScriptGetsTheLatestFrameOnly) {
    Fixture f;
    auto element = f.Make("latest", R"(Ampify.audio.onFrame((frame) => Ampify.log("t=" + frame.time.toFixed(2)));)",
                          R"("audio.analyze")");
    CHECK(element->Start());
    f.Play(0.5);
    f.engine.DeliverPending();
    f.Play(0.5, 24000);
    f.engine.DeliverPending();
    element->Pump();
    CHECK(f.Count("[latest] t=") == 1);
    CHECK(f.Logged("[latest] t=1.00"));
    element->Stop();
}

TEST_CASE(AudioModule_AnalyzeGivesTempoLoudnessAndBands) {
    Fixture f;
    auto element = f.Make("analyze", R"(
        const first = Ampify.audio.analyze();
        Ampify.log("before " + first.tempo + " " + first.bands.length + " " + first.frequencies.length);
        setTimeout(() => {
            const result = Ampify.audio.analyze();
            Ampify.log("tempo " + Math.round(result.tempo) + " confidence " + (result.confidence > 0.3) +
                    " loudness " + result.loudness.momentary + "/" + result.loudness.shortTerm +
                    " bands " + result.bands.length);
        }, 10);
    )", R"("audio.analyze")");
    CHECK(element->Start());
    CHECK(f.Logged("[analyze] before null 0 16"));
    f.Play(10);
    f.engine.PublishLoudness({-15.5, -16.25});
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    element->Pump();
    CHECK(f.Logged("[analyze] tempo 120 confidence true loudness -15.5/-16.25 bands 16"));
    element->Stop();
    CHECK(!f.engine.AnalysisWanted());  // the element stopped: nobody needs the analysis now
}

TEST_CASE(AudioModule_LoudnessAndVolumeAreRead) {
    Fixture f;
    f.engine.PublishPlayerVolume(0.42);
    f.engine.PublishLoudness({-9.0, std::nullopt});
    auto element = f.Make("readings", R"(
        const l = Ampify.audio.loudness();
        Ampify.log("loudness " + l.momentary + " " + l.shortTerm + " volume " + Ampify.audio.volume());
    )", R"("audio.analyze", "player.read")");
    CHECK(element->Start());
    CHECK(f.Logged("[readings] loudness -9 null volume 0.42"));
    element->Stop();
}

TEST_CASE(AudioModule_EqualizerBandsAndPresets) {
    Fixture f;
    auto element = f.Make("eq", R"(
        Ampify.log("bands " + Ampify.audio.eq.bands().map((b) => b.frequency + ":" + b.type).join(","));
        Ampify.log("set " + Ampify.audio.eq.setBand(5, 6));
        Ampify.log("1 kHz " + Ampify.audio.eq.bands()[5].gainDb);
        Ampify.log("limit " + Ampify.audio.eq.setBand(0, 40) + " " + Ampify.audio.eq.bands()[0].gainDb);
        try { Ampify.audio.eq.setBand(10, 1); } catch (e) { Ampify.log("out of list " + e.name); }
        try { Ampify.audio.eq.setBand(1.5, 1); } catch (e) { Ampify.log("not whole " + e.name); }
        try { Ampify.audio.eq.preset("unknown"); } catch (e) { Ampify.log("preset " + e.name); }
        Ampify.log("presets " + Ampify.audio.eq.presets().includes("rock"));
        Ampify.log("rock " + Ampify.audio.eq.preset("rock"));
    )", R"("audio.eq")");
    CHECK(element->Start());
    CHECK(f.Logged("[eq] bands 31.25:peaking,62.5:peaking,125:peaking,250:peaking,500:peaking,1000:peaking,"
                   "2000:peaking,4000:peaking,8000:peaking,16000:peaking"));
    CHECK(f.Logged("[eq] set true"));
    CHECK(f.Logged("[eq] 1 kHz 6"));
    CHECK(f.Logged("[eq] limit true 12"));
    CHECK(f.Logged("[eq] out of list RangeError"));
    CHECK(f.Logged("[eq] not whole RangeError"));
    CHECK(f.Logged("[eq] preset RangeError"));
    CHECK(f.Logged("[eq] presets true"));
    CHECK(f.Logged("[eq] rock true"));
    CHECK_NEAR(f.engine.Eq().GainDb(0), ampify::audio::PresetGains("rock")->at(0), 1e-6);
    CHECK(!f.engine.Eq().IsFlat());
    element->Stop();
    CHECK(f.engine.Eq().IsFlat());  // the element that set it stopped: back to a bit-exact passthrough
}

TEST_CASE(AudioModule_PermissionsAreChecked) {
    Fixture f;
    auto element = f.Make("denied", R"(
        for (const [name, call] of [["frames", () => Ampify.audio.onFrame(() => {})], ["analyze", () => Ampify.audio.analyze()],
                                    ["loudness", () => Ampify.audio.loudness()], ["volume", () => Ampify.audio.volume()],
                                    ["eq", () => Ampify.audio.eq.setBand(0, 1)], ["bands", () => Ampify.audio.eq.bands()]]) {
            try { call(); Ampify.log(name + " permis"); } catch (e) { Ampify.log(name + " " + e.name + " " + e.message); }
        }
    )", "");
    CHECK(element->Start());
    CHECK(f.Logged("[denied] frames PermissionDenied missing permission: audio.analyze"));
    CHECK(f.Logged("[denied] analyze PermissionDenied missing permission: audio.analyze"));
    CHECK(f.Logged("[denied] loudness PermissionDenied missing permission: audio.analyze"));
    CHECK(f.Logged("[denied] volume PermissionDenied missing permission: player.read"));
    CHECK(f.Logged("[denied] eq PermissionDenied missing permission: audio.eq"));
    CHECK(f.Logged("[denied] bands PermissionDenied missing permission: audio.eq"));
    CHECK(f.engine.Eq().IsFlat());
    element->Stop();
}

TEST_CASE(AudioModule_WithoutAudioNothingThrows) {
    Fixture f(false);  // the interface process: no AudioHost
    auto element = f.Make("noaudio", R"(
        Ampify.log("subscribed " + Ampify.audio.onFrame(() => {}));
        const result = Ampify.audio.analyze();
        Ampify.log("tempo " + result.tempo + " loudness " + Ampify.audio.loudness().momentary + " volume " + Ampify.audio.volume());
        Ampify.log("eq " + Ampify.audio.eq.setBand(0, 3) + " " + Ampify.audio.eq.bands().length + " " + Ampify.audio.eq.preset("pop"));
    )", R"("audio.analyze", "audio.eq", "player.read")");
    CHECK(element->Start());
    CHECK(f.Logged("[noaudio] subscribed false"));
    CHECK(f.Logged("[noaudio] tempo null loudness null volume null"));
    CHECK(f.Logged("[noaudio] eq false 0 false"));
    CHECK(f.Count("audio analysis is not available") == 1);
    element->Stop();
}

TEST_CASE(AudioModule_ListenerErrorsAreLoggedAndOthersStillRun) {
    Fixture f;
    auto element = f.Make("guarded", R"(
        Ampify.audio.onFrame(() => { throw new Error("failed"); });
        Ampify.audio.onFrame(() => Ampify.log("second called"));
    )", R"("audio.analyze")");
    CHECK(element->Start());
    f.Play(0.1);
    f.engine.DeliverPending();
    element->Pump();
    CHECK(f.Logged("failed"));
    CHECK(f.Logged("[guarded] second called"));
    CHECK(element->running());
    element->Stop();
}

TEST_CASE(ColorModule_ExtractsTwoColours) {
    Fixture f;
    auto element = f.Make("colors", R"(
        const w = 10, h = 4, pixels = new Uint8Array(w * h * 4);
        for (let i = 0; i < w * h; i++) {
            const red = (i % w) < 7;
            pixels.set(red ? [200, 40, 40, 255] : [40, 70, 200, 255], i * 4);
        }
        const c = Ampify.color.extract(pixels, w, h);
        Ampify.log("colors " + c.PROMINENT + " " + c.palette.map((s) => s.color).join(",") + " " +
                ["VIBRANT", "DARK_VIBRANT", "LIGHT_VIBRANT", "DESATURATED", "VIBRANT_NON_ALARMING"].every((k) => /^#[0-9a-f]{6}$/.test(c[k])));
        Ampify.log("array " + Ampify.color.extract(Array.from(pixels), w, h).PROMINENT);
        Ampify.log("buffer " + Ampify.color.extract(pixels.buffer, w, h).PROMINENT);
        try { Ampify.color.extract("https://exemple.fr/artwork.jpg"); } catch (e) { Ampify.log("address " + e.name); }
        try { Ampify.color.extract(pixels, 3, 3); } catch (e) { Ampify.log("size " + e.name); }
    )", "");
    CHECK(element->Start());
    CHECK(f.Logged("[colors] colors #c82828 #c82828,#2846c8 true"));
    CHECK(f.Logged("[colors] array #c82828"));
    CHECK(f.Logged("[colors] buffer #c82828"));
    CHECK(f.Logged("[colors] address TypeError"));
    CHECK(f.Logged("[colors] size RangeError"));
    element->Stop();
}

namespace {

// Decodes any bytes to a 10 x 4 image, 7 columns red and 3 blue, and keeps what it was given.
struct FakeImages : ampify::ImageHost {
    std::string received;
    uint32_t maxSide = 0;

    std::optional<ampify::RgbaImage> DecodeImage(const std::string& bytes, uint32_t side) override {
        received = bytes;
        maxSide = side;
        if (bytes == "not an image") return std::nullopt;
        ampify::RgbaImage image;
        image.width = 10;
        image.height = 4;
        for (uint32_t i = 0; i < 40; i++) {
            bool red = (i % 10) < 7;
            image.pixels.insert(image.pixels.end(), {static_cast<uint8_t>(red ? 200 : 40), static_cast<uint8_t>(red ? 40 : 70),
                                                     static_cast<uint8_t>(red ? 40 : 200), 255});
        }
        return image;
    }
};

}  // namespace

TEST_CASE(ColorModule_FromArtworkDownloadsDecodesAndExtracts) {
    Fixture f(false);
    FakeImages images;
    f.host.images = &images;
    f.host.httpResponse = {200, "jpeg bytes"};
    auto element = f.Make("artwork", R"(
        Ampify.color.fromArtwork("https://is1-ssl.mzstatic.com/image/thumb/Music/a.jpg/1000x1000bb.jpg")
            .then((c) => Ampify.log("colors " + c.PROMINENT + " " + c.palette.length));
        Ampify.color.fromArtwork("https://example.org/a.jpg").catch((e) => Ampify.log("elsewhere " + e.name));
        Ampify.color.fromArtwork("http://is1-ssl.mzstatic.com/a.jpg").catch((e) => Ampify.log("plain " + e.name));
    )", "");
    CHECK(element->Start());
    CHECK(ampify::test::PumpUntil(*element, [&] { return f.Logged("colors "); }));
    CHECK(f.Logged("[artwork] colors #c82828 2"));
    CHECK(f.Logged("[artwork] elsewhere TypeError"));
    CHECK(f.Logged("[artwork] plain TypeError"));
    CHECK(images.received == "jpeg bytes");
    CHECK(images.maxSide == 128);
    CHECK(f.host.RequestCount() == 1);  // the refused addresses were never requested
    element->Stop();
}

TEST_CASE(ColorModule_FromArtworkRejectsWithTheReason) {
    Fixture f(false);
    FakeImages images;
    f.host.httpResponse = {404, "missing"};
    auto element = f.Make("artwork-fail", R"(
        const url = "https://is1-ssl.mzstatic.com/a.jpg";
        Ampify.color.fromArtwork(url).catch((e) => Ampify.log("nodecoder " + e.message));
    )", "");
    CHECK(element->Start());
    CHECK(ampify::test::PumpUntil(*element, [&] { return f.Logged("nodecoder "); }));
    CHECK(f.Logged("[artwork-fail] nodecoder color.fromArtwork: images are not decoded by this host"));

    f.host.images = &images;
    auto missing = f.Make("artwork-404", R"(
        Ampify.color.fromArtwork("https://is1-ssl.mzstatic.com/a.jpg").catch((e) => Ampify.log("got " + e.message));
    )", "");
    CHECK(missing->Start());
    CHECK(ampify::test::PumpUntil(*missing, [&] { return f.Logged("[artwork-404] got "); }));
    CHECK(f.Logged("the artwork could not be downloaded (status 404)"));

    f.host.httpResponse = {200, "not an image"};
    auto broken = f.Make("artwork-broken", R"(
        Ampify.color.fromArtwork("https://is1-ssl.mzstatic.com/a.jpg").catch((e) => Ampify.log("got " + e.message));
    )", "");
    CHECK(broken->Start());
    CHECK(ampify::test::PumpUntil(*broken, [&] { return f.Logged("[artwork-broken] got "); }));
    CHECK(f.Logged("the artwork is not an image Windows can read"));
    element->Stop();
    missing->Stop();
    broken->Stop();
}

namespace {

// The cover of the song playing: whatever the test sets before the script asks for it.
struct FakeArtwork : ampify::ArtworkHost {
    std::optional<File> file;
    std::optional<File> CurrentArtwork() override { return file; }
};

}  // namespace

TEST_CASE(ColorModule_PlayingReadsTheCoverOfTheSongNow) {
    Fixture f(false);
    FakeImages images;
    FakeArtwork artwork;
    f.host.images = &images;

    // Without the player.read permission, the request is refused before anything is read. The refusal arrives as a
    // rejection: the request is made through the dispatcher, so a try block around the call does not see it.
    auto denied = f.Make("playing-denied", R"JS(
        Ampify.color.playing().catch((e) => Ampify.log("denied " + e.name));
    )JS", "");
    CHECK(denied->Start());
    CHECK(ampify::test::PumpUntil(*denied, [&] { return f.Logged("[playing-denied] denied "); }));
    CHECK(f.Logged("[playing-denied] denied PermissionDenied"));

    // A host without the cover service says so.
    auto off = f.Make("playing-off", R"JS(
        Ampify.color.playing().catch((e) => Ampify.log("off " + e.message));
    )JS", "\"player.read\"");
    CHECK(off->Start());
    CHECK(ampify::test::PumpUntil(*off, [&] { return f.Logged("[playing-off] off "); }));
    CHECK(f.Logged("color.playing: covers are not read by this host"));

    f.host.artwork = &artwork;
    auto none = f.Make("playing-none", R"JS(
        Ampify.color.playing().then((c) => Ampify.log("nocover " + (c === null)));
    )JS", "\"player.read\"");
    CHECK(none->Start());
    CHECK(ampify::test::PumpUntil(*none, [&] { return f.Logged("[playing-none] nocover "); }));
    CHECK(f.Logged("[playing-none] nocover true"));

    artwork.file = ampify::ArtworkHost::File{"image/png", "jpeg bytes"};
    auto cover = f.Make("playing-cover", R"JS(
        Ampify.color.playing().then((c) => Ampify.log("cover " + c.PROMINENT + " " + c.palette.length));
    )JS", "\"player.read\"");
    CHECK(cover->Start());
    CHECK(ampify::test::PumpUntil(*cover, [&] { return f.Logged("[playing-cover] cover "); }));
    CHECK(f.Logged("[playing-cover] cover #c82828 2"));
    CHECK(images.received == "jpeg bytes");

    artwork.file = ampify::ArtworkHost::File{"image/png", "not an image"};
    auto broken = f.Make("playing-broken", R"JS(
        Ampify.color.playing().catch((e) => Ampify.log("broken " + e.message));
    )JS", "\"player.read\"");
    CHECK(broken->Start());
    CHECK(ampify::test::PumpUntil(*broken, [&] { return f.Logged("[playing-broken] broken "); }));
    CHECK(f.Logged("color.playing: the cover is not an image Windows can read"));

    denied->Stop();
    off->Stop();
    none->Stop();
    cover->Stop();
    broken->Stop();
}
