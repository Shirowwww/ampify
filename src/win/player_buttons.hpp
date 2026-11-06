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

// Apple's player volume, 0 to 1: from the audio process (src/kernel/kernel.cpp installs the audio hooks' setter) or,
// in the interface process, through the main window's volume model (src/win/xaml_host.cpp, which runs it on the
// interface thread and names the reason when it fails, so the log can say why).
using SetPlayerVolumeFn = bool (*)(double volume);
using PlayerVolumeProblemFn = std::string (*)();
void InstallPlayerVolume(SetPlayerVolumeFn setter, PlayerVolumeProblemFn problem = nullptr);

// The start in ms of the lyrics line Apple's lyrics view lights up, or -1 when there is none. Apple Music 1.1792 leaves
// the position of its player, of its media session and of its progress bar at 0 while a song plays; the line its
// lyrics view lights up is the one sign of where the song is. Runs on the interface thread; the XAML build installs it
// (src/win/xaml_host.cpp), src/win/player_bridge.cpp reads it.
using ReadLyricsLineStartFn = long long (*)();
void InstallLyricsLineStart(ReadLyricsLineStartFn read);

}  // namespace ampify::win
