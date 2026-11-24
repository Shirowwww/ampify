// Built with AMPIFY_WITH_XAML, like the interface service that calls it.

#if defined(AMPIFY_WITH_XAML)

#include "mini_motion.hpp"
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

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <utility>
#include <vector>

#include "win_common.hpp"
#include "xaml_chrome.hpp"

namespace ampify::win {

namespace {

namespace controls = winrt::Microsoft::UI::Xaml::Controls;
namespace dispatching = winrt::Microsoft::UI::Dispatching;
namespace playback = winrt::Windows::Media::Playback;
namespace streams = winrt::Windows::Storage::Streams;
namespace xaml = winrt::Microsoft::UI::Xaml;

constexpr int kArtworkSearchLimit = 400;  // the artwork sits near the top of the mini player's tree
constexpr auto kDownloadTimeout = std::chrono::seconds(20);
constexpr std::uint32_t kMaxVideoBytes = 8u * 1024 * 1024;  // Apple's 486 px loop is under 2 MB
constexpr std::size_t kKeptVideos = 6;  // the albums played lately, so going back to one does not download it again
constexpr wchar_t kElementName[] = L"AMPIFY_MiniMotion";

}  // namespace

struct MiniMotionState {
    std::string address;  // what plays, or is being loaded
    std::uint64_t generation = 0;  // one more at each new address, so a late download is dropped
    bool loading = false;  // a download of this address is under way
    bool failed = false;   // this address could not be played: not tried again until another one comes
    controls::MediaPlayerElement element{nullptr};  // strong: Ampify's own element, released on this thread
    playback::MediaPlayer player{nullptr};
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

// The frame inside Apple's ArtworkImage#ActualArtwork (its AspectRatioGrid), which Apple sizes and grows into the
// album view; null while the mini player has not laid it out.
controls::Panel FindArtworkFrame(const xaml::DependencyObject& root) {
    std::vector<xaml::DependencyObject> queue{root};
    for (std::size_t i = 0; i < queue.size() && queue.size() < static_cast<std::size_t>(kArtworkSearchLimit); i++) {
        auto element = queue[i].try_as<xaml::FrameworkElement>();
        if (element && element.Name() == L"ActualArtwork") {
            int count = xaml::Media::VisualTreeHelper::GetChildrenCount(element);
            for (int child = 0; child < count; child++) {
                if (auto panel = xaml::Media::VisualTreeHelper::GetChild(element, child).try_as<controls::Panel>()) return panel;
            }
            return nullptr;
        }
        int count = xaml::Media::VisualTreeHelper::GetChildrenCount(queue[i]);
        for (int child = 0; child < count; child++) queue.push_back(xaml::Media::VisualTreeHelper::GetChild(queue[i], child));
    }
    return nullptr;
}

void Remove(MiniMotionState& state) {
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

controls::MediaPlayerElement MakeElement(const playback::MediaPlayer& player) {
    controls::MediaPlayerElement element;
    element.Name(kElementName);
    element.AreTransportControlsEnabled(false);
    element.Stretch(xaml::Media::Stretch::UniformToFill);
    element.IsHitTestVisible(false);  // the click on the artwork stays Apple's (the album view)
    element.Opacity(0);               // until the first frame, so the still cover never blinks
    element.SetMediaPlayer(player);
    return element;
}

// Puts the element in the artwork's frame, above Apple's still cover, when it is not there yet.
bool Place(MiniMotionState& state, const controls::Panel& frame) {
    if (!state.element) {
        state.player = MakePlayer();
        state.element = MakeElement(state.player);
        winrt::weak_ref<controls::MediaPlayerElement> weakElement = winrt::make_weak(state.element);
        dispatching::DispatcherQueue queue = dispatching::DispatcherQueue::GetForCurrentThread();
        state.player.MediaOpened([weakElement, queue](auto&&, auto&&) noexcept {
            TryEnqueueUi(queue, [weakElement]() noexcept {
                try {
                    if (auto element = weakElement.get()) element.Opacity(1);
                } catch (...) {
                }
            });
        });
    }
    if (state.frame.get() == frame && state.element.Parent() == frame) return true;
    if (auto old = state.element.Parent().try_as<controls::Panel>()) {
        std::uint32_t index = 0;
        if (old.Children().IndexOf(state.element, index)) old.Children().RemoveAt(index);
    }
    frame.Children().Append(state.element);
    state.frame = winrt::make_weak(frame);
    return true;
}

// On the mini player's thread: the stream goes to the player if the same address is still wanted there.
void Deliver(const std::weak_ptr<MiniMotionState>& weak, std::uint64_t generation,
             const streams::InMemoryRandomAccessStream& stream, const std::string& failure, Journal* journal) {
    auto state = weak.lock();
    if (!state || state->generation != generation || !state->player) return;
    state->loading = false;
    if (!stream) {
        state->failed = true;
        Remove(*state);
        if (journal) journal->Write("Mini player: the animated cover could not be loaded (" + failure + ")");
        return;
    }
    state->player.Source(winrt::Windows::Media::Core::MediaSource::CreateFromStream(stream, L"video/mp4"));
    if (journal) journal->Write("Mini player: animated cover given to the player over the artwork");
}

// Downloads the video (or takes it from the cache) and copies it into a stream on the thread pool, then delivers it on
// the mini player's thread.
winrt::fire_and_forget Load(std::string address, std::uint64_t generation, std::weak_ptr<MiniMotionState> weak,
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

void FollowMiniMotion(std::shared_ptr<MiniMotionState>& state, const xaml::DependencyObject& root,
                      const std::string& address, Journal& journal) noexcept {
    try {
        if (address.empty() || !MotionAllowed()) {
            if (state) ReleaseMiniMotion(state);
            return;
        }
        if (!state) state = std::make_shared<MiniMotionState>();
        if (address != state->address) {
            state->address = address;
            state->generation++;
            state->loading = false;
            state->failed = false;
            if (state->element) {
                state->element.Opacity(0);  // the last album's video goes until the new one shows
                state->player.Source(nullptr);
            }
        } else if (state->failed) {
            return;
        }
        controls::Panel frame = root ? FindArtworkFrame(root) : nullptr;
        if (!frame) return;  // not laid out yet: the next refresh looks again
        Place(*state, frame);
        if (!state->loading && !state->player.Source()) {
            state->loading = true;
            Load(state->address, state->generation, state, dispatching::DispatcherQueue::GetForCurrentThread(), &journal);
        }
    } catch (const winrt::hresult_error& error) {
        journal.Write("Mini player: the animated cover could not be shown (" + HresultText(error.code().value) + ")");
        if (state) state->failed = true;
    } catch (...) {
        if (state) state->failed = true;
    }
}

void ReleaseMiniMotion(std::shared_ptr<MiniMotionState>& state) noexcept {
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
