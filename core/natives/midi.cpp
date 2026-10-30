// Natives of the module midi (core/prelude/midi.cpp). Permission: midi.input. Setting: midi.enabled.

#include "midi.hpp"

#include "../api/bridge.hpp"
#include "../api/module.hpp"
#include "../json.hpp"
#include "../midi_map.hpp"
#include "service_gate.hpp"

namespace amk::natives {

namespace {

struct MidiNatives : ScriptBridge {
    // native.midiBinding(text): {"type":"any"|"note"|"cc","channel":n,"number":n}, or a RangeError.
    AMK_NATIVE(Binding) {
        ScriptExtension* ext = Owner(ctx);
        JSValue failure = JS_UNDEFINED;
        if (!Require(ctx, ext, "midi.input", &failure)) return failure;
        std::string error;
        std::optional<midi::Binding> binding = midi::ParseBinding(String(ctx, argc, argv, 0).value_or(""), &error);
        if (!binding) return Throw(ctx, "RangeError", error);
        const char* type = binding->type == midi::Binding::Type::Note      ? "note"
                           : binding->type == midi::Binding::Type::Control ? "cc"
                                                                           : "any";
        Json out(Json::Object{});
        out.Set("type", Json(type));
        out.Set("channel", Json(static_cast<double>(binding->channel)));
        out.Set("number", Json(static_cast<double>(binding->number)));
        std::string text = out.Dump();
        return JS_NewStringLen(ctx, text.data(), text.size());
    }

    AMK_NATIVE(Subscribe) {
        ScriptExtension* ext = Owner(ctx);
        JSValue failure = JS_UNDEFINED;
        if (!Require(ctx, ext, "midi.input", &failure)) return failure;
        if (!ServiceGate::Open(ext, "midi.enabled", "MIDI")) return JS_FALSE;
        MidiHost* midiHost = HostOf(ext).Capability<MidiHost>();
        return JS_NewBool(ctx, midiHost && midiHost->SubscribeMidi(ext->id()));
    }

    // native.midiDevices(): JSON array of the input device names.
    AMK_NATIVE(Devices) {
        ScriptExtension* ext = Owner(ctx);
        JSValue failure = JS_UNDEFINED;
        if (!Require(ctx, ext, "midi.input", &failure)) return failure;
        Json::Array names;
        if (MidiHost* midiHost = HostOf(ext).Capability<MidiHost>()) {
            for (const std::string& name : midiHost->MidiDevices()) names.emplace_back(name);
        }
        std::string text = Json(std::move(names)).Dump();
        return JS_NewStringLen(ctx, text.data(), text.size());
    }
};

}  // namespace

void DefineMidi(api::NativeScope& scope) {
    scope.Function("midiBinding", MidiNatives::Binding, 1);
    scope.Function("midiSubscribe", MidiNatives::Subscribe, 0);
    scope.Function("midiDevices", MidiNatives::Devices, 0);
}

void StopMidi(ScriptExtension& extension) {
    if (!extension.manifest().HasPermission("midi.input")) return;
    if (MidiHost* host = ScriptBridge::HostOf(&extension).Capability<MidiHost>()) host->UnsubscribeMidi(extension.id());
}

}  // namespace amk::natives
