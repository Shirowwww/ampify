// Module midi: Ampify.midi, notes and controllers of a MIDI device. Natives: src/core/natives/midi.cpp. The matching below
// follows midi::Matches (src/core/midi_map.cpp); tests/services_tests.cpp checks both against the same events.

namespace ampify::prelude {

extern const char kMidi[];
const char kMidi[] = R"AMPIFYJS(
  const midiHandlers = [];

  const midiMatches = (binding, event) => {
    if (binding.type === "any") return true;
    if (binding.channel && binding.channel !== event.channel) return false;
    if (binding.number >= 0 && binding.number !== event.number) return false;
    return binding.type === "note" ? event.type === "note" : event.type === "cc";
  };

  Ampify.midi = {
    // binding: "note:60", "cc:7", "ch2:note:36", "note:*", "*". fn({ type, channel, number, value }).
    // Returns false when MIDI is off (setting midi.enabled) or no device could be opened.
    on(binding, fn) {
      if (typeof fn !== "function") throw new TypeError("midi.on expects a function");
      const parsed = JSON.parse(native.midiBinding(text(binding)));
      midiHandlers.push({ binding: parsed, fn });
      return native.midiSubscribe();
    },
    devices: () => JSON.parse(native.midiDevices()),
  };

  dispatch.midi = (payload) => {
    if (midiHandlers.length === 0) return;
    const event = JSON.parse(payload);
    for (const handler of midiHandlers) {
      if (midiMatches(handler.binding, event)) runGuarded("midi " + event.type + ":" + event.number, handler.fn, [event]);
    }
  };
)AMPIFYJS";

}  // namespace ampify::prelude
