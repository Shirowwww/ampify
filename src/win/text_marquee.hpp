#pragma once
// A text cut with an ellipsis ("…") scrolls under the pointer, back and forth with a pause at each end, then rests
// where it began once the pointer leaves, as Apple's own display does with a long title. It works on any single-line
// text of the main window and of its popups (menus, Ampify's panels): the text under the pointer, or every cut text of
// the row, card or button under it. Nothing runs while the pointer is still or elsewhere.

#if defined(AMPIFY_WITH_XAML)

namespace ampify {
class Journal;
}

namespace ampify::win {

// Puts the pointer handlers on the main window and its open popups, or checks they are still there; turned off
// (SetTextMarquee), takes them off and puts back any text that scrolls. Interface thread only, cheap once in place.
void KeepTextMarquee(Journal& journal) noexcept;

// apple.marquee: on or off, applied at the next KeepTextMarquee. Any thread.
void SetTextMarquee(bool enabled) noexcept;

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
