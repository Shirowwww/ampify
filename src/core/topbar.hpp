#pragma once
// apple.topbar = search: Apple's search field and a back arrow in the empty strip at the top of the window, next to the
// caption buttons (src/win/topbar_search.cpp). These are the rules it follows, in device-independent pixels.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

namespace ampify {

inline constexpr double kTopbarHeight = 40;         // the strip themes leave above the panels (--caption-room)
// Apple's search box keeps its TextBox's 32 minimum height whatever its slot is: a shorter slot cut off its bottom
// line and corners. The back arrow matches it.
inline constexpr double kAppleSearchBoxHeight = 32;
inline constexpr double kTopbarControlHeight = kAppleSearchBoxHeight;
inline constexpr double kTopbarMinTop = 4;          // the gap kept above them, even under a standard 32 caption
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
// Under the standard 32 caption the control touched the window's top edge: it keeps a gap above, and so sits centred
// in the 40 strip, as much lower than the caption buttons' glyphs as that gap.
inline TopbarLayout LayOutTopbar(double windowWidth, double captionHeight) {
    const double width = TopbarSearchWidth(windowWidth);
    const double left = (windowWidth - width) / 2;
    const double top = std::max(kTopbarMinTop, (captionHeight - kTopbarControlHeight) / 2);
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

// One setter of Apple's ClosedCompact state as Ampify can read it. `targetsPresenter` is unknown when WinUI cannot
// resolve the target: TargetPropertyPath.Target looks the name up from the path itself, outside the template's
// namescope, and fails with E_FAIL (0x80004005).
struct StateSetterLook {
    bool visibility = false;  // the setter's path is Visibility
    std::optional<bool> targetsPresenter;
    bool collapses = false;   // its value is Collapsed
};

// A Visibility setter's value collapses: Apple's template boxes it as an Int32 (Collapsed is 1), not as a Visibility,
// and markup read as text gives its name. Unread, it never collapses.
inline bool VisibilityCollapses(std::optional<int> number, std::wstring_view text) {
    return number ? *number == 1 : text == L"Collapsed";
}

// The setter that collapses the search presenter: the one whose target resolved to it, else the only Visibility setter
// with an unknown target that collapses (the state's other Visibility setters show the hamburger and the search
// button). Nullopt when neither is found, or when the second rule is ambiguous.
inline std::optional<std::size_t> PresenterSetterIndex(const std::vector<StateSetterLook>& setters) {
    std::optional<std::size_t> unresolved;
    int candidates = 0;
    for (std::size_t i = 0; i < setters.size(); i++) {
        const StateSetterLook& look = setters[i];
        if (!look.visibility) continue;
        if (look.targetsPresenter.value_or(false)) return i;
        if (!look.targetsPresenter && look.collapses) {
            unresolved = i;
            candidates++;
        }
    }
    return candidates == 1 ? unresolved : std::nullopt;
}

// One left margin Apple's sidebar header changes for its back arrow (the header grid's, the title's). While the strip
// holds the arrow, Apple's own one is invisible: the header keeps its place without it, as Apple lays it out when
// there is nowhere to go back to. That place is learnt from Apple; until Apple is seen without its arrow (the strip
// turned on with a page to go back to), it is the one Apple Music 1.1792 writes (kAppleHeaderLeft, kAppleTitleLeft).
inline constexpr double kAppleHeaderLeft = -26;  // Grid#PaneHeaderGrid without the arrow (5, or 6 folded, with it)
inline constexpr double kAppleTitleLeft = 34;    // TextBlock#AppName without the arrow (26 with it)

struct ArrowRoom {
    std::optional<double> without;  // Apple's value while its arrow is away
    std::optional<double> apple;    // Apple's value with the arrow, replaced while held

    // The value to write now that Apple's arrow shows or not and the margin is `current`; nullopt leaves it.
    std::optional<double> Next(bool arrowShows, double current) {
        if (!arrowShows) {
            without = current;
            apple.reset();
            return std::nullopt;
        }
        if (!without || current == *without) return std::nullopt;
        apple = current;
        return without;
    }

    // Apple's value to put back when the strip lets the arrow go, while it still shows.
    std::optional<double> Restore(bool arrowShows) const { return arrowShows ? apple : std::nullopt; }
};

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
