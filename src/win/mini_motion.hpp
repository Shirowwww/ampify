#pragma once

// The album's animated cover in Apple's mini player (Ampify.ui.miniPlayer.set({ motion })). Apple shows motion
// artwork on album pages only, in a MediaPlayerElement of its own (MotionArtworkPlayer); its mini player only ever has
// the still cover. An element finds the album's video in the catalog and gives its address (an MP4 file on Apple's
// host, checked by IsMotionArtworkAddress); Ampify downloads it once, keeps the last few in memory, and plays it muted
// and looping in a MediaPlayerElement laid over the mini player's artwork, inside the artwork's own frame, so it
// follows the artwork when Apple grows it into the album view. Nothing plays while Windows' animation effects are off.
// Interface thread of the mini player only. Compiled only with AMPIFY_WITH_XAML.

#if defined(AMPIFY_WITH_XAML)

#include <memory>
#include <string>

#include <winrt/Microsoft.UI.Xaml.h>

#include "../core/journal.hpp"

namespace ampify::win {

struct MiniMotionState;  // what plays now, held by the mini player's window context

// Shows `address` over the artwork under `root` (empty: none, and Apple's still cover shows again). Called at each
// refresh of the mini player: it only acts when something changed or the artwork's frame was rebuilt.
void FollowMiniMotion(std::shared_ptr<MiniMotionState>& state, const winrt::Microsoft::UI::Xaml::DependencyObject& root,
                      const std::string& address, Journal& journal) noexcept;

// Stops the video and takes its element out. Interface thread of the mini player.
void ReleaseMiniMotion(std::shared_ptr<MiniMotionState>& state) noexcept;

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
