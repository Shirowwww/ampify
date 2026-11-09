#pragma once

// MIDI input, without any device: reading the short messages that winmm hands over (midiInOpen packs the status
// byte and two data bytes in one DWORD), and matching them against bindings such as "note:60" or "ch2:cc:7".
// The Windows side (src/win/midi.*) only reads the device. Bindings come from two places: the setting midi.map, read by
// the kit (note:60=next;note:62=previous;cc:7=gain), and Ampify.midi.on(binding, fn) in an element.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ampify::midi {

enum class Kind { NoteOn, NoteOff, ControlChange };

struct Event {
    Kind kind = Kind::NoteOn;
    int channel = 1;  // 1 to 16
    int number = 0;   // note or controller, 0 to 127
    int value = 0;    // velocity or controller value, 0 to 127
};

// A message from midiInProc (MIM_DATA): status in the low byte, then data 1, then data 2. Only notes and
// controllers are kept; a note on with velocity 0 is a note off, as the MIDI standard says.
std::optional<Event> Decode(uint32_t packed);

uint32_t Pack(uint8_t status, uint8_t data1, uint8_t data2);

// {"type":"note"|"noteoff"|"cc","channel":1,"number":60,"value":100}, as the elements receive it.
std::string EventJson(const Event& event);

// What a binding listens to. "note:60" (a key pressed), "cc:7" (a knob or a fader), with an optional channel prefix
// "ch2:". channel 0 means any channel; number -1 means any note or controller ("note:*", "cc:*"); "*" matches all.
struct Binding {
    enum class Type { Any, Note, Control } type = Type::Any;
    int channel = 0;
    int number = -1;
};

// On error returns nullopt and fills `error` in English.
std::optional<Binding> ParseBinding(std::string_view text, std::string* error = nullptr);

// The canonical text, such as "ch2:note:60".
std::string BindingText(const Binding& binding);

// True when the event is a note on (for a note binding) or a controller message (for a cc binding) that the
// binding covers. Note offs only match "*".
bool Matches(const Binding& binding, const Event& event);

// Actions of the kit, for the setting midi.map.
enum class Action { Play, Pause, Next, Previous, Gain };

struct Rule {
    Binding binding;
    Action action = Action::Next;
};

// What a rule asks for when an event arrives: a player command, or the internal gain for a fader.
struct Decision {
    Action action = Action::Next;
    double gainDb = 0;  // for Action::Gain: value 0 -> -30 dB, 127 -> 0 dB
};

class Map {
public:
    // Reads "note:60=next;note:62=previous;cc:7=gain" (separators ';' or ','). Actions: play, pause, next,
    // previous, gain (gain needs a cc binding). On error, keeps nothing and fills `error` in English.
    bool Parse(std::string_view text, std::string* error = nullptr);

    // The first rule that matches. A note rule fires on note on. A cc rule with a command fires when the value goes
    // from below 64 to 64 or above (a button that sends 127 then 0 fires once); a gain rule fires on every value.
    std::optional<Decision> Handle(const Event& event);

    const std::vector<Rule>& rules() const { return m_rules; }

private:
    std::vector<Rule> m_rules;
    int m_lastValue[16][128] = {};
};

}  // namespace ampify::midi
