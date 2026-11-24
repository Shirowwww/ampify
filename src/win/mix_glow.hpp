#pragma once

// While AutoMix's mix is heard, Apple's progress line shows it as Apple Music does on the iPhone: the part played
// glows, and "Mixing" shows between the two times with a light passing over it. In the play bar's display and in the
// full-screen player. The audio process says when (SharedAudio::mixHeardSinceMs, src/win/audio/automix.cpp).

namespace ampify {
class Journal;
}

namespace ampify::win {

// Ampify's layer over Apple's line, which holds the glow and the light, and its label: themes leave them alone
// (xaml_backend.cpp), so a rule for Apple's texts cannot show or hide the label.
inline constexpr char kMixLayerName[] = "AmpifyMixLayer";
inline constexpr char kMixLabelName[] = "AmpifyMixLabel";

// Finds the progress lines of the main window and of the full-screen player, and keeps the timer that follows the
// mix running. Interface thread only; cheap once both are found.
void KeepMixGlow(Journal& journal) noexcept;

}  // namespace ampify::win
