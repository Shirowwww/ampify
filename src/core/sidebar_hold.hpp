#pragma once
// The hover sidebar stays open while someone uses it: typing in its search field, or a menu open in it, and only
// while the window is in front.

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
