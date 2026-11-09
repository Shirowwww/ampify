// Natives of the module audioAnalysis (src/core/prelude/audio_analysis.cpp): analysis frames, tempo, loudness, player
// volume and equalizer, through the host's AudioHost capability (src/core/audio/audio_host.hpp). Permissions:
// audio.analyze (frames, analyze, loudness), audio.eq (equalizer), player.read (volume).
//
// Without the capability (a host without audio) nothing throws: the frames never come, the readings are null and the
// equalizer answers false, as it does where the capability is read only (the interface process). A missing
// permission throws PermissionDenied as usual.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>

#include "../api/bridge.hpp"
#include "../api/dispatch_names.hpp"
#include "../api/module.hpp"
#include "../apple_music.hpp"
#include "../audio/audio_host.hpp"
#include "../audio/equalizer.hpp"
#include "../json.hpp"

namespace ampify::natives {

namespace {

// Numbers handed to the script: six significant digits are plenty, and keep the frames small. Not finite: null.
Json Num(double value) {
    if (!std::isfinite(value)) return Json();
    char text[32];
    std::snprintf(text, sizeof(text), "%.6g", value);
    return Json(std::strtod(text, nullptr));
}

Json Num(const std::optional<double>& value) { return value ? Num(*value) : Json(); }

// A band level in dBFS as 0..1: 0 at -80 dB or below, 1 at 0 dB.
double BandValue(float db) { return std::clamp((static_cast<double>(db) + 80.0) / 80.0, 0.0, 1.0); }

Json BandsJson(const std::vector<float>& bandsDb) {
    Json::Array bands;
    for (float db : bandsDb) bands.push_back(Num(std::round(BandValue(db) * 1000.0) / 1000.0));
    return Json(std::move(bands));
}

std::string FrameJson(const audio::AnalysisFrame& frame) {
    Json::Object members;
    members.emplace_back("bands", BandsJson(frame.bandsDb));
    members.emplace_back("rms", Num(frame.rms));
    members.emplace_back("peak", Num(frame.peak));
    members.emplace_back("time", Num(frame.seconds));
    return Json(std::move(members)).Dump();
}

Json LoudnessJson(const LoudnessReading& reading) {
    return Json(Json::Object{{"momentary", Num(reading.momentary)}, {"shortTerm", Num(reading.shortTerm)}});
}

std::string AnalysisJson(const AudioAnalysisResult& result) {
    Json::Object members;
    members.emplace_back("tempo", result.tempo.bpm > 0 ? Num(result.tempo.bpm) : Json());
    members.emplace_back("confidence", Num(result.tempo.confidence));
    members.emplace_back("loudness", LoudnessJson(result.loudness));
    members.emplace_back("rms", result.frame ? Num(result.frame->rms) : Json());
    members.emplace_back("peak", result.frame ? Num(result.frame->peak) : Json());
    members.emplace_back("bands", result.frame ? BandsJson(result.frame->bandsDb) : Json(Json::Array{}));
    Json::Array frequencies;
    for (double hz : result.bandFrequencies) frequencies.push_back(Num(std::round(hz)));
    members.emplace_back("frequencies", Json(std::move(frequencies)));
    return Json(std::move(members)).Dump();
}

std::string EqBandsJson(const std::vector<EqBandState>& bands) {
    Json::Array out;
    for (const EqBandState& band : bands) {
        out.push_back(Json(Json::Object{{"frequency", Num(band.frequency)},
                                        {"gainDb", Num(band.gainDb)},
                                        {"q", Num(band.q)},
                                        {"type", Json(band.kind)}}));
    }
    return Json(std::move(out)).Dump();
}

// The newest frame for one element, until its script takes it. A frame that arrives before the previous one was
// taken replaces it, so a slow script gets the latest frame instead of a growing queue.
struct FrameMailbox {
    std::mutex lock;
    std::string latest;
    bool pending = false;  // a dispatch.audioFrame call is queued
};

// What the element was told once in the log, so a script that keeps asking does not fill it.
struct AudioNotes {
    bool unavailable = false;
    bool equalizerElsewhere = false;
};

struct AudioAnalysisNatives : ScriptBridge {
    static AudioHost* Audio(ScriptExtension* ext) { return HostOf(ext).Capability<AudioHost>(); }

    static void NoteUnavailable(ScriptExtension* ext) {
        AudioNotes& notes = State<AudioNotes>(ext);
        if (notes.unavailable) return;
        notes.unavailable = true;
        JournalOf(ext).Write("[" + ext->id() + "] audio analysis is not available in this process (" +
                             HostOf(ext).Process() + "): sound is only processed in " + std::string(kLibraryAgentImage));
    }

    // The host when the equalizer runs in this process; otherwise null, said once in the log.
    static AudioHost* Equalizer(ScriptExtension* ext) {
        AudioHost* audio = Audio(ext);
        if (!audio) {
            NoteUnavailable(ext);
            return nullptr;
        }
        if (audio->Reach() == AudioReach::Full) return audio;
        AudioNotes& notes = State<AudioNotes>(ext);
        if (!notes.equalizerElsewhere) {
            notes.equalizerElsewhere = true;
            JournalOf(ext).Write("[" + ext->id() + "] the equalizer is only applied in " +
                                 std::string(kLibraryAgentImage) + ", not in this process (" + HostOf(ext).Process() +
                                 ")");
        }
        return nullptr;
    }

    static JSValue Text(JSContext* ctx, const std::string& text) { return JS_NewStringLen(ctx, text.data(), text.size()); }

    AMPIFY_NATIVE(Subscribe) {
        ScriptExtension* ext = Owner(ctx);
        JSValue failure = JS_UNDEFINED;
        if (!Require(ctx, ext, "audio.analyze", &failure)) return failure;
        AudioHost* audio = Audio(ext);
        if (!audio) {
            NoteUnavailable(ext);
            return JS_FALSE;
        }
        // The mailbox lives as long as the runtime; the stop hook releases the sink before the runtime goes.
        FrameMailbox* box = &State<FrameMailbox>(ext);
        audio->SubscribeFrames(ext->id(), [ext, box](const audio::AnalysisFrame& frame) {
            std::string json = FrameJson(frame);
            std::lock_guard<std::mutex> guard(box->lock);
            box->latest = std::move(json);
            if (box->pending) return;
            box->pending = true;
            ext->DeliverEvent(dispatch::kAudioFrame, "");
        });
        return JS_TRUE;
    }

    AMPIFY_NATIVE(TakeFrame) {
        ScriptExtension* ext = Owner(ctx);
        FrameMailbox& box = State<FrameMailbox>(ext);
        std::string json;
        {
            std::lock_guard<std::mutex> guard(box.lock);
            json.swap(box.latest);
            box.pending = false;
        }
        if (json.empty()) return JS_NULL;
        return Text(ctx, json);
    }

    AMPIFY_NATIVE(Analyze) {
        ScriptExtension* ext = Owner(ctx);
        JSValue failure = JS_UNDEFINED;
        if (!Require(ctx, ext, "audio.analyze", &failure)) return failure;
        AudioHost* audio = Audio(ext);
        AudioAnalysisResult result;
        if (audio) {
            result = audio->Analyze(ext->id());
        } else {
            NoteUnavailable(ext);
        }
        return Text(ctx, AnalysisJson(result));
    }

    AMPIFY_NATIVE(Loudness) {
        ScriptExtension* ext = Owner(ctx);
        JSValue failure = JS_UNDEFINED;
        if (!Require(ctx, ext, "audio.analyze", &failure)) return failure;
        AudioHost* audio = Audio(ext);
        return Text(ctx, LoudnessJson(audio ? audio->Loudness() : LoudnessReading{}).Dump());
    }

    AMPIFY_NATIVE(Volume) {
        ScriptExtension* ext = Owner(ctx);
        JSValue failure = JS_UNDEFINED;
        if (!Require(ctx, ext, "player.read", &failure)) return failure;
        AudioHost* audio = Audio(ext);
        std::optional<double> volume = audio ? audio->PlayerVolume() : std::nullopt;
        if (!volume) return JS_NULL;
        return JS_NewFloat64(ctx, *volume);
    }

    static JSValue SetBand(JSContext* ctx, ScriptExtension* ext, AudioHost* audio, double index, double gainDb) {
        size_t count = audio->EqBands().size();
        if (count == 0) return JS_FALSE;
        if (!(index >= 0) || index >= static_cast<double>(count) || index != std::floor(index)) {
            return Throw(ctx, "RangeError",
                         "unknown equalizer band: " + Num(index).Dump() + " (0 to " + std::to_string(count - 1) + ")");
        }
        return JS_NewBool(ctx, audio->SetEqBand(ext->id(), static_cast<size_t>(index), gainDb));
    }

    AMPIFY_NATIVE(EqSetBand) {
        ScriptExtension* ext = Owner(ctx);
        JSValue failure = JS_UNDEFINED;
        if (!Require(ctx, ext, "audio.eq", &failure)) return failure;
        AudioHost* audio = Equalizer(ext);
        if (!audio) return JS_FALSE;
        return SetBand(ctx, ext, audio, Number(ctx, argc, argv, 0), Number(ctx, argc, argv, 1));
    }

    AMPIFY_NATIVE(EqPreset) {
        ScriptExtension* ext = Owner(ctx);
        JSValue failure = JS_UNDEFINED;
        if (!Require(ctx, ext, "audio.eq", &failure)) return failure;
        std::string name = String(ctx, argc, argv, 0).value_or("");
        std::optional<std::vector<double>> gains = audio::PresetGains(name);
        if (!gains) {
            std::string names;
            for (const std::string& known : audio::PresetNames()) names += (names.empty() ? "" : ", ") + known;
            return Throw(ctx, "RangeError", "unknown preset: " + name + " (" + names + ")");
        }
        AudioHost* audio = Equalizer(ext);
        if (!audio) return JS_FALSE;
        size_t count = std::min(gains->size(), audio->EqBands().size());
        for (size_t i = 0; i < count; i++) audio->SetEqBand(ext->id(), i, (*gains)[i]);
        return JS_TRUE;
    }

    AMPIFY_NATIVE(EqBands) {
        ScriptExtension* ext = Owner(ctx);
        JSValue failure = JS_UNDEFINED;
        if (!Require(ctx, ext, "audio.eq", &failure)) return failure;
        AudioHost* audio = Audio(ext);
        return Text(ctx, EqBandsJson(audio ? audio->EqBands() : std::vector<EqBandState>{}));
    }
};

std::string PresetNamesJson() {
    Json::Array names;
    for (const std::string& name : audio::PresetNames()) names.emplace_back(name);
    return Json(std::move(names)).Dump();
}

}  // namespace

void DefineAudioAnalysis(api::NativeScope& scope) {
    scope.Function("audioSubscribe", AudioAnalysisNatives::Subscribe, 0);
    scope.Function("audioTakeFrame", AudioAnalysisNatives::TakeFrame, 0);
    scope.Function("audioAnalyze", AudioAnalysisNatives::Analyze, 0);
    scope.Function("audioLoudness", AudioAnalysisNatives::Loudness, 0);
    scope.Function("audioVolume", AudioAnalysisNatives::Volume, 0);
    scope.Function("eqSetBand", AudioAnalysisNatives::EqSetBand, 2);
    scope.Function("eqPreset", AudioAnalysisNatives::EqPreset, 1);
    scope.Function("eqBands", AudioAnalysisNatives::EqBands, 0);
    scope.String("eqPresetNames", PresetNamesJson());
}

// The element stops: its frames end and an equalizer it set goes back to flat.
void StopAudioAnalysis(ScriptExtension& extension) {
    if (AudioHost* audio = ScriptBridge::HostOf(&extension).Capability<AudioHost>()) audio->Release(extension.id());
}

}  // namespace ampify::natives
