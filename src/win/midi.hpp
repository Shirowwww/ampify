#pragma once

// A MIDI input device through winmm (midiInOpen). Each short message is handed to the callback on winmm's own
// thread, packed as in src/core/midi_map.hpp; the callback must only queue. No output: Ampify sends no light back.

#include <windows.h>
#include <mmsystem.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace ampify::win {

class MidiInput {
public:
    using OnMessage = std::function<void(uint32_t packed)>;

    MidiInput() = default;
    ~MidiInput();

    MidiInput(const MidiInput&) = delete;
    MidiInput& operator=(const MidiInput&) = delete;

    // Names of the input devices, in winmm order (the index of midi.device).
    static std::vector<std::string> Devices();

    // Opens and starts device `index`. False with `error` in French (no device, already taken...).
    bool Open(unsigned index, OnMessage onMessage, std::string* error);
    void Close();

    bool open() const { return m_handle != nullptr; }
    const std::string& name() const { return m_name; }

private:
    static void CALLBACK Callback(HMIDIIN handle, UINT message, DWORD_PTR instance, DWORD_PTR param1, DWORD_PTR param2);

    HMIDIIN m_handle = nullptr;
    OnMessage m_onMessage;
    std::string m_name;
};

}  // namespace ampify::win
