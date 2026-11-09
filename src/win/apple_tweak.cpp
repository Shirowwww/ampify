#if defined(AMPIFY_WITH_XAML)

#include "apple_tweak.hpp"

#include "artwork_passthrough.hpp"
#include "lcd_links.hpp"
#include "lyrics_window.hpp"
#include "notifications.hpp"
#include "player_controls.hpp"
#include "sharp_artwork.hpp"
#include "sidebar_hover.hpp"
#include "text_marquee.hpp"
#include "topbar_search.hpp"
#include "../core/journal.hpp"

namespace ampify::win {

namespace {

struct AppleTweak {
    // Takes the tweak's value from the settings; nullptr for a tweak without a setting.
    void (*apply)(const uirender::AppleTweaks& tweaks, Journal& journal);
    // Puts the tweak back in place in the main window; nullptr for a tweak followed elsewhere (the notification hook,
    // the full-screen player's buttons and cover, which apple_player.cpp follows while that window is open).
    void (*keep)(Journal& journal) noexcept;
};

// The order is the one of the keeps: the sidebar folds Apple's NavigationView first, and the top bar then takes the
// search field out of the folded pane's ClosedCompact state, which the fold has put in place (topbar_search.cpp).
constexpr AppleTweak kTweaks[] = {
    {nullptr, KeepBarArtworkJournal},
    {nullptr, KeepDisplayLinks},
    {[](const uirender::AppleTweaks& t, Journal&) { SetTextMarquee(t.textMarquee); }, KeepTextMarquee},
    {[](const uirender::AppleTweaks& t, Journal&) { SetSidebarOnHover(t.sidebarOnHover); }, KeepSidebarOnHover},
    {[](const uirender::AppleTweaks& t, Journal&) { SetTopbarSearch(t.topbarSearch); }, KeepTopbarSearch},
    {[](const uirender::AppleTweaks& t, Journal& journal) { SetNotificationRule(t.notifications, journal); }, nullptr},
    {[](const uirender::AppleTweaks& t, Journal&) { SetPlayerControlsShown(t.playerControls); }, nullptr},
    {[](const uirender::AppleTweaks& t, Journal&) { SetSharpArtwork(t.sharpArtwork); }, nullptr},
    {[](const uirender::AppleTweaks& t, Journal&) { SetLyricsWindow(t.lyricsBefore, t.lyricsAfter); }, nullptr},
};

}  // namespace

void ApplyAppleTweaks(const uirender::AppleTweaks& tweaks, Journal& journal) {
    for (const AppleTweak& tweak : kTweaks) {
        if (tweak.apply) tweak.apply(tweaks, journal);
    }
}

void KeepAppleTweaks(Journal& journal) noexcept {
    for (const AppleTweak& tweak : kTweaks) {
        if (tweak.keep) tweak.keep(journal);
    }
}

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
