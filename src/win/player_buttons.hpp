#pragma once

// What Ampify.player.shuffle(), repeat(), setShuffle() and setRepeat() reach in Apple Music: the shuffle and repeat
// buttons of the main window, read and pressed on the interface thread. The XAML build installs the two functions at
// start (src/win/xaml_host.cpp, which owns the buttons' code in src/win/player_controls.cpp); without them, as in the
// tests, nothing is known and nothing is pressed.

#include <string>

namespace ampify::win {

struct MainPlayerModes {
    int shuffle = -1;  // -1 not known, 0 off, 1 on
    int repeat = -1;   // -1 not known, 0 off, 1 all, 2 one
    unsigned repeatGlyph = 0;  // the icon the repeat button shows, to tell "all" from "one" (journaled once)
    std::string detail;        // how the repeat button looks (its dot, its icon colour), journaled when it changes
};

using ReadPlayerModesFn = MainPlayerModes (*)();
using PressPlayerButtonFn = bool (*)(bool shuffle);  // true: shuffle, false: repeat

// Both run on the interface thread. Null functions remove the support.
void InstallPlayerButtons(ReadPlayerModesFn read, PressPlayerButtonFn press);

// Apple's player volume, 0 to 1, from the audio process (src/kernel/kernel.cpp installs the leveler's setter).
using SetPlayerVolumeFn = bool (*)(double volume);
void InstallPlayerVolume(SetPlayerVolumeFn setter);

}  // namespace ampify::win
