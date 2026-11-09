#pragma once
// With Ampify's buttons, Apple's centred row of toggles (volume, lyrics, queue) grows past its column and runs under
// the window's minimize, maximize and close buttons, at the top right of the bar: it then sits a little lower, clear
// of them (FoldPlaybarButtons in src/win/ui_service_playbar.cpp).

#include "topbar.hpp"

namespace ampify {

constexpr double kCaptionClearance = 8;        // a row ending this close to them counts as under them
constexpr double kTogglesDrop = 4;             // how much lower the row then sits

// How far below its place the row of toggles goes: its right end and its top in window pixels, the window's width.
inline double TogglesDrop(double togglesRight, double togglesTop, double windowWidth) {
    bool underButtons = togglesRight > windowWidth - kTopbarCaptionWidth - kCaptionClearance;
    bool inTheirStrip = togglesTop < kTopbarCaptionHeight;
    return underButtons && inTheirStrip ? kTogglesDrop : 0;
}

}  // namespace ampify
