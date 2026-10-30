// Tests of AMK.audio's analysis, equalizer, loudness and volume, and of AMK.color, as an extension sees them: a
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

#include "../core/audio/audio_engine.hpp"
#include "../core/manifest.hpp"
#include "../core/script_extension.hpp"
#include "fake_host.hpp"
#include "test.hpp"

namespace {

namespace fs = std::filesystem;

struct Fixture {
    amk::test::FakeHost host;
    amk::audio::AudioEngine engine;
    std::ostringstream out;
    amk::Journal journal{out, [] { return std::string("t"); }};

    explicit Fixture(bool withAudio = true) {
        host.process = "engine";  // the audio is rendered in AMPLibraryAgent.exe
        if (withAudio) host.audio = &engine;
    }

    std::unique_ptr<amk::ScriptExtension> Make(const std::string& id, const std::string& main,
                                               const std::string& permissions) {
        fs::path dir = fs::temp_directory_path() / "amk-tests" / ("audio-" + id);
        fs::remove_all(dir);
        fs::create_directories(dir);
        const std::string manifest =
            R"({"id":")" + id + R"(","name":"Démo","version":"1.0.0","kind":"extension","author":"Shirowwww",)" +
            R"("description":"Test.","kitApi":"1.0","main":"main.js","permissions":[)" + permissions + "]}";
        std::ofstream(dir / "manifest.json") << manifest;
        std::ofstream(dir / "main.js") << main;
        amk::ManifestResult result = amk::ParseManifest(manifest, id, 1, [](const std::string&) { return true; });
        return std::make_unique<amk::ScriptExtension>(*result.manifest, dir, host, journal);
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
        AMK.log("abonné " + AMK.audio.onFrame((frame) => {
            const loudest = frame.bands.indexOf(Math.max(...frame.bands));
            AMK.log("image " + frame.bands.length + " bandes, max " + loudest + ", crête " + frame.peak.toFixed(2) +
                    ", rms>0 " + (frame.rms > 0) + ", entre 0 et 1 " + frame.bands.every((b) => b >= 0 && b <= 1));
        }));
    )", R"("audio.analyze")");
    CHECK(element->Start());
    CHECK(f.Logged("[frames] abonné true"));
    f.Play(0.5);
    f.engine.DeliverPending();
    element->Pump();
    CHECK(f.Logged("[frames] image 16 bandes, max 8, crête 0.25, rms>0 true, entre 0 et 1 true"));
    element->Stop();
}

TEST_CASE(AudioModule_SlowScriptGetsTheLatestFrameOnly) {
    Fixture f;
    auto element = f.Make("latest", R"(AMK.audio.onFrame((frame) => AMK.log("t=" + frame.time.toFixed(2)));)",
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
        const first = AMK.audio.analyze();
        AMK.log("avant " + first.tempo + " " + first.bands.length + " " + first.frequencies.length);
        setTimeout(() => {
            const result = AMK.audio.analyze();
            AMK.log("tempo " + Math.round(result.tempo) + " confiance " + (result.confidence > 0.3) +
                    " loudness " + result.loudness.momentary + "/" + result.loudness.shortTerm +
                    " bandes " + result.bands.length);
        }, 10);
    )", R"("audio.analyze")");
    CHECK(element->Start());
    CHECK(f.Logged("[analyze] avant null 0 16"));
    f.Play(10);
    f.engine.PublishLoudness({-15.5, -16.25});
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    element->Pump();
    CHECK(f.Logged("[analyze] tempo 120 confiance true loudness -15.5/-16.25 bandes 16"));
    element->Stop();
    CHECK(!f.engine.AnalysisWanted());  // the element stopped: nobody needs the analysis now
}

TEST_CASE(AudioModule_LoudnessAndVolumeAreRead) {
    Fixture f;
    f.engine.PublishPlayerVolume(0.42);
    f.engine.PublishLoudness({-9.0, std::nullopt});
    auto element = f.Make("readings", R"(
        const l = AMK.audio.loudness();
        AMK.log("loudness " + l.momentary + " " + l.shortTerm + " volume " + AMK.audio.volume());
    )", R"("audio.analyze", "player.read")");
    CHECK(element->Start());
    CHECK(f.Logged("[readings] loudness -9 null volume 0.42"));
    element->Stop();
}

TEST_CASE(AudioModule_EqualizerBandsAndPresets) {
    Fixture f;
    auto element = f.Make("eq", R"(
        AMK.log("bandes " + AMK.audio.eq.bands().map((b) => b.frequency + ":" + b.type).join(","));
        AMK.log("réglée " + AMK.audio.eq.setBand(5, 6));
        AMK.log("1 kHz " + AMK.audio.eq.bands()[5].gainDb);
        AMK.log("borne " + AMK.audio.eq.setBand(0, 40) + " " + AMK.audio.eq.bands()[0].gainDb);
        try { AMK.audio.eq.setBand(10, 1); } catch (e) { AMK.log("hors liste " + e.name); }
        try { AMK.audio.eq.setBand(1.5, 1); } catch (e) { AMK.log("pas entier " + e.name); }
        try { AMK.audio.eq.preset("inconnu"); } catch (e) { AMK.log("préréglage " + e.name); }
        AMK.log("préréglages " + AMK.audio.eq.presets().includes("rock"));
        AMK.log("rock " + AMK.audio.eq.preset("rock"));
    )", R"("audio.eq")");
    CHECK(element->Start());
    CHECK(f.Logged("[eq] bandes 31.25:peaking,62.5:peaking,125:peaking,250:peaking,500:peaking,1000:peaking,"
                   "2000:peaking,4000:peaking,8000:peaking,16000:peaking"));
    CHECK(f.Logged("[eq] réglée true"));
    CHECK(f.Logged("[eq] 1 kHz 6"));
    CHECK(f.Logged("[eq] borne true 12"));
    CHECK(f.Logged("[eq] hors liste RangeError"));
    CHECK(f.Logged("[eq] pas entier RangeError"));
    CHECK(f.Logged("[eq] préréglage RangeError"));
    CHECK(f.Logged("[eq] préréglages true"));
    CHECK(f.Logged("[eq] rock true"));
    CHECK_NEAR(f.engine.Eq().GainDb(0), amk::audio::PresetGains("rock")->at(0), 1e-6);
    CHECK(!f.engine.Eq().IsFlat());
    element->Stop();
    CHECK(f.engine.Eq().IsFlat());  // the element that set it stopped: back to a bit-exact passthrough
}

TEST_CASE(AudioModule_PermissionsAreChecked) {
    Fixture f;
    auto element = f.Make("denied", R"(
        for (const [name, call] of [["frames", () => AMK.audio.onFrame(() => {})], ["analyze", () => AMK.audio.analyze()],
                                    ["loudness", () => AMK.audio.loudness()], ["volume", () => AMK.audio.volume()],
                                    ["eq", () => AMK.audio.eq.setBand(0, 1)], ["bands", () => AMK.audio.eq.bands()]]) {
            try { call(); AMK.log(name + " permis"); } catch (e) { AMK.log(name + " " + e.name + " " + e.message); }
        }
    )", "");
    CHECK(element->Start());
    CHECK(f.Logged("[denied] frames PermissionDenied permission manquante : audio.analyze"));
    CHECK(f.Logged("[denied] analyze PermissionDenied permission manquante : audio.analyze"));
    CHECK(f.Logged("[denied] loudness PermissionDenied permission manquante : audio.analyze"));
    CHECK(f.Logged("[denied] volume PermissionDenied permission manquante : player.read"));
    CHECK(f.Logged("[denied] eq PermissionDenied permission manquante : audio.eq"));
    CHECK(f.Logged("[denied] bands PermissionDenied permission manquante : audio.eq"));
    CHECK(f.engine.Eq().IsFlat());
    element->Stop();
}

TEST_CASE(AudioModule_WithoutAudioNothingThrows) {
    Fixture f(false);  // the interface process: no AudioHost
    auto element = f.Make("noaudio", R"(
        AMK.log("abonné " + AMK.audio.onFrame(() => {}));
        const result = AMK.audio.analyze();
        AMK.log("tempo " + result.tempo + " loudness " + AMK.audio.loudness().momentary + " volume " + AMK.audio.volume());
        AMK.log("eq " + AMK.audio.eq.setBand(0, 3) + " " + AMK.audio.eq.bands().length + " " + AMK.audio.eq.preset("pop"));
    )", R"("audio.analyze", "audio.eq", "player.read")");
    CHECK(element->Start());
    CHECK(f.Logged("[noaudio] abonné false"));
    CHECK(f.Logged("[noaudio] tempo null loudness null volume null"));
    CHECK(f.Logged("[noaudio] eq false 0 false"));
    CHECK(f.Count("analyse audio indisponible") == 1);
    element->Stop();
}

TEST_CASE(AudioModule_ListenerErrorsAreLoggedAndOthersStillRun) {
    Fixture f;
    auto element = f.Make("guarded", R"(
        AMK.audio.onFrame(() => { throw new Error("raté"); });
        AMK.audio.onFrame(() => AMK.log("second appelé"));
    )", R"("audio.analyze")");
    CHECK(element->Start());
    f.Play(0.1);
    f.engine.DeliverPending();
    element->Pump();
    CHECK(f.Logged("raté"));
    CHECK(f.Logged("[guarded] second appelé"));
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
        const c = AMK.color.extract(pixels, w, h);
        AMK.log("couleurs " + c.PROMINENT + " " + c.palette.map((s) => s.color).join(",") + " " +
                ["VIBRANT", "DARK_VIBRANT", "LIGHT_VIBRANT", "DESATURATED", "VIBRANT_NON_ALARMING"].every((k) => /^#[0-9a-f]{6}$/.test(c[k])));
        AMK.log("tableau " + AMK.color.extract(Array.from(pixels), w, h).PROMINENT);
        AMK.log("buffer " + AMK.color.extract(pixels.buffer, w, h).PROMINENT);
        try { AMK.color.extract("https://exemple.fr/pochette.jpg"); } catch (e) { AMK.log("adresse " + e.name); }
        try { AMK.color.extract(pixels, 3, 3); } catch (e) { AMK.log("taille " + e.name); }
    )", "");
    CHECK(element->Start());
    CHECK(f.Logged("[colors] couleurs #c82828 #c82828,#2846c8 true"));
    CHECK(f.Logged("[colors] tableau #c82828"));
    CHECK(f.Logged("[colors] buffer #c82828"));
    CHECK(f.Logged("[colors] adresse TypeError"));
    CHECK(f.Logged("[colors] taille RangeError"));
    element->Stop();
}
