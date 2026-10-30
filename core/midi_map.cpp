#include "midi_map.hpp"

#include <algorithm>
#include <cctype>

namespace amk::midi {

namespace {

bool Fail(std::string* error, const std::string& message) {
    if (error) *error = message;
    return false;
}

std::string Lower(std::string_view text) {
    std::string out;
    for (char c : text) {
        if (c != ' ' && c != '\t') out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

// A decimal number from `low` to `high`, or -1.
int Number(std::string_view text, int low, int high) {
    if (text.empty() || text.size() > 3) return -1;
    int value = 0;
    for (char c : text) {
        if (c < '0' || c > '9') return -1;
        value = value * 10 + (c - '0');
    }
    return value >= low && value <= high ? value : -1;
}

}  // namespace

std::optional<Event> Decode(uint32_t packed) {
    uint8_t status = static_cast<uint8_t>(packed & 0xFF);
    uint8_t data1 = static_cast<uint8_t>((packed >> 8) & 0x7F);
    uint8_t data2 = static_cast<uint8_t>((packed >> 16) & 0x7F);
    Event event;
    event.channel = (status & 0x0F) + 1;
    event.number = data1;
    event.value = data2;
    switch (status & 0xF0) {
        case 0x90: event.kind = data2 == 0 ? Kind::NoteOff : Kind::NoteOn; return event;
        case 0x80: event.kind = Kind::NoteOff; return event;
        case 0xB0: event.kind = Kind::ControlChange; return event;
        default: return std::nullopt;  // pitch bend, aftertouch, clock, sysex...
    }
}

uint32_t Pack(uint8_t status, uint8_t data1, uint8_t data2) {
    return static_cast<uint32_t>(status) | (static_cast<uint32_t>(data1 & 0x7F) << 8) |
           (static_cast<uint32_t>(data2 & 0x7F) << 16);
}

std::string EventJson(const Event& event) {
    const char* type = event.kind == Kind::NoteOn ? "note" : event.kind == Kind::NoteOff ? "noteoff" : "cc";
    return std::string("{\"type\":\"") + type + "\",\"channel\":" + std::to_string(event.channel) +
           ",\"number\":" + std::to_string(event.number) + ",\"value\":" + std::to_string(event.value) + "}";
}

std::optional<Binding> ParseBinding(std::string_view input, std::string* error) {
    std::string text = Lower(input);
    Binding binding;
    if (text == "*") return binding;
    std::string_view rest = text;
    if (rest.substr(0, 2) == "ch") {
        size_t colon = rest.find(':');
        int channel = colon == std::string_view::npos ? -1 : Number(rest.substr(2, colon - 2), 1, 16);
        if (channel < 0) {
            Fail(error, "canal MIDI invalide dans « " + std::string(input) + " » (ch1 à ch16)");
            return std::nullopt;
        }
        binding.channel = channel;
        rest.remove_prefix(colon + 1);
    }
    size_t colon = rest.find(':');
    std::string_view type = rest.substr(0, colon);
    std::string_view number = colon == std::string_view::npos ? std::string_view() : rest.substr(colon + 1);
    if (type == "note") binding.type = Binding::Type::Note;
    else if (type == "cc") binding.type = Binding::Type::Control;
    else {
        Fail(error, "liaison MIDI inconnue : « " + std::string(input) + " » (ex. note:60, cc:7, ch2:note:36, *)");
        return std::nullopt;
    }
    if (number == "*") return binding;
    binding.number = Number(number, 0, 127);
    if (binding.number < 0) {
        Fail(error, "numéro MIDI invalide dans « " + std::string(input) + " » (0 à 127)");
        return std::nullopt;
    }
    return binding;
}

std::string BindingText(const Binding& binding) {
    if (binding.type == Binding::Type::Any) return "*";
    std::string out = binding.channel ? "ch" + std::to_string(binding.channel) + ":" : "";
    out += binding.type == Binding::Type::Note ? "note:" : "cc:";
    out += binding.number < 0 ? "*" : std::to_string(binding.number);
    return out;
}

bool Matches(const Binding& binding, const Event& event) {
    if (binding.type == Binding::Type::Any) return true;
    if (binding.channel && binding.channel != event.channel) return false;
    if (binding.number >= 0 && binding.number != event.number) return false;
    if (binding.type == Binding::Type::Note) return event.kind == Kind::NoteOn;
    return event.kind == Kind::ControlChange;
}

bool Map::Parse(std::string_view text, std::string* error) {
    std::vector<Rule> rules;
    std::string_view rest = text;
    while (!rest.empty()) {
        size_t end = rest.find_first_of(";,");
        std::string_view item = rest.substr(0, end);
        rest = end == std::string_view::npos ? std::string_view() : rest.substr(end + 1);
        std::string trimmed = Lower(item);
        if (trimmed.empty()) continue;
        size_t eq = trimmed.find('=');
        if (eq == std::string::npos) return Fail(error, "midi.map : « " + trimmed + " » attend liaison=action");
        std::optional<Binding> binding = ParseBinding(trimmed.substr(0, eq), error);
        if (!binding) return false;
        if (binding->type == Binding::Type::Any) return Fail(error, "midi.map : « * » ne peut pas recevoir d'action");
        std::string action = trimmed.substr(eq + 1);
        Rule rule{*binding, Action::Next};
        if (action == "play") rule.action = Action::Play;
        else if (action == "pause") rule.action = Action::Pause;
        else if (action == "next") rule.action = Action::Next;
        else if (action == "previous") rule.action = Action::Previous;
        else if (action == "gain") rule.action = Action::Gain;
        else return Fail(error, "midi.map : action inconnue « " + action + " » (play, pause, next, previous, gain)");
        if (rule.action == Action::Gain && binding->type != Binding::Type::Control) {
            return Fail(error, "midi.map : gain demande un contrôleur (cc:<n>)");
        }
        rules.push_back(rule);
    }
    m_rules = std::move(rules);
    return true;
}

std::optional<Decision> Map::Handle(const Event& event) {
    int previous = 0;
    if (event.kind == Kind::ControlChange) {
        int& last = m_lastValue[event.channel - 1][event.number];
        previous = last;
        last = event.value;
    }
    for (const Rule& rule : m_rules) {
        if (!Matches(rule.binding, event)) continue;
        if (rule.action == Action::Gain) {
            return Decision{Action::Gain, -30.0 + 30.0 * static_cast<double>(event.value) / 127.0};
        }
        if (event.kind == Kind::ControlChange && !(previous < 64 && event.value >= 64)) return std::nullopt;
        return Decision{rule.action, 0};
    }
    return std::nullopt;
}

}  // namespace amk::midi
