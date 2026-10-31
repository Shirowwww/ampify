// MIDI input without a device: decoding the winmm messages, bindings, the kit's midi.map (core/midi_map.*), and
// Ampify.midi, whose JavaScript matcher must agree with midi::Matches.

#include <string>
#include <vector>

#include "../core/midi_map.hpp"
#include "service_host.hpp"
#include "test.hpp"

namespace {

using ampify::midi::Kind;

ampify::midi::Event Note(int number, int velocity = 100, int channel = 1) {
    return *ampify::midi::Decode(ampify::midi::Pack(static_cast<uint8_t>(0x90 | (channel - 1)), static_cast<uint8_t>(number),
                                              static_cast<uint8_t>(velocity)));
}

ampify::midi::Event Control(int number, int value, int channel = 1) {
    return *ampify::midi::Decode(ampify::midi::Pack(static_cast<uint8_t>(0xB0 | (channel - 1)), static_cast<uint8_t>(number),
                                              static_cast<uint8_t>(value)));
}

}  // namespace

TEST_CASE(Midi_DecodesNotesAndControllers) {
    auto on = ampify::midi::Decode(ampify::midi::Pack(0x92, 60, 100));
    CHECK(on && on->kind == Kind::NoteOn && on->channel == 3 && on->number == 60 && on->value == 100);
    auto silent = ampify::midi::Decode(ampify::midi::Pack(0x90, 60, 0));
    CHECK(silent && silent->kind == Kind::NoteOff);  // velocity 0 is a note off
    auto off = ampify::midi::Decode(ampify::midi::Pack(0x80, 61, 40));
    CHECK(off && off->kind == Kind::NoteOff && off->number == 61);
    auto cc = ampify::midi::Decode(ampify::midi::Pack(0xBF, 7, 127));
    CHECK(cc && cc->kind == Kind::ControlChange && cc->channel == 16 && cc->number == 7 && cc->value == 127);
    CHECK(!ampify::midi::Decode(ampify::midi::Pack(0xE0, 0, 64)));  // pitch bend
    CHECK(!ampify::midi::Decode(0xF8));                           // clock
    CHECK(ampify::midi::EventJson(*on) == R"({"type":"note","channel":3,"number":60,"value":100})");
    CHECK(ampify::midi::EventJson(*cc) == R"({"type":"cc","channel":16,"number":7,"value":127})");
}

TEST_CASE(Midi_ParsesBindings) {
    auto check = [](const char* text, const char* canonical) {
        auto binding = ampify::midi::ParseBinding(text);
        CHECK(binding.has_value());
        if (binding) CHECK(ampify::midi::BindingText(*binding) == canonical);
    };
    check("note:60", "note:60");
    check(" CC : 7 ", "cc:7");
    check("ch2:note:36", "ch2:note:36");
    check("note:*", "note:*");
    check("*", "*");
    std::string error;
    CHECK(!ampify::midi::ParseBinding("note:128", &error) && !error.empty());
    CHECK(!ampify::midi::ParseBinding("ch17:cc:1"));
    CHECK(!ampify::midi::ParseBinding("pad:3"));
    CHECK(!ampify::midi::ParseBinding("note:"));
}

TEST_CASE(Midi_MatchesBindingsAgainstEvents) {
    auto binding = [](const char* text) { return *ampify::midi::ParseBinding(text); };
    CHECK(ampify::midi::Matches(binding("note:60"), Note(60)));
    CHECK(!ampify::midi::Matches(binding("note:60"), Note(61)));
    CHECK(!ampify::midi::Matches(binding("note:60"), Note(60, 0)));  // note off
    CHECK(ampify::midi::Matches(binding("ch2:note:*"), Note(10, 90, 2)));
    CHECK(!ampify::midi::Matches(binding("ch2:note:*"), Note(10, 90, 3)));
    CHECK(ampify::midi::Matches(binding("cc:7"), Control(7, 0)));
    CHECK(!ampify::midi::Matches(binding("cc:7"), Note(7)));
    CHECK(ampify::midi::Matches(binding("*"), Note(60, 0)));
}

TEST_CASE(Midi_MapRunsKitActions) {
    ampify::midi::Map map;
    std::string error;
    CHECK(map.Parse("note:60=next; note:62=previous, cc:20=play; cc:7=gain", &error));
    CHECK(map.rules().size() == 4);

    auto next = map.Handle(Note(60));
    CHECK(next && next->action == ampify::midi::Action::Next);
    CHECK(!map.Handle(Note(60, 0)));  // releasing the key does nothing
    CHECK(map.Handle(Note(62))->action == ampify::midi::Action::Previous);

    // A button sending a controller: 127 then 0 fires once; a second press fires again.
    CHECK(map.Handle(Control(20, 127)).has_value());
    CHECK(!map.Handle(Control(20, 127)).has_value());
    CHECK(!map.Handle(Control(20, 0)).has_value());
    CHECK(map.Handle(Control(20, 100)).has_value());

    auto quiet = map.Handle(Control(7, 0));
    CHECK(quiet && quiet->action == ampify::midi::Action::Gain);
    CHECK_NEAR(quiet->gainDb, -30.0, 1e-9);
    CHECK_NEAR(map.Handle(Control(7, 127))->gainDb, 0.0, 1e-9);

    CHECK(!map.Parse("note:60", &error));            // no action
    CHECK(!map.Parse("note:60=volume", &error));     // unknown action
    CHECK(!map.Parse("note:60=gain", &error));       // gain needs a controller
    CHECK(!map.Parse("*=next", &error));
    CHECK(map.rules().size() == 4);                  // a refused text keeps the previous rules
    CHECK(map.Parse("", &error) && map.rules().empty());
}

TEST_CASE(MidiModule_NeedsPermissionAndSettingThenDeliversMatchingEvents) {
    ampify::test::ServiceFixture<> f;
    auto without = f.Make("midi-none", R"(try { Ampify.midi.on("note:60", () => {}); } catch (e) { Ampify.log("refused " + e.name); })");
    CHECK(without->Start());
    CHECK(f.Logged("refused PermissionDenied"));

    auto off = f.Make("midi-off", R"(Ampify.log("open " + Ampify.midi.on("note:60", () => {}));)", R"("midi.input")");
    CHECK(off->Start());
    CHECK(f.Logged("open false"));
    CHECK(f.Logged("MIDI: off (setting midi.enabled is 0)"));

    f.host.settings["midi.enabled"] = "1";
    auto on = f.Make("midi-on", R"(
        Ampify.log("open " + Ampify.midi.on("note:60", (e) => Ampify.log("note " + e.number + " " + e.value)));
        Ampify.midi.on("cc:*", (e) => Ampify.log("cc " + e.number + "=" + e.value));
        try { Ampify.midi.on("pad:1", () => {}); } catch (e) { Ampify.log("binding " + e.name); }
        Ampify.log("devices " + Ampify.midi.devices().join(","));
    )", R"("midi.input")");
    CHECK(on->Start());
    CHECK(f.Logged("open true"));
    CHECK(f.Logged("binding RangeError"));
    CHECK(f.Logged("devices Test keyboard"));
    CHECK(!f.host.midiSubscribers.empty() && f.host.midiSubscribers[0] == "midi-on");

    on->DeliverEvent("midi", ampify::midi::EventJson(Note(60, 99)));
    on->DeliverEvent("midi", ampify::midi::EventJson(Note(61, 99)));
    on->DeliverEvent("midi", ampify::midi::EventJson(Control(7, 64)));
    on->Pump();
    CHECK(f.Logged("note 60 99"));
    CHECK(!f.Logged("note 61"));
    CHECK(f.Logged("cc 7=64"));

    on->Stop();
    CHECK(!f.host.midiUnsubscribed.empty() && f.host.midiUnsubscribed[0] == "midi-on");
}

TEST_CASE(MidiModule_JavaScriptMatcherAgreesWithTheKernel) {
    const std::vector<std::string> bindings = {"*", "note:60", "note:*", "ch2:note:60", "cc:7", "ch3:cc:*", "cc:*"};
    const std::vector<ampify::midi::Event> events = {Note(60), Note(60, 0), Note(61, 80, 2), Note(60, 80, 2),
                                                  Control(7, 10), Control(7, 10, 3), Control(8, 0, 3)};
    std::string script;
    for (size_t b = 0; b < bindings.size(); b++) {
        script += "Ampify.midi.on(\"" + bindings[b] + "\", (e) => Ampify.log(\"hit " + std::to_string(b) +
                  " \" + e.type + e.channel + \":\" + e.number + \":\" + e.value));\n";
    }
    ampify::test::ServiceFixture<> f;
    f.host.settings["midi.enabled"] = "1";
    auto element = f.Make("midi-agree", script, R"("midi.input")");
    CHECK(element->Start());
    for (const ampify::midi::Event& event : events) element->DeliverEvent("midi", ampify::midi::EventJson(event));
    element->Pump();

    for (size_t b = 0; b < bindings.size(); b++) {
        ampify::midi::Binding binding = *ampify::midi::ParseBinding(bindings[b]);
        for (const ampify::midi::Event& event : events) {
            const char* type = event.kind == Kind::NoteOn ? "note" : event.kind == Kind::NoteOff ? "noteoff" : "cc";
            std::string line = "hit " + std::to_string(b) + " " + type + std::to_string(event.channel) + ":" +
                               std::to_string(event.number) + ":" + std::to_string(event.value);
            if (f.Logged(line) != ampify::midi::Matches(binding, event)) {
                ampify::test::ReportFailure(__FILE__, __LINE__, "disagreement: " + bindings[b] + " / " + line);
            }
        }
    }
}
