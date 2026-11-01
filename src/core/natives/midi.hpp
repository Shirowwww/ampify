#pragma once

// The optional host service behind Ampify.midi (permission midi.input, setting midi.enabled). The Windows host reads
// the device chosen by midi.device with winmm (src/win/midi.*). Each note or controller message comes back to the
// elements as dispatch.midi(<midi::EventJson>).

#include <string>
#include <string_view>
#include <vector>

namespace ampify {

struct MidiHost {
    static constexpr std::string_view kCapabilityName = "midi";
    virtual ~MidiHost() = default;

    // The element wants the MIDI messages. False when no device could be opened.
    virtual bool SubscribeMidi(const std::string& elementId) = 0;
    virtual void UnsubscribeMidi(const std::string& elementId) = 0;

    // Names of the MIDI input devices, in the order of midi.device.
    virtual std::vector<std::string> MidiDevices() = 0;
};

}  // namespace ampify
