#pragma once
// The hover sidebar stays open while someone uses it: typing in its search field, or a menu open in it, and only
// while the window is in front.

#include "sidebar_fold.hpp"

#include <chrono>
#include <optional>
#include <string>
#include <string_view>

namespace ampify {

constexpr auto kTypingHold = std::chrono::milliseconds(2000);

// The keyboard is in the search field and its text changed a moment ago.
inline bool HoldsForTyping(bool focusInPane, bool windowInFront, std::chrono::milliseconds sinceTyped) {
    return focusInPane && windowInFront && sinceTyped < kTypingHold;
}

// A menu or a suggestion list is open in the pane.
inline bool HoldsForMenu(bool windowInFront, bool menuOpen) {
    return windowInFront && menuOpen;
}

// The keyboard uses the pane: it was opened from the hamburger with a key, or a key went down in it since it opened,
// and the focus is still in it or on the hamburger. The pointer may be anywhere then. The focus alone does not hold:
// WinUI moves it into a pane the pointer opened, which then never closed.
inline bool HoldsForKeyboard(bool windowInFront, bool keysUsed, bool focusOnPane) {
    return windowInFront && keysUsed && focusOnPane;
}

// A pin pushes the pages aside only where Apple Music opens its own sidebar beside them. Narrower, Apple's narrow
// layout clips a pane opened beside the pages and hides the hamburger: the pinned pane stays over the pages instead.
inline bool PinPushesPages(double windowWidth) {
    return windowWidth > kSidebarFoldsAt;
}

// The pin button of the open pane: the Pin glyph and its words while the pane folds under the pointer, the Unpin glyph
// and its words while pinned (Segoe Fluent Icons; the label is an i18n key).
struct PinFace {
    const wchar_t* glyph;
    const char* label;
};

inline PinFace PinButtonFace(bool pinned) {
    if (pinned) return {L"\xE77A", "sidebar.unpin"};
    return {L"\xE718", "sidebar.pin"};
}

// The right margin that puts a button of the sidebar's header just left of "···" (whose own right margin and width
// are given), `spacing` apart, plus the room of the buttons already standing there.
inline double LeftOfMoreMargin(double moreRight, double moreWidth, double spacing, double taken = 0) {
    return moreRight + moreWidth + spacing + taken;
}

// A window coordinate `at` in an element's own coordinates along one axis, from where its local 0 and its local 1 land
// in the window. Mirrored (flow-direction rtl), 1 lands left of 0 and the offset counts from the element's right edge.
// None when the element is squashed flat.
inline std::optional<double> LocalOffset(double at, double origin, double unit) {
    const double step = unit - origin;
    if (step == 0) return std::nullopt;
    return (at - origin) / step;
}

// The pane folding under the pointer slides shut along this ease, as the control points of a cubic Bezier (x1, y1,
// x2, y2), in the time the pane takes to open: a standard ease that moves all along. WinUI's own fold lasts a third of
// its opening and does four fifths of the move in the first 60 ms, which read as a snap (measured 2026-11-10).
struct Ease {
    double x1, y1, x2, y2;
};
inline constexpr Ease kFoldEase{0.4, 0.0, 0.2, 1.0};
constexpr auto kFoldSlideFallback = std::chrono::milliseconds(250);  // when the opening's length cannot be read

// Notes when the search text last changed while the keyboard was in the field. The first look is not typing.
class TypingTracker {
public:
    using Clock = std::chrono::steady_clock;

    void Look(std::wstring_view text, Clock::time_point now) {
        if (hasText_ && text != text_) typed_ = now;
        text_.assign(text);
        hasText_ = true;
    }

    void Forget() {
        text_.clear();
        hasText_ = false;
        typed_.reset();
    }

    // Time since the text last changed; a long time when it never did.
    std::chrono::milliseconds SinceTyped(Clock::time_point now) const {
        if (!typed_) return kTypingHold;
        return std::chrono::duration_cast<std::chrono::milliseconds>(now - *typed_);
    }

private:
    std::wstring text_;
    bool hasText_ = false;
    std::optional<Clock::time_point> typed_;
};

}  // namespace ampify
