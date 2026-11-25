#pragma once

// The album's animated cover over one of Apple's artworks: the mini player's (Ampify.ui.miniPlayer.set({ motion })),
// and the play bar's, the full-screen player's and the mini player's again through Ampify.ui.animatedCover. Apple
// shows motion artwork on album pages only, in a MediaPlayerElement of its own (MotionArtworkPlayer); elsewhere it only
// ever has the still cover. An element finds the album's video in the catalog and gives its address (an MP4 file on
// Apple's host, checked by IsMotionArtworkAddress); Ampify downloads it once, keeps the last few in memory, and plays
// it muted and looping in an Image (fed by VideoFrames) laid over the artwork, inside the artwork's own frame, so it follows the
// artwork when Apple grows it (the mini player's album view) and takes its shape when a theme rounds or turns it.
// Nothing plays while Windows' animation effects are off. Interface thread of the window that holds the artwork.
// Compiled only with AMPIFY_WITH_XAML.

#if defined(AMPIFY_WITH_XAML)

#include <memory>
#include <string>

#include <winrt/Microsoft.UI.Xaml.h>

#include "../core/journal.hpp"

namespace ampify::win {

struct ArtworkMotionState;  // what plays now in one place, held by the window's context

// The artworks an animated cover can stand over: which one, found under the window's content, and its name in the log.
enum class MotionPlace { Mini, Playbar, FullScreen };

// Shows `address` over the artwork of `place` under `root` (empty: none, and Apple's still cover shows again). Called
// at each refresh of the window: it only acts when something changed or the artwork's frame was rebuilt. False while
// a video is wanted and the artwork is not laid out yet (the caller may look again a moment later).
bool FollowArtworkMotion(std::shared_ptr<ArtworkMotionState>& state, MotionPlace place,
                         const winrt::Microsoft::UI::Xaml::DependencyObject& root, const std::string& address,
                         Journal& journal) noexcept;

// Stops the video and takes its element out. Interface thread of the window.
void ReleaseArtworkMotion(std::shared_ptr<ArtworkMotionState>& state) noexcept;

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
