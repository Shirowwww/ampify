#pragma once

// The frames of a MediaPlayer drawn into an ordinary XAML image. A MediaPlayerElement is external content: Windows
// composes its video apart from the XAML tree, so it ignores the rotation, the rounded corners and the clip of the
// elements around it (seen 25/11/2026: the animated cover stayed square and still over the turning record of Spinning
// artwork). Here the player serves its frames (IsVideoFrameServerEnabled); each is copied, at the size it is shown,
// into a Direct3D surface, then into a SoftwareBitmapSource that an Image shows like any picture. A frame that comes
// while the previous one is still on its way is skipped. Compiled only with AMPIFY_WITH_XAML.

#if defined(AMPIFY_WITH_XAML)

#include <functional>
#include <memory>

#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
#include <winrt/Windows.Media.Playback.h>

namespace ampify::win {

class VideoFrames {
public:
    struct Shared;

    // Serves `player`'s frames to `source`, on `queue`'s thread (the window's); `firstFrame` runs there once, when the
    // first frame shows. The player must not be shown by a MediaPlayerElement. Called on that thread.
    static std::shared_ptr<VideoFrames> Start(const winrt::Windows::Media::Playback::MediaPlayer& player,
                                              const winrt::Microsoft::UI::Xaml::Media::Imaging::SoftwareBitmapSource& source,
                                              const winrt::Microsoft::UI::Dispatching::DispatcherQueue& queue,
                                              std::function<void()> firstFrame);

    // The longest side, in pixels, the frames are copied at (the video's own size at most). Any thread.
    void Side(int pixels) noexcept;

    // No frame is served any more. Thread of the queue given to Start.
    void Stop() noexcept;

    ~VideoFrames();

private:
    VideoFrames() = default;
    std::shared_ptr<Shared> m_shared;
    winrt::Windows::Media::Playback::MediaPlayer m_player{nullptr};
    winrt::event_token m_token{};
};

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
