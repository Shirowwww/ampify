#pragma once
// ui_renderer.cpp and ui_renderer_playing.cpp only: the views of Ampify.ui that show the song playing, its cover
// (an image with src "artwork" or a queue entry's) and its lyrics (the `lyrics` node).

#if defined(AMPIFY_WITH_XAML)

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
#include <winrt/Windows.UI.h>

#include "../core/now_playing.hpp"
#include "artwork_fade.hpp"
#include "presence_watch.hpp"

namespace ampify::win::playing_views {

namespace xaml = winrt::Microsoft::UI::Xaml;
namespace controls = xaml::Controls;
namespace media = xaml::Media;

constexpr double kLyricsSize = 26;

// An image that shows the song playing (src: "artwork"): Apple's play-bar image tells when the song changes.
struct ArtworkFollow {
    media::ImageBrush brush{nullptr};
    controls::Image sizer{nullptr};
    controls::Image apple{nullptr};  // set while followed
    int64_t token = 0;
    bool off = false;  // the image shows something else now
    // The large cover being loaded and its handlers, removed by token: auto_revoke goes through a weak reference,
    // which never resolves for a XAML object Ampify made, so the handlers would stay and an old cover land late.
    xaml::Media::Imaging::BitmapImage loading{nullptr};
    winrt::event_token openedToken{};
    winrt::event_token failedToken{};
    bool pinned = false;  // the sizer held at its height while it loads the large cover
    // The frame's Loaded and Unloaded handlers, removed when the image shows something else. The frame itself is not
    // kept: it holds those handlers, which hold the follower.
    winrt::event_token loadedToken{};
    winrt::event_token unloadedToken{};
    // The song playing only: the previous cover fades out over the new one, rounded like the frame.
    ArtworkFade fade;
    xaml::CornerRadius corner{};
    // Without Apple's play-bar image in the window, Apple's loader gives the cover of each new song (PollArtwork).
    winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer poll{nullptr};
    std::string polledKey;  // the song whose cover the loader last gave

    ~ArtworkFollow() {
        try {
            if (poll) poll.Stop();
        } catch (...) {  // off the interface thread, or once its queue has stopped: never terminate over a timer
        }
    }
};

// The lyrics of the song playing (the `lyrics` node), lit word by word as in Apple's own view: one TextBlock per line,
// one Run per word whose brush brightens as the word is sung, the current line larger and kept in view. Driven by a
// timer of the interface thread from src/core/now_playing.hpp, so the extension does not render it again and again.
// The lines move by their Translation inside a clipped viewport rather than in a ScrollViewer of their own, which took
// the wheel from the panel around them: the wheel moves the panel first, and the lines once the panel goes no further.
// Lines the user moved stay put (a button brings back the line being sung) until that line comes into view by itself.
struct LyricsView {
    controls::Grid viewport{nullptr};
    controls::StackPanel lines{nullptr};
    controls::TextBlock placeholder{nullptr};
    controls::Button back{nullptr};
    controls::FontIcon backIcon{nullptr};
    double offset = 0;    // how far the lines are moved up
    bool manual = false;  // the user moved them
    std::chrono::steady_clock::time_point resting{};  // since when the pointer rests on the lines
    SeenTimer timer;  // a frame of the karaoke, while the view is loaded and the window seen
    std::shared_ptr<const Lyrics> shown;
    std::vector<std::vector<media::SolidColorBrush>> words;  // per line, per word (one per line without word times)
    int current = -2;                                        // the line drawn as current; -2 before the first frame
    double size = kLyricsSize;
    winrt::Windows::UI::Color color{255, 255, 255, 255};
    bool stale = true;  // the look changed: build the lines again
    std::weak_ptr<LyricsView> self;
    std::function<void(int64_t)> pick;  // a line clicked: its start, for the element's onChange
};

// A queue entry's cover (AppleArtworkSource): Apple's loader draws it, then it is decoded large. It does not follow
// anything, as the entry keeps its cover; the image gets a new src when the view shows another entry.
std::shared_ptr<ArtworkFollow> ShowQueueArtwork(const media::ImageBrush& brush, const controls::Image& sizer,
                                                std::uint64_t itemId);

// The image shows something else now: what it followed stops, and its fade layer leaves the frame.
void DropFollow(const controls::Border& frame, ArtworkFollow& follow);

// Ties an image of a view to Apple's own image of the song playing, which changes with every song, and shows that
// cover loaded large. Followed while the view is in the window, where Apple's image can be found.
std::shared_ptr<ArtworkFollow> FollowPlayingArtwork(const controls::Border& frame, const media::ImageBrush& brush,
                                                    const controls::Image& sizer);

void PaintLyrics(LyricsView& view);

xaml::FrameworkElement MakeLyricsView(const std::shared_ptr<LyricsView>& view);

}  // namespace ampify::win::playing_views

#endif  // AMPIFY_WITH_XAML
