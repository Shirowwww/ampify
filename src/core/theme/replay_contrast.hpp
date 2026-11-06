#pragma once

#include <string>
#include <vector>

#include "replay.hpp"

namespace ampify::theme {

// The legibility of each text after a replay (docs/development/offline-bench.md, "Contrast"): its final colour, faded
// by its own opacity and its parents', over the colour under it. That colour is the nearest element up the tree with a
// known opaque background, with the translucent colours between laid over it; a background the dump only holds as
// "present" (a brush the inspector cannot express) is passed over and counted.
struct TextContrast {
    enum class Verdict {
        Fine,       // the ratio reaches what the text needs
        Low,        // it does not
        NotJudged,  // a colour of Apple's in it is from the other mode than the dump's (SetAppMode, color-scheme)
        Unknown,    // no element up the tree has a known opaque colour
    };
    int node = -1;
    int backdrop = -1;     // the element whose opaque background the text is judged on, -1 when none
    std::string text;      // the text's colour as shown, "#RRGGBB"
    std::string under;     // the colour under it, "#RRGGBB"
    double ratio = 0;
    double needed = 4.5;   // 3 for large text (24 px, or 18.66 px bold) and icons
    int passedOver = 0;    // backgrounds on the way that the dump does not know ("present", images, gradients)
    Verdict verdict = Verdict::Fine;
};

// The texts whose colour or the colour under them the theme changed; a text with only Apple's colours on both sides is
// Apple's design and left out. Hidden texts (collapsed, or at opacity 0 with a parent) are left out too.
std::vector<TextContrast> JudgeContrast(ReplayBackend& tree);

const char* VerdictName(TextContrast::Verdict verdict);

}  // namespace ampify::theme
