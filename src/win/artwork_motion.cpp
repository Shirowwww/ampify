// Built with AMPIFY_WITH_XAML, like the interface service that calls it.

#if defined(AMPIFY_WITH_XAML)

#include "artwork_motion.hpp"
#include "xaml_ending.hpp"

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Core.h>
#include <winrt/Windows.Media.Playback.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.System.Threading.h>
#include <winrt/Windows.Web.Http.h>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <utility>
#include <vector>

#include "video_frames.hpp"
#include "win_common.hpp"
#include "xaml_chrome.hpp"

namespace ampify::win {

namespace {

namespace controls = winrt::Microsoft::UI::Xaml::Controls;
namespace dispatching = winrt::Microsoft::UI::Dispatching;
namespace playback = winrt::Windows::Media::Playback;
namespace streams = winrt::Windows::Storage::Streams;
namespace xaml = winrt::Microsoft::UI::Xaml;
namespace xamlimaging = winrt::Microsoft::UI::Xaml::Media::Imaging;

constexpr int kMiniSearchLimit = 400;     // the artwork sits near the top of the mini player's tree
constexpr int kWindowSearchLimit = 6000;  // elements walked in the main window or the full-screen player
constexpr auto kDownloadTimeout = std::chrono::seconds(20);
constexpr std::uint32_t kMaxVideoBytes = 8u * 1024 * 1024;  // Apple's 486 px loop is under 2 MB
constexpr std::size_t kKeptVideos = 6;  // the albums played lately, so going back to one does not download it again

struct PlaceTraits {
    const wchar_t* element;  // the name of Ampify's player element
    const char* label;       // the place, in the log
};

PlaceTraits TraitsOf(MotionPlace place) {
    switch (place) {
        case MotionPlace::Playbar: return {L"AMPIFY_PlaybarMotion", "Play bar"};
        case MotionPlace::FullScreen: return {L"AMPIFY_FullScreenMotion", "Full-screen player"};
        case MotionPlace::Mini: break;
    }
    return {L"AMPIFY_MiniMotion", "Mini player"};
}

}  // namespace

struct ArtworkMotionState {
    MotionPlace place = MotionPlace::Mini;
    std::string address;  // what plays, or is being loaded
    std::uint64_t generation = 0;  // one more at each new address, so a late download is dropped
    bool loading = false;  // a download of this address is under way
    bool failed = false;   // this address could not be played: not tried again until another one comes
    // Strong: Ampify's own elements, released on this thread. The canvas stands in the artwork's frame and asks no
    // room of it (a canvas measures 0): the image inside, sized to the canvas, then never makes Apple's frame grow.
    controls::Canvas element{nullptr};
    controls::Image image{nullptr};
    playback::MediaPlayer player{nullptr};
    std::shared_ptr<VideoFrames> frames;  // the player's frames, drawn into the image's source
    winrt::weak_ref<controls::Panel> frame;  // the artwork's frame it stands in
};

namespace {

// The videos downloaded, the most recent last. Any thread: the mini player gets a new thread at each opening, and the
// downloads end on the thread pool. Never destroyed: a static destructor would run after Windows shut COM down.
struct VideoCache {
    std::mutex lock;
    std::vector<std::pair<std::string, streams::IBuffer>> videos;
};
VideoCache& Cache() {
    static auto* cache = new VideoCache();
    return *cache;
}

streams::IBuffer Kept(const std::string& address) {
    VideoCache& cache = Cache();
    std::lock_guard<std::mutex> guard(cache.lock);
    for (const auto& [kept, buffer] : cache.videos) {
        if (kept == address) return buffer;
    }
    return nullptr;
}

void Keep(const std::string& address, const streams::IBuffer& buffer) {
    VideoCache& cache = Cache();
    std::lock_guard<std::mutex> guard(cache.lock);
    std::erase_if(cache.videos, [&](const auto& entry) { return entry.first == address; });
    cache.videos.emplace_back(address, buffer);
    if (cache.videos.size() > kKeptVideos) cache.videos.erase(cache.videos.begin());
}

// The first element under `root`, breadth first, that `match` accepts; null when none within `limit` elements.
template <typename Match>
xaml::FrameworkElement FindFirst(const xaml::DependencyObject& root, int limit, Match&& match) {
    std::vector<xaml::DependencyObject> queue{root};
    for (std::size_t i = 0; i < queue.size() && queue.size() < static_cast<std::size_t>(limit); i++) {
        auto element = queue[i].try_as<xaml::FrameworkElement>();
        if (element && match(element)) return element;
        int count = xaml::Media::VisualTreeHelper::GetChildrenCount(queue[i]);
        for (int child = 0; child < count; child++) queue.push_back(xaml::Media::VisualTreeHelper::GetChild(queue[i], child));
    }
    return nullptr;
}

bool Named(const xaml::FrameworkElement& element, std::wstring_view name) {
    return element.Name() == name;
}

// The frame inside Apple's ArtworkImage#ActualArtwork (its AspectRatioGrid), which Apple sizes and, in the mini
// player, grows into the album view; null while the window has not laid it out. The play bar's artwork is the one in
// HoverGrid#ThumbnailHoverGrid, the full-screen player's the one in its FullScreenPlayerView (its ImmersionLockup
// background has artwork too).
controls::Panel FindArtworkFrame(const xaml::DependencyObject& root, MotionPlace place) {
    xaml::DependencyObject scope = root;
    int limit = kMiniSearchLimit;
    if (place == MotionPlace::Playbar) {
        scope = FindFirst(root, kWindowSearchLimit, [](const auto& element) { return Named(element, L"ThumbnailHoverGrid"); });
    } else if (place == MotionPlace::FullScreen) {
        scope = FindFirst(root, kWindowSearchLimit, [](const auto& element) {
            return std::wstring_view(winrt::get_class_name(element)).ends_with(L"FullScreenPlayerView");
        });
        limit = kWindowSearchLimit;
    }
    if (!scope) return nullptr;
    auto artwork = FindFirst(scope, limit, [](const auto& element) { return Named(element, L"ActualArtwork"); });
    if (!artwork) return nullptr;
    int count = xaml::Media::VisualTreeHelper::GetChildrenCount(artwork);
    for (int child = 0; child < count; child++) {
        if (auto panel = xaml::Media::VisualTreeHelper::GetChild(artwork, child).try_as<controls::Panel>()) return panel;
    }
    return nullptr;
}

void Remove(ArtworkMotionState& state) {
    if (state.frames) state.frames->Stop();
    state.frames = nullptr;
    if (state.player) {
        state.player.Pause();
        state.player.Source(nullptr);
        state.player.Close();
    }
    if (state.element) {
        if (auto panel = state.element.Parent().try_as<controls::Panel>()) {
            std::uint32_t index = 0;
            if (panel.Children().IndexOf(state.element, index)) panel.Children().RemoveAt(index);
        }
    }
    state.player = nullptr;
    state.element = nullptr;
    state.image = nullptr;
    state.frame = nullptr;
}

// A muted, looping player that stays out of Windows' media controls: the music's own session keeps them.
playback::MediaPlayer MakePlayer() {
    playback::MediaPlayer player;
    player.CommandManager().IsEnabled(false);
    player.IsMuted(true);
    player.IsLoopingEnabled(true);
    player.AutoPlay(true);
    return player;
}

// An ordinary image, so the video turns, rounds and fades with the artwork (VideoFrames says why not a
// MediaPlayerElement), in a canvas that fills the frame.
void MakeElements(ArtworkMotionState& state) {
    state.element = controls::Canvas();
    state.element.Name(TraitsOf(state.place).element);
    state.element.IsHitTestVisible(false);  // the click on the artwork stays Apple's (the album view, the mini player)
    state.image = controls::Image();
    state.image.Stretch(xaml::Media::Stretch::UniformToFill);
    state.image.Opacity(0);  // until the first frame, so the still cover never blinks
    state.element.Children().Append(state.image);
}

// The image takes the canvas's size; the frames are copied at that size in pixels: small in the play bar, large in
// full screen.
void FitImage(const controls::Canvas& canvas, VideoFrames& frames) {
    auto image = canvas.Children().Size() > 0 ? canvas.Children().GetAt(0).try_as<controls::Image>() : nullptr;
    if (!image) return;
    image.Width(canvas.ActualWidth());
    image.Height(canvas.ActualHeight());
    double scale = canvas.XamlRoot() ? canvas.XamlRoot().RasterizationScale() : 1.0;
    frames.Side(static_cast<int>(std::max(canvas.ActualWidth(), canvas.ActualHeight()) * scale + 0.5));
}

// Makes the elements and the player, the frames going from one to the other.
void Make(ArtworkMotionState& state) {
    state.player = MakePlayer();
    MakeElements(state);
    xamlimaging::SoftwareBitmapSource source;
    state.image.Source(source);
    winrt::weak_ref<controls::Image> weakImage = winrt::make_weak(state.image);
    state.frames = VideoFrames::Start(state.player, source, dispatching::DispatcherQueue::GetForCurrentThread(),
                                      [weakImage]() {
                                          if (auto image = weakImage.get()) image.Opacity(1);
                                      });
    std::weak_ptr<VideoFrames> weakFrames = state.frames;
    state.element.SizeChanged([weakFrames](const winrt::Windows::Foundation::IInspectable& sender, auto&&) noexcept {
        try {
            auto frames = weakFrames.lock();
            auto canvas = sender.try_as<controls::Canvas>();
            if (frames && canvas) FitImage(canvas, *frames);
        } catch (...) {
        }
    });
}

// Where the video goes among the frame's children: over Apple's pictures, under its rim (the Border that draws the
// artwork's edge, in the full-screen player), so the edge still shows.
std::uint32_t PlaceAmong(const controls::Panel& frame) {
    const auto children = frame.Children();
    for (std::uint32_t i = 0; i < children.Size(); i++) {
        if (children.GetAt(i).try_as<controls::Border>()) return i;
    }
    return children.Size();
}

// Puts the element in the artwork's frame, above Apple's still cover, when it is not there yet.
bool Place(ArtworkMotionState& state, const controls::Panel& frame) {
    if (!state.element) Make(state);
    if (state.frame.get() == frame && state.element.Parent() == frame) return true;
    if (auto old = state.element.Parent().try_as<controls::Panel>()) {
        std::uint32_t index = 0;
        if (old.Children().IndexOf(state.element, index)) old.Children().RemoveAt(index);
    }
    frame.Children().InsertAt(PlaceAmong(frame), state.element);
    state.frame = winrt::make_weak(frame);
    return true;
}

// On the window's thread: the stream goes to the player if the same address is still wanted there.
void Deliver(const std::weak_ptr<ArtworkMotionState>& weak, std::uint64_t generation,
             const streams::InMemoryRandomAccessStream& stream, const std::string& failure, Journal* journal) {
    auto state = weak.lock();
    if (!state || state->generation != generation || !state->player) return;
    state->loading = false;
    const std::string label = TraitsOf(state->place).label;
    if (!stream) {
        state->failed = true;
        Remove(*state);
        if (journal) journal->Write(label + ": the animated cover could not be loaded (" + failure + ")");
        return;
    }
    state->player.Source(winrt::Windows::Media::Core::MediaSource::CreateFromStream(stream, L"video/mp4"));
    if (journal) journal->Write(label + ": animated cover given to the player over the artwork");
}

// Downloads the video (or takes it from the cache) and copies it into a stream on the thread pool, then delivers it on
// the window's thread.
winrt::fire_and_forget Load(std::string address, std::uint64_t generation, std::weak_ptr<ArtworkMotionState> weak,
                            dispatching::DispatcherQueue queue, Journal* journal) {
    streams::InMemoryRandomAccessStream stream{nullptr};
    std::string failure;
    try {
        co_await winrt::resume_background();
        streams::IBuffer buffer = Kept(address);
        if (!buffer) {
            winrt::Windows::Web::Http::HttpClient client;
            auto request = client.GetBufferAsync(winrt::Windows::Foundation::Uri(winrt::to_hstring(address)));
            auto timeout = winrt::Windows::System::Threading::ThreadPoolTimer::CreateTimer(
                [request](auto&&) {
                    try {
                        request.Cancel();
                    } catch (...) {
                    }
                },
                kDownloadTimeout);
            buffer = co_await request;
            timeout.Cancel();
            if (buffer && buffer.Length() > kMaxVideoBytes) {
                failure = "the video is larger than 8 MB";
                buffer = nullptr;
            }
            if (buffer) Keep(address, buffer);
        }
        if (buffer) {
            stream = streams::InMemoryRandomAccessStream();
            co_await stream.WriteAsync(buffer);
            stream.Seek(0);
        } else if (failure.empty()) {
            failure = "no answer";
        }
    } catch (const winrt::hresult_error& error) {
        failure = HresultText(error.code().value);
        stream = nullptr;
    } catch (...) {
        failure = "unknown error";
        stream = nullptr;
    }
    TryEnqueueUi(queue, [weak, generation, stream, failure, journal]() noexcept {
        try {
            Deliver(weak, generation, stream, failure, journal);
        } catch (...) {
            // Work queued on Apple Music's thread: nothing may escape it.
        }
    });
}

}  // namespace

bool FollowArtworkMotion(std::shared_ptr<ArtworkMotionState>& state, MotionPlace place,
                         const xaml::DependencyObject& root, const std::string& address, Journal& journal) noexcept {
    try {
        if (address.empty() || !MotionAllowed()) {
            if (state) ReleaseArtworkMotion(state);
            return true;
        }
        if (!state) {
            state = std::make_shared<ArtworkMotionState>();
            state->place = place;
        }
        if (address != state->address) {
            state->address = address;
            state->generation++;
            state->loading = false;
            state->failed = false;
            Remove(*state);  // the last album's video goes; the new one shows from its own first frame
        } else if (state->failed) {
            return true;
        }
        // The frame found before, while Apple keeps it: the main window's tree is too large to walk at each refresh.
        controls::Panel frame = state->frame.get();
        if (!frame || !frame.IsLoaded() || !state->element || state->element.Parent() != frame) {
            frame = root ? FindArtworkFrame(root, place) : nullptr;
        }
        if (!frame) return false;  // not laid out yet: the next refresh looks again
        Place(*state, frame);
        if (!state->loading && !state->player.Source()) {
            state->loading = true;
            Load(state->address, state->generation, state, dispatching::DispatcherQueue::GetForCurrentThread(), &journal);
        }
    } catch (const winrt::hresult_error& error) {
        journal.Write(std::string(TraitsOf(place).label) + ": the animated cover could not be shown (" +
                      HresultText(error.code().value) + ")");
        if (state) state->failed = true;
    } catch (...) {
        if (state) state->failed = true;
    }
    return true;
}

void ReleaseArtworkMotion(std::shared_ptr<ArtworkMotionState>& state) noexcept {
    if (!state) return;
    try {
        state->generation++;
        Remove(*state);
    } catch (...) {
        // The window is going: nothing is left to give back.
    }
    state.reset();
}

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
