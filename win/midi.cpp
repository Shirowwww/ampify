#include "midi.hpp"

#include "win_common.hpp"

namespace amk::win {

MidiInput::~MidiInput() { Close(); }

std::vector<std::string> MidiInput::Devices() {
    std::vector<std::string> names;
    UINT count = midiInGetNumDevs();
    for (UINT i = 0; i < count; i++) {
        MIDIINCAPSW caps{};
        if (midiInGetDevCapsW(i, &caps, sizeof caps) == MMSYSERR_NOERROR) {
            names.push_back(ToUtf8(caps.szPname));
        } else {
            names.push_back("MIDI " + std::to_string(i));
        }
    }
    return names;
}

bool MidiInput::Open(unsigned index, OnMessage onMessage, std::string* error) {
    Close();
    std::vector<std::string> devices = Devices();
    if (index >= devices.size()) {
        if (error) {
            *error = devices.empty() ? "aucun appareil MIDI branché"
                                     : "appareil MIDI " + std::to_string(index) + " absent (" +
                                           std::to_string(devices.size()) + " trouvé(s))";
        }
        return false;
    }
    m_onMessage = std::move(onMessage);
    HMIDIIN handle = nullptr;
    MMRESULT result = midiInOpen(&handle, index, reinterpret_cast<DWORD_PTR>(&Callback),
                                 reinterpret_cast<DWORD_PTR>(this), CALLBACK_FUNCTION);
    if (result != MMSYSERR_NOERROR) {
        if (error) {
            *error = result == MMSYSERR_ALLOCATED ? "appareil déjà utilisé par un autre programme"
                                                  : "midiInOpen a échoué (code " + std::to_string(result) + ")";
        }
        return false;
    }
    if (midiInStart(handle) != MMSYSERR_NOERROR) {
        midiInClose(handle);
        if (error) *error = "midiInStart a échoué";
        return false;
    }
    m_handle = handle;
    m_name = devices[index];
    return true;
}

void MidiInput::Close() {
    if (!m_handle) return;
    midiInStop(m_handle);
    midiInReset(m_handle);
    midiInClose(m_handle);
    m_handle = nullptr;
}

void CALLBACK MidiInput::Callback(HMIDIIN, UINT message, DWORD_PTR instance, DWORD_PTR param1, DWORD_PTR) {
    if (message != MIM_DATA) return;
    auto* self = reinterpret_cast<MidiInput*>(instance);
    if (self && self->m_onMessage) self->m_onMessage(static_cast<uint32_t>(param1));
}

}  // namespace amk::win
