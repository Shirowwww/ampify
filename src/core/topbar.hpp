#pragma once
// apple.topbar = search: Apple's search field and a back arrow in the empty strip at the top of the window, next to the
// caption buttons (src/win/topbar_search.cpp). These are the rules it follows, in device-independent pixels.

#include <algorithm>
#include <cmath>
#include <vector>

namespace ampify {

inline constexpr double kTopbarHeight = 40;         // the strip themes leave above the panels (--caption-room)
inline constexpr double kTopbarControlHeight = 32;  // Apple's search box and the back arrow
inline constexpr double kTopbarBackWidth = 40;
inline constexpr double kTopbarGap = 8;             // between the back arrow and the field
inline constexpr double kTopbarCaptionWidth = 138;  // the Windows 11 caption buttons
inline constexpr double kTopbarCaptionHeight = 32;  // their standard height, which Apple Music keeps
inline constexpr double kTopbarSearchMaxWidth = 480;
inline constexpr double kTopbarSearchMinWidth = 240;
// Room for Back, the search field and the caption buttons; a narrower window keeps Apple's own layout and no strip.
// No forward arrow: Apple Music keeps a back stack only (knowledge/apple-music-internals.md, "Top bar").
inline constexpr double kTopbarMinWidth =
    2 * (kTopbarBackWidth + kTopbarGap + kTopbarCaptionWidth) + kTopbarSearchMinWidth;

struct TopbarRect {
    double x = 0;
    double y = 0;
    double width = 0;
    double height = 0;
};

struct DeviceRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    bool operator==(const DeviceRect&) const = default;
};

struct TopbarLayout {
    TopbarRect back;
    TopbarRect search;
};

// On when the setting asks for it, the window is wide enough, and Apple's transport bar is not at the top (the strip
// would cover it).
inline bool TopbarShown(bool setting, bool transportAtTop, double windowWidth) {
    return setting && !transportAtTop && windowWidth >= kTopbarMinWidth;
}

// The transport bar is at the top when it shows and starts inside the strip: Apple's own layout, or a theme's top bar.
inline bool TransportAtTop(bool visible, double top) {
    return visible && top < kTopbarHeight;
}

// Centred in the window: each side keeps room for Back and the caption buttons, so the field never reaches them.
inline double TopbarSearchWidth(double windowWidth) {
    double room = windowWidth - 2 * (kTopbarBackWidth + kTopbarGap + kTopbarCaptionWidth);
    return std::clamp(room, kTopbarSearchMinWidth, kTopbarSearchMaxWidth);
}

// The caption buttons' height in device-independent pixels, from the title bar's height in device pixels; Windows'
// standard height while the window has not extended its content into the title bar (a height of 0).
inline double TopbarCaptionHeight(int devicePixels, double scale) {
    return devicePixels > 0 && scale > 0 ? devicePixels / scale : kTopbarCaptionHeight;
}

// The field in the middle of the window, the back arrow just left of it, as Spotify lays them out. Both centre on the
// caption buttons, not on the strip: centred on the strip they sat 4 pixels (5 to 6 at 125%) under the buttons' glyphs.
inline TopbarLayout LayOutTopbar(double windowWidth, double captionHeight) {
    const double width = TopbarSearchWidth(windowWidth);
    const double left = (windowWidth - width) / 2;
    const double top = std::max(0.0, (captionHeight - kTopbarControlHeight) / 2);
    return TopbarLayout{{left - kTopbarGap - kTopbarBackWidth, top, kTopbarBackWidth, kTopbarControlHeight},
                        {left, top, width, kTopbarControlHeight}};
}

// Passthrough regions are in device pixels: the rectangle is rounded outwards so no pixel of the control is missed.
inline DeviceRect ToDeviceRect(const TopbarRect& rect, double scale) {
    int left = static_cast<int>(std::floor(rect.x * scale));
    int top = static_cast<int>(std::floor(rect.y * scale));
    int right = static_cast<int>(std::ceil((rect.x + rect.width) * scale));
    int bottom = static_cast<int>(std::ceil((rect.y + rect.height) * scale));
    return DeviceRect{left, top, right - left, bottom - top};
}

// The window's passthrough regions with Ampify's previous ones taken out and its new ones added. Apple sets its own
// regions too: they are kept as they are, in their order.
inline std::vector<DeviceRect> MergeRegions(const std::vector<DeviceRect>& current, const std::vector<DeviceRect>& before,
                                            const std::vector<DeviceRect>& now) {
    std::vector<DeviceRect> merged;
    for (const DeviceRect& rect : current) {
        if (std::find(before.begin(), before.end(), rect) == before.end()) merged.push_back(rect);
    }
    merged.insert(merged.end(), now.begin(), now.end());
    return merged;
}

}  // namespace ampify
