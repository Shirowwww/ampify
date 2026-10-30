// Natives of the module audioAnalysis (core/prelude/audio_analysis.cpp): analysis frames, tempo, loudness, player
// volume and equalizer, through the host's AudioHost capability (core/audio/audio_host.hpp). Permissions:
// audio.analyze (frames, analyze, loudness), audio.eq (equalizer), player.read (volume).
//
// Without the capability (the interface process, or a host without audio) nothing throws: the frames never come,
// the readings are null and the equalizer answers false. A missing permission throws PermissionDenied as usual.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <string>

#include "../api/bridge.hpp"
#include "../api/module.hpp"
#include "../audio/audio_host.hpp"
#include "../audio/equalizer.hpp"

namespace amk::natives {

namespace {

// Numbers for the JSON handed to the script: six significant digits are plenty, and keep the frames small.
std::string Num(double value) {
    if (!std::isfinite(value)) return "null";
    char text[32];
    std::snprintf(text, sizeof(text), "%.6g", value);
    return text;
}

std::string Num(const std::optional<double>& value) { return value ? Num(*value) : "null"; }

std::string Quoted(const std::string& text) {
    return "\"" + text + "\"";  // only kit-made names reach this: no escaping needed
}

// A band level in dBFS as 0..1: 0 at -80 dB or below, 1 at 0 dB.
double BandValue(float db) { return std::clamp((static_cast<double>(db) + 80.0) / 80.0, 0.0, 1.0); }

std::string BandsJson(const std::vector<float>& bandsDb) {
    std::string out = "[";
    for (size_t i = 0; i < bandsDb.size(); i++) {
        if (i) out += ",";
        out += Num(std::round(BandValue(bandsDb[i]) * 1000.0) / 1000.0);
    }
    return out + "]";
}

std::string FrameJson(const audio::AnalysisFrame& frame) {
    return "{\"bands\":" + BandsJson(frame.bandsDb) + ",\"rms\":" + Num(frame.rms) + ",\"peak\":" + Num(frame.peak) +
           ",\"time\":" + Num(frame.seconds) + "}";
}

std::string LoudnessJson(const LoudnessReading& reading) {
    return "{\"momentary\":" + Num(reading.momentary) + ",\"shortTerm\":" + Num(reading.shortTerm) + "}";
}

// The newest frame for one element, until its script takes it. A frame that arrives before the previous one was
// taken replaces it, so a slow script gets the latest frame instead of a growing queue.
struct FrameMailbox {
    std::mutex lock;
    std::string latest;
    bool pending = false;  // a dispatch.audioFrame call is queued
    bool unavailableNoted = false;
};

struct AudioAnalysisNatives : ScriptBridge {
    static AudioHost* Audio(ScriptExtension* ext) { return HostOf(ext).Capability<AudioHost>(); }

    static void NoteUnavailable(ScriptExtension* ext) {
        FrameMailbox& box = State<FrameMailbox>(ext);
        if (box.unavailableNoted) return;
        box.unavailableNoted = true;
        JournalOf(ext).Write("[" + ext->id() + "] analyse audio indisponible dans ce processus (" +
                             HostOf(ext).Process() + ") : le son n'est traité que dans AMPLibraryAgent.exe");
    }

    static JSValue Text(JSContext* ctx, const std::string& text) { return JS_NewStringLen(ctx, text.data(), text.size()); }

    AMK_NATIVE(Subscribe) {
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
            ext->DeliverEvent("audioFrame", "");
        });
        return JS_TRUE;
    }

    AMK_NATIVE(TakeFrame) {
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

    AMK_NATIVE(Analyze) {
        ScriptExtension* ext = Owner(ctx);
        JSValue failure = JS_UNDEFINED;
        if (!Require(ctx, ext, "audio.analyze", &failure)) return failure;
        AudioHost* audio = Audio(ext);
        AudioAnalysisResult result;
        std::vector<double> frequencies;
        if (audio) {
            result = audio->Analyze(ext->id());
            frequencies = audio->BandFrequencies();
        } else {
            NoteUnavailable(ext);
        }
        std::string json = "{\"tempo\":" + (result.tempo.bpm > 0 ? Num(result.tempo.bpm) : std::string("null")) +
                           ",\"confidence\":" + Num(result.tempo.confidence) +
                           ",\"loudness\":" + LoudnessJson(result.loudness);
        if (result.frame) {
            json += ",\"rms\":" + Num(result.frame->rms) + ",\"peak\":" + Num(result.frame->peak) +
                    ",\"bands\":" + BandsJson(result.frame->bandsDb);
        } else {
            json += ",\"rms\":null,\"peak\":null,\"bands\":[]";
        }
        json += ",\"frequencies\":[";
        for (size_t i = 0; i < frequencies.size(); i++) json += (i ? "," : "") + Num(std::round(frequencies[i]));
        json += "]}";
        return Text(ctx, json);
    }

    AMK_NATIVE(Loudness) {
        ScriptExtension* ext = Owner(ctx);
        JSValue failure = JS_UNDEFINED;
        if (!Require(ctx, ext, "audio.analyze", &failure)) return failure;
        AudioHost* audio = Audio(ext);
        return Text(ctx, LoudnessJson(audio ? audio->Loudness() : LoudnessReading{}));
    }

    AMK_NATIVE(Volume) {
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
                         "bande d'égaliseur inconnue : " + Num(index) + " (0 à " + std::to_string(count - 1) + ")");
        }
        return JS_NewBool(ctx, audio->SetEqBand(ext->id(), static_cast<size_t>(index), gainDb));
    }

    AMK_NATIVE(EqSetBand) {
        ScriptExtension* ext = Owner(ctx);
        JSValue failure = JS_UNDEFINED;
        if (!Require(ctx, ext, "audio.eq", &failure)) return failure;
        AudioHost* audio = Audio(ext);
        if (!audio) {
            NoteUnavailable(ext);
            return JS_FALSE;
        }
        return SetBand(ctx, ext, audio, Number(ctx, argc, argv, 0), Number(ctx, argc, argv, 1));
    }

    AMK_NATIVE(EqPreset) {
        ScriptExtension* ext = Owner(ctx);
        JSValue failure = JS_UNDEFINED;
        if (!Require(ctx, ext, "audio.eq", &failure)) return failure;
        std::string name = String(ctx, argc, argv, 0).value_or("");
        std::optional<std::vector<double>> gains = audio::PresetGains(name);
        if (!gains) {
            std::string names;
            for (const std::string& known : audio::PresetNames()) names += (names.empty() ? "" : ", ") + known;
            return Throw(ctx, "RangeError", "préréglage inconnu : " + name + " (" + names + ")");
        }
        AudioHost* audio = Audio(ext);
        if (!audio) {
            NoteUnavailable(ext);
            return JS_FALSE;
        }
        size_t count = std::min(gains->size(), audio->EqBands().size());
        for (size_t i = 0; i < count; i++) audio->SetEqBand(ext->id(), i, (*gains)[i]);
        return JS_TRUE;
    }

    AMK_NATIVE(EqBands) {
        ScriptExtension* ext = Owner(ctx);
        JSValue failure = JS_UNDEFINED;
        if (!Require(ctx, ext, "audio.eq", &failure)) return failure;
        AudioHost* audio = Audio(ext);
        std::string json = "[";
        if (audio) {
            std::vector<EqBandState> bands = audio->EqBands();
            for (size_t i = 0; i < bands.size(); i++) {
                json += (i ? "," : "");
                json += "{\"frequency\":" + Num(bands[i].frequency) + ",\"gainDb\":" + Num(bands[i].gainDb) +
                        ",\"q\":" + Num(bands[i].q) + ",\"type\":" + Quoted(bands[i].kind) + "}";
            }
        }
        return Text(ctx, json + "]");
    }
};

std::string PresetNamesJson() {
    std::string json = "[";
    for (const std::string& name : audio::PresetNames()) json += (json.size() > 1 ? "," : "") + Quoted(name);
    return json + "]";
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

}  // namespace amk::natives
