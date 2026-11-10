#pragma once

// The readout under the pointer on the progress line of the playback bar (Ampify.ui.playbar.readout): a thin marker
// where the pointer is and, over it, the time of the song at that point. Laid out on a line `width` pixels long; the
// marker and the label stay whole inside the line, so the label stops at the ends instead of leaving the bar.

#include <algorithm>
#include <cmath>
#include <string>

namespace ampify {

struct HoverReadout {
    bool shown = false;
    double part = 0;         // where the pointer is, as a part of the line (0 to 1)
    double markerLeft = 0;   // left edge of the marker
    double labelLeft = 0;    // left edge of the label
};

inline HoverReadout LayOutHoverReadout(double width, double x, double markerWidth, double labelWidth) {
    HoverReadout readout;
    if (!(width > markerWidth) || !std::isfinite(x)) return readout;
    double at = std::clamp(x, 0.0, width);
    readout.shown = true;
    readout.part = at / width;
    readout.markerLeft = std::clamp(at - markerWidth / 2, 0.0, width - markerWidth);
    double label = std::max(0.0, labelWidth);
    readout.labelLeft = label >= width ? (width - label) / 2 : std::clamp(at - label / 2, 0.0, width - label);
    return readout;
}

// A time as the playback bar writes it: 3:07, or 1:02:09 from an hour on. Seconds are cut, never rounded up, so the
// readout never names a second the song has not reached at that point.
inline std::string ClockText(double ms) {
    constexpr double kLongestMs = 3.6e11;  // 100 000 hours: past it the readout says so, and the cast stays defined
    long long seconds = std::isfinite(ms) && ms > 0 ? static_cast<long long>(std::min(ms, kLongestMs) / 1000) : 0;
    long long hours = seconds / 3600;
    long long minutes = seconds / 60 % 60;
    auto two = [](long long value) { return (value < 10 ? "0" : "") + std::to_string(value); };
    if (hours > 0) return std::to_string(hours) + ":" + two(minutes) + ":" + two(seconds % 60);
    return std::to_string(minutes) + ":" + two(seconds % 60);
}

}  // namespace ampify
