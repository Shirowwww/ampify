#pragma once

// The optional host services behind Ampify.player.shuffle(), repeat(), setShuffle(), setRepeat() and setVolume().
// See src/core/host.hpp for how a capability works. Shuffle and repeat are Apple's own buttons, which only exist in
// AppleMusic.exe ("ui" process); the volume is Apple's player volume, set from the volume control of its window ("ui"
// process, Apple then moves its own slider) or by a hook in AMPLibraryAgent.exe ("engine" process). A host without a
// service makes the readers answer null and the setters answer false.

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>

namespace ampify {

// Apple Music's volume slider is in decibels: its position p (0 to 1) gives 30 x (p - 1) dB (measured on 1.1792, and
// what the volume-percentage example uses). The position that gives a player gain of `gain` (0 to 1).
inline double SliderPositionOfGain(double gain) {
    constexpr double kRangeDb = 30;
    if (!(gain > 0)) return 0;
    return std::clamp(1 + 20 * std::log10(gain) / kRangeDb, 0.0, 1.0);
}

struct PlayerModes {
    std::optional<bool> shuffle;  // nullopt: not known (the player is not on screen, another Apple Music version)
    std::string repeat;           // "off", "all" or "one"; empty: not known
};

// "off", "all" or "one".
inline bool IsRepeatMode(std::string_view mode) { return mode == "off" || mode == "all" || mode == "one"; }

struct PlayerModesHost {
    static constexpr std::string_view kCapabilityName = "player.modes";
    virtual ~PlayerModesHost() = default;

    virtual PlayerModes Modes() = 0;
    // True once the player is in the asked state (already there, or reached by pressing Apple's button).
    virtual bool SetShuffle(bool on) = 0;
    virtual bool SetRepeat(const std::string& mode) = 0;
};

struct PlayerVolumeHost {
    static constexpr std::string_view kCapabilityName = "player.volume";
    virtual ~PlayerVolumeHost() = default;

    // Apple's player volume, 0 to 1. False when the player is not known yet (Apple sets its volume when a song starts).
    virtual bool SetVolume(double volume) = 0;
};

}  // namespace ampify
