#if defined(AMPIFY_WITH_XAML)

#include "album_expand.hpp"
#include "xaml_ending.hpp"

#include <winrt/Microsoft.UI.Composition.h>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Microsoft.UI.Xaml.Automation.Peers.h>
#include <winrt/Microsoft.UI.Xaml.Automation.Provider.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Hosting.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.Numerics.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.System.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <memory>
#include <mutex>
#include <limits>
#include <optional>
#include <thread>
#include <vector>

#include "../core/hover_readout.hpp"
#include "../core/i18n.hpp"
#include "../core/library/album_tracks.hpp"
#include "../core/amp_abi.hpp"
#include "../core/now_playing.hpp"
#include "album_panel.hpp"
#include "amp_item.hpp"
#include "amp_player.hpp"
#include "win_common.hpp"
#include "winrt_abi.hpp"
#include "xaml_chrome.hpp"

#include <windows.h>  // GetKeyState; after the WinRT headers

namespace ampify::win {

namespace {

namespace xaml = winrt::Microsoft::UI::Xaml;
namespace controls = xaml::Controls;
namespace media = xaml::Media;
namespace comp = winrt::Microsoft::UI::Composition;
namespace hosting = xaml::Hosting;
using winrt::Windows::Foundation::Numerics::float3;

constexpr wchar_t kGridClass[] = L"AMP.UI.Controls.GridView";
// The grids of Library > Albums and of Library > Recently Added (grouped by date), in Apple Music 1.1792.
constexpr wchar_t kPageGrid[] = L"FixedGridPageContent";
constexpr wchar_t kGroupedPageGrid[] = L"FixedGroupedGridSectionContent";
constexpr int kTilesLookedAt = 8;  // a grid is the library's if one of its first tiles is a library album
constexpr wchar_t kScrollerName[] = L"ScrollViewer";
constexpr wchar_t kArtworkName[] = L"StaticArtwork";
constexpr wchar_t kSubtitleName[] = L"SubtitleTextBlock";
constexpr char kAlbumKind[] = "eAlbum";
constexpr auto kSearchEvery = std::chrono::seconds(2);
constexpr size_t kSearchLimit = 12000;
constexpr size_t kItemSearchLimit = 200;
constexpr double kGap = 12;        // between the panel and the rows above and below it
constexpr double kTileMargin = 6;  // Apple's GridViewItem margin: the panel lines up with the covers
constexpr auto kSlide = std::chrono::milliseconds(260);
constexpr float kClickSlop = 6;  // a press and release further apart than this (px) are a drag, not a click
constexpr int kColorSample = 48;  // px: the tile's cover is read this small for the album's colours

std::atomic<bool> g_wanted{false};
std::atomic<bool> g_colors{false};

// The library file's tracks, read on a worker thread and kept until the file changes. Never destroyed.
struct Library {
    std::mutex lock;
    std::filesystem::file_time_type stamp{};
    std::shared_ptr<const std::vector<LibraryTrack>> tracks;
    bool loading = false;
    // A file that could not be read is not read again until it changes.
    std::optional<std::filesystem::file_time_type> failedStamp;
};

Library& TheLibrary() {
    static auto* library = new Library();
    return *library;
}

struct Open {
    int index = -1;
    winrt::Windows::Foundation::IUnknown item{nullptr};  // the grid's item at `index`, held: compared by identity
    double rowTop = 0;     // the album's row in the items panel (its tile's layout slot)
    double rowHeight = 0;
    rt::Ref descriptor;  // the album's ContentDescriptor, which library playback takes
    LibraryItemId id;
    AlbumPanelModel model;  // title, subtitle and cover from the tile; the rest from the library
    std::wstring tileArtist;
    std::vector<LibraryTrack> tracks;
    std::string playingKey;  // the song playing when the list was built: another one rebuilds it
    bool waiting = false;  // the library file was still being read
    bool readingColors = false;  // the panel waits for its album's colours before it opens
    xaml::FrameworkElement panel{nullptr};
    double height = 0;
    comp::CompositionPropertySet place{nullptr};  // "Base": the panel's top at a scroll of 0
};

// Interface thread of the main window only; never destroyed: XAML objects are released there, not by a static
// destructor (docs/development/xaml-lessons.md).
struct State {
    Journal* journal = nullptr;
    winrt::weak_ref<controls::GridView> grid;
    bool attached = false;
    bool clickWasEnabled = true;
    winrt::Windows::Foundation::IInspectable pressed{nullptr};  // the handlers, boxed as AddHandler takes them
    winrt::Windows::Foundation::IInspectable released{nullptr};
    winrt::Windows::Foundation::IInspectable keys{nullptr};
    winrt::Windows::Foundation::IInspectable wheel{nullptr};
    // The tile a left press fell on, and where in the window; the click toggled last, to let a double click be.
    winrt::weak_ref<controls::GridViewItem> pressedTile;
    winrt::Windows::Foundation::Point pressedAt{};
    winrt::weak_ref<controls::GridViewItem> clickedTile;
    uint64_t clickedTick = 0;
    controls::ItemsWrapGrid wrap{nullptr};
    controls::ScrollViewer scroller{nullptr};
    controls::Canvas overlay{nullptr};
    winrt::event_token layoutToken{};
    xaml::Thickness wrapMargin{};
    xaml::Vector3Transition slide{nullptr};
    std::chrono::steady_clock::time_point slideUntil{};
    std::chrono::steady_clock::time_point nextSearch{};
    Open open;
};

State& TheState() {
    static auto* state = new State();
    return *state;
}

void Note(const std::string& line) {
    if (Journal* journal = TheState().journal) journal->Write(line);
}

template <typename Match>
void Walk(const xaml::DependencyObject& root, size_t limit, Match&& match) {
    std::vector<xaml::DependencyObject> queue{root};
    for (size_t i = 0; i < queue.size() && queue.size() < limit; i++) {
        if (match(queue[i])) return;
        int count = media::VisualTreeHelper::GetChildrenCount(queue[i]);
        for (int child = 0; child < count; child++) queue.push_back(media::VisualTreeHelper::GetChild(queue[i], child));
    }
}

template <typename T>
T FindNamed(const xaml::DependencyObject& root, const wchar_t* name, size_t limit) {
    T found{nullptr};
    Walk(root, limit, [&](const xaml::DependencyObject& object) {
        auto typed = object.try_as<T>();
        if (typed && typed.Name() == name) found = typed;
        return static_cast<bool>(found);
    });
    return found;
}

rt::Ref ModelOf(const controls::GridViewItem& item) {
    auto content = item ? item.Content() : nullptr;
    if (!content) return {};
    return rt::Ref(winrt::detach_abi(content.as<winrt::Windows::Foundation::IUnknown>()));
}

std::optional<LibraryItemId> AlbumIdOf(const rt::Ref& descriptor) {
    std::optional<LibraryItemId> id = ParseLibraryItemId(DescriptorIdentifier(descriptor));
    if (!id || id->kind != kAlbumKind) return std::nullopt;
    return id;
}

// ---- The library file ----

void RefreshOpen();

// The tracks as last read, and a new reading started when the file changed since; `waiting` while one runs.
std::shared_ptr<const std::vector<LibraryTrack>> LibraryTracks(bool& waiting) {
    Library& library = TheLibrary();
    const std::filesystem::path file = AppleLibraryFile();
    std::error_code error;
    const std::filesystem::file_time_type stamp = std::filesystem::last_write_time(file, error);
    std::lock_guard guard(library.lock);
    waiting = library.loading;
    if (error || (library.tracks && library.stamp == stamp) || library.loading || library.failedStamp == stamp) {
        return library.tracks;
    }
    library.loading = waiting = true;
    auto queue = winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();
    std::thread([file, stamp, queue] {
        MusicDbResult result = ReadMusicDb(file);
        Library& shared = TheLibrary();
        {
            std::lock_guard done(shared.lock);
            shared.loading = false;
            if (result.error.empty()) {
                shared.tracks = std::make_shared<const std::vector<LibraryTrack>>(std::move(result.tracks));
                shared.stamp = stamp;
                shared.failedStamp.reset();
            } else {
                shared.failedStamp = stamp;
            }
        }
        if (!result.error.empty()) Note("Album click: the library file could not be read: " + result.error);
        if (queue) TryEnqueueUi(queue, [] { RefreshOpen(); });
    }).detach();
    return library.tracks;
}

// ---- The panel ----

std::wstring Wide(const std::string& text) { return FromUtf8(text); }

std::wstring Subtitle(const Open& open) {
    if (open.tracks.empty()) return open.tileArtist;
    const LibraryTrack& first = open.tracks.front();
    std::wstring line = Wide(AlbumArtistOf(open.tracks, ToUtf8(open.tileArtist)));
    if (!first.genre.empty()) line += L" · " + Wide(first.genre);
    if (first.year) line += L" · " + std::to_wstring(first.year);
    return line;
}

std::wstring Footer(const Open& open) {
    if (open.tracks.empty()) return open.waiting ? L"…" : Wide(i18n::Text("album.empty"));
    double total = 0;
    for (const LibraryTrack& track : open.tracks) total += track.durationMs;
    const std::string duration = ClockText(total);
    if (open.tracks.size() == 1) return Wide(i18n::Text("album.song", {{"duration", duration}}));
    return Wide(i18n::Text("album.songs", {{"count", std::to_string(open.tracks.size())}, {"duration", duration}}));
}

void FillModel(Open& open) {
    open.model.subtitle = Subtitle(open);
    open.model.footer = Footer(open);
    open.model.tracks.clear();
    const std::string& albumArtist = open.tracks.empty() ? std::string()
                                     : open.tracks.front().albumArtist.empty() ? open.tracks.front().artist
                                                                               : open.tracks.front().albumArtist;
    const bool discs = DiscCount(open.tracks) > 1;
    const NowPlaying now = CurrentNowPlaying();
    open.playingKey = now.trackKey;
    const std::optional<size_t> playing = PlayingTrackIndex(open.tracks, now.title, now.album);
    uint32_t lastDisc = 0;
    for (const LibraryTrack& track : open.tracks) {
        AlbumPanelTrack row;
        row.playing = playing && *playing == open.model.tracks.size();
        if (discs && DiscOf(track) != lastDisc) {
            lastDisc = DiscOf(track);
            row.section = Wide(i18n::Text("album.disc", {{"number", std::to_string(lastDisc)}}));
        }
        if (track.trackNumber) row.number = std::to_wstring(track.trackNumber);
        row.title = Wide(track.title);
        if (track.artist != albumArtist) row.artist = Wide(track.artist);
        if (track.durationMs) row.duration = Wide(ClockText(track.durationMs));
        open.model.tracks.push_back(std::move(row));
    }
}

void Collapse();
void Reflow();
void Reveal(State& state);
void OpenPage(int index);
void Play(int track, bool shuffle);
void Queue(int track, bool next);

int Columns(const State& state, double& itemWidth, double& itemHeight) {
    itemWidth = state.wrap.ItemWidth();
    itemHeight = state.wrap.ItemHeight();
    if (!std::isfinite(itemWidth) || !std::isfinite(itemHeight)) {
        for (const xaml::UIElement& child : state.wrap.Children()) {
            auto element = child.try_as<controls::GridViewItem>();  // not a group's header, which is wider
            if (!element) continue;
            winrt::Windows::Foundation::Rect slot = controls::Primitives::LayoutInformation::GetLayoutSlot(element);
            if (!std::isfinite(itemWidth)) itemWidth = slot.Width;
            if (!std::isfinite(itemHeight)) itemHeight = slot.Height;
            break;
        }
    }
    if (!(itemWidth > 0) || !(itemHeight > 0)) return 0;
    return std::max(1, static_cast<int>(std::floor(state.wrap.ActualWidth() / itemWidth + 0.001)));
}

// Follows the grid's scroll on the compositor and, for a panel that opens, plays its reveal and fade.
void Animate(State& state, Open& open, bool animate) {
    comp::Visual visual = hosting::ElementCompositionPreview::GetElementVisual(open.panel);
    comp::Compositor compositor = visual.Compositor();
    hosting::ElementCompositionPreview::SetIsTranslationEnabled(open.panel, true);
    open.place = compositor.CreatePropertySet();
    open.place.InsertScalar(L"Base", 0.0f);
    comp::ExpressionAnimation follow = compositor.CreateExpressionAnimation(L"Vector3(0, place.Base + scroll.Translation.Y, 0)");
    follow.SetReferenceParameter(L"place", open.place);
    follow.SetReferenceParameter(L"scroll", hosting::ElementCompositionPreview::GetScrollViewerManipulationPropertySet(state.scroller));
    visual.StartAnimation(L"Translation", follow);

    if (!animate) return;  // the same panel rebuilt with its tracks: already shown
    comp::InsetClip clip = compositor.CreateInsetClip();
    visual.Clip(clip);
    comp::ScalarKeyFrameAnimation reveal = compositor.CreateScalarKeyFrameAnimation();
    reveal.InsertKeyFrame(0.0f, static_cast<float>(open.height));
    reveal.InsertKeyFrame(1.0f, 0.0f);
    reveal.Duration(kSlide);
    clip.StartAnimation(L"BottomInset", reveal);
    comp::ScalarKeyFrameAnimation fade = compositor.CreateScalarKeyFrameAnimation();
    fade.InsertKeyFrame(0.0f, 0.0f);
    fade.InsertKeyFrame(1.0f, 1.0f);
    fade.Duration(kSlide);
    visual.StartAnimation(L"Opacity", fade);
}

void BuildPanel(State& state, bool animate) {
    Open& open = state.open;
    double itemWidth = 0, itemHeight = 0;
    const int columns = Columns(state, itemWidth, itemHeight);
    if (columns == 0) return;
    const double width = columns * itemWidth - 2 * kTileMargin;
    auto grid = state.grid.get();
    if (!grid) return;
    FillModel(open);
    // The panel's handlers run inside Apple Music's XAML frame: nothing may escape them.
    AlbumPanelActions actions;
    actions.play = [](int track, bool shuffle) {
        try {
            Play(track, shuffle);
        } catch (...) {
        }
    };
    actions.queue = [](int track, bool next) {
        try {
            Queue(track, next);
        } catch (...) {
        }
    };
    actions.open = [index = open.index] {
        try {
            OpenPage(index);
        } catch (...) {
        }
    };
    actions.close = [] {
        try {
            Collapse();
        } catch (...) {
        }
    };
    xaml::FrameworkElement panel = MakeAlbumPanel(open.model, actions, grid.Foreground(), width);
    panel.Measure({static_cast<float>(width), std::numeric_limits<float>::infinity()});
    open.height = panel.DesiredSize().Height;
    state.overlay.Children().Clear();
    state.overlay.Children().Append(panel);
    open.panel = panel;
    if (animate) state.slideUntil = std::chrono::steady_clock::now() + kSlide;
    Animate(state, open, animate);
}

// The library's reading ended: the open album gets its tracks. Runs from the worker's TryEnqueue.
void RefreshOpen() {
    try {
        State& state = TheState();
        if (!state.attached || !state.grid.get() || state.open.index < 0 || !state.open.waiting) return;
        bool waiting = false;
        auto tracks = LibraryTracks(waiting);
        state.open.waiting = waiting;
        if (tracks) state.open.tracks = TracksOfAlbum(*tracks, state.open.id.persistent);
        if (state.open.readingColors) return;  // the panel opens with its colours, tracks included
        BuildPanel(state, false);
        Reflow();
        Reveal(state);
    } catch (...) {
        // A queued call runs inside Apple Music's XAML frame: nothing may escape it.
    }
}

// ---- The gap ----

void Shift(State& state, const xaml::UIElement& child, double by) {
    float3 now = child.Translation();
    if (std::fabs(now.y - by) < 0.5 && now.x == 0 && now.z == 0) return;
    child.TranslationTransition(std::chrono::steady_clock::now() < state.slideUntil ? state.slide : nullptr);
    child.Translation({0, static_cast<float>(by), 0});
}

void SetWrapMargin(State& state, double extra) {
    xaml::Thickness wanted = state.wrapMargin;
    wanted.Bottom += extra;
    xaml::Thickness now = state.wrap.Margin();
    if (std::fabs(now.Bottom - wanted.Bottom) > 0.5) state.wrap.Margin(wanted);
}

bool SameItem(const controls::GridView& grid, const Open& open) {
    auto items = grid.Items();
    if (!open.item || open.index >= static_cast<int>(items.Size())) return false;
    auto now = items.GetAt(static_cast<uint32_t>(open.index));
    return now && winrt::get_abi(now.as<winrt::Windows::Foundation::IUnknown>()) == winrt::get_abi(open.item);
}

winrt::Windows::Foundation::Rect SlotOf(const xaml::UIElement& element) {
    auto framework = element.try_as<xaml::FrameworkElement>();
    return framework ? controls::Primitives::LayoutInformation::GetLayoutSlot(framework)
                     : winrt::Windows::Foundation::Rect{0, 0, 0, 0};
}

// The open album's row in the items panel, read from its tile's place while the tile is realized; kept from the
// last reading when the grid has scrolled it away.
void FollowRow(const controls::GridView& grid, Open& open) {
    auto tile = grid.ContainerFromIndex(open.index).try_as<xaml::UIElement>();
    if (!tile) return;
    winrt::Windows::Foundation::Rect slot = SlotOf(tile);
    if (slot.Height <= 0) return;
    open.rowTop = slot.Y;
    open.rowHeight = slot.Height;
}

// Called after each layout pass: whatever sits lower than the open album's row (tiles, and the headers of a grouped
// grid such as Recently Added) moves down by the panel's height, the scroll range grows by as much, and the panel
// stands under the row whatever the window's width. Places come from the layout, not from indexes: a grouped grid
// starts a new row at each group.
void Reflow() {
    State& state = TheState();
    auto grid = state.grid.get();
    if (!state.attached || !grid || !state.wrap) return;
    Open& open = state.open;
    if (open.index >= 0 && !SameItem(grid, open)) return Collapse();  // Apple sorted or changed the grid
    if (open.index >= 0) FollowRow(grid, open);
    const bool shown = open.index >= 0 && open.panel && open.rowHeight > 0;
    const double gap = shown ? open.height + 2 * kGap : 0;
    for (const xaml::UIElement& child : state.wrap.Children()) {
        Shift(state, child, shown && SlotOf(child).Y > open.rowTop + 1 ? gap : 0);
    }
    SetWrapMargin(state, gap);
    if (!shown) return;
    const float anchor = static_cast<float>(open.rowTop + open.rowHeight + kGap);
    winrt::Windows::Foundation::Point at =
        state.wrap.TransformToVisual(state.overlay).TransformPoint({static_cast<float>(kTileMargin), anchor});
    const double left = at.X;
    if (std::fabs(controls::Canvas::GetLeft(open.panel) - left) > 0.5) controls::Canvas::SetLeft(open.panel, left);
    // TransformToVisual counts the current scroll; the expression adds it back as the grid moves.
    open.place.InsertScalar(L"Base", static_cast<float>(at.Y + state.scroller.VerticalOffset()));
}

// Brings the panel into view when it opened under the window's bottom edge.
void Reveal(State& state) {
    Open& open = state.open;
    if (!open.panel || open.rowHeight <= 0) return;
    winrt::Windows::Foundation::Point top = state.wrap.TransformToVisual(state.scroller).TransformPoint({0, 0});
    const double rowTop = top.Y + open.rowTop;
    const double bottom = rowTop + open.rowHeight + open.height + 2 * kGap;
    const double overflow = bottom - state.scroller.ViewportHeight();
    if (overflow <= 0) return;
    const double by = std::min(overflow, std::max(0.0, rowTop));  // never past the album's own row
    state.scroller.ChangeView(nullptr, winrt::Windows::Foundation::IReference<double>(state.scroller.VerticalOffset() + by),
                              nullptr);
}

void Collapse() {
    State& state = TheState();
    if (state.open.index < 0) return;
    state.open = Open{};
    if (state.overlay) state.overlay.Children().Clear();
    state.slideUntil = std::chrono::steady_clock::now() + kSlide;
    Reflow();
}

// The panel opens and the rows below make room for it.
void Show(State& state) {
    BuildPanel(state, true);
    Reflow();
    Reveal(state);
}

// apple.albumClick = colors: the tile's cover is read small and the panel opens in the album's colours, a frame or two
// later. If the reading fails, it opens in the grid's colours. The album must still be the open one by then.
void ShowInColors(State& state, const controls::Image& cover) {
    state.open.readingColors = true;
    const int index = state.open.index;
    const winrt::Windows::Foundation::IUnknown item = state.open.item;
    auto finish = [queue = cover.DispatcherQueue(), index, item](std::optional<AlbumColors> colors) {
        TryEnqueueUi(queue, [index, item, colors] {
            State& state = TheState();
            Open& open = state.open;
            if (open.index != index || open.item != item || !open.readingColors) return;
            open.readingColors = false;
            open.model.colors = colors;
            Show(state);
        });
    };
    try {
        using winrt::Windows::Foundation::AsyncStatus;
        xaml::Media::Imaging::RenderTargetBitmap target;
        auto rendering = target.RenderAsync(cover, kColorSample, kColorSample);
        rendering.Completed([target, finish](auto const&, AsyncStatus status) {
            if (status != AsyncStatus::Completed) return finish(std::nullopt);
            target.GetPixelsAsync().Completed([target, finish](auto const& operation, AsyncStatus read) {
                try {
                    if (read != AsyncStatus::Completed) return finish(std::nullopt);
                    auto pixels = operation.GetResults();
                    std::optional<ExtractedColors> colors =
                        ExtractColorsFromBgra(pixels.data(), pixels.Length(), static_cast<size_t>(target.PixelWidth()),
                                              static_cast<size_t>(target.PixelHeight()));
                    finish(colors ? std::optional<AlbumColors>(AlbumPanelColors(*colors)) : std::nullopt);
                } catch (...) {
                    finish(std::nullopt);
                }
            });
        });
    } catch (...) {
        finish(std::nullopt);
    }
}

void Expand(const controls::GridViewItem& item, int index) {
    State& state = TheState();
    rt::Ref model = ModelOf(item);
    rt::Ref descriptor = ContentDescriptorOf(model);
    std::optional<LibraryItemId> id = AlbumIdOf(descriptor);
    if (!id) return OpenPage(index);  // a playlist or a catalog album: Apple's page, as before
    Open open;
    open.index = index;
    winrt::Windows::Foundation::Rect slot = SlotOf(item);
    open.rowTop = slot.Y;
    open.rowHeight = slot.Height;
    if (auto content = item.Content()) open.item = content.as<winrt::Windows::Foundation::IUnknown>();
    open.descriptor = descriptor;
    open.id = *id;
    rt::Ref titled;
    if (rt::Query(model, rt::GuidOf(amp::kIidTitledComponent), titled)) {
        open.model.title = Wide(rt::ReadString(titled, amp::slot::kTitledTitle));
    }
    controls::Image cover = FindNamed<controls::Image>(item, kArtworkName, kItemSearchLimit);
    if (cover) open.model.artwork = cover.Source();
    if (auto subtitle = FindNamed<controls::TextBlock>(item, kSubtitleName, kItemSearchLimit)) {
        open.tileArtist = subtitle.Text().c_str();
    }
    bool waiting = false;
    auto tracks = LibraryTracks(waiting);
    open.waiting = waiting;
    if (tracks) open.tracks = TracksOfAlbum(*tracks, id->persistent);
    state.open = std::move(open);
    if (g_colors.load() && cover && cover.Source()) return ShowInColors(state, cover);
    Show(state);
}

// Apple's own click on a tile: its page. The grid's ItemClick is given back for the time of the automation peer's
// Invoke, which raises it, then held back again. The peer offers Invoke only while the grid's click is on.
void OpenPage(int index) {
    State& state = TheState();
    auto grid = state.grid.get();
    if (!grid) return;
    Collapse();
    auto item = grid.ContainerFromIndex(index).try_as<xaml::UIElement>();
    if (!item) return;
    grid.IsItemClickEnabled(true);
    auto peer = xaml::Automation::Peers::FrameworkElementAutomationPeer::CreatePeerForElement(item);
    auto pattern = peer ? peer.GetPattern(xaml::Automation::Peers::PatternInterface::Invoke) : nullptr;
    auto invoke = pattern ? pattern.try_as<xaml::Automation::Provider::IInvokeProvider>() : nullptr;
    if (invoke) invoke.Invoke();
    else Note("Album click: the album's page cannot be opened (no Invoke on the tile)");
    auto weak = state.grid;
    TryEnqueueUi(grid.DispatcherQueue(), winrt::Microsoft::UI::Dispatching::DispatcherQueuePriority::Low, [weak] {
        if (auto again = weak.get(); again && g_wanted.load()) again.IsItemClickEnabled(false);
    });
}

void Play(int track, bool shuffle) {
    State& state = TheState();
    Open& open = state.open;
    if (open.index < 0 || !open.descriptor) return;
    std::string failure;
    rt::Ref start;
    if (track >= 0 && static_cast<size_t>(track) < open.tracks.size()) {
        start = DescriptorFromJson(SongDescriptorJson(open.id.database, open.tracks[track].persistentId), failure);
        if (!start) return Note("Album click: the track cannot be played: " + failure);
    }
    if (!StartLibraryPlayback(open.descriptor, start, shuffle, failure)) Note("Album click: Apple did not play the album: " + failure);
}

// Play Next or Play Later from a track's right-click menu (issue #7).
void Queue(int track, bool next) {
    Open& open = TheState().open;
    if (open.index < 0 || track < 0 || static_cast<size_t>(track) >= open.tracks.size()) return;
    std::string failure;
    rt::Ref song = DescriptorFromJson(SongDescriptorJson(open.id.database, open.tracks[track].persistentId), failure);
    if (!song) return Note("Album click: the track cannot be queued: " + failure);
    if (!QueueLibrarySong(song, next, failure)) Note("Album click: Apple did not queue the track: " + failure);
}

// The panel lies over the grid, not in it: the wheel over it scrolls the grid as it does anywhere else (issue #7).
void OnWheel(const winrt::Windows::Foundation::IInspectable&, const xaml::Input::PointerRoutedEventArgs& args) {
    try {
        State& state = TheState();
        if (!state.scroller) return;
        const int32_t delta = args.GetCurrentPoint(nullptr).Properties().MouseWheelDelta();
        if (delta == 0) return;
        state.scroller.ChangeView(nullptr, state.scroller.VerticalOffset() - delta, nullptr);
        args.Handled(true);
    } catch (...) {
        // A handler runs inside Apple Music's XAML frame: nothing may escape it.
    }
}

// ---- The grid ----

bool ModifierDown() { return (GetKeyState(VK_CONTROL) & 0x8000) || (GetKeyState(VK_SHIFT) & 0x8000); }

// The tile under a click, unless the click was on one of Apple's buttons of the tile (play, more, favourite).
controls::GridViewItem TileOf(const winrt::Windows::Foundation::IInspectable& source, const controls::GridView& grid) {
    auto at = source.try_as<xaml::DependencyObject>();
    while (at && at != grid) {
        if (at.try_as<controls::Primitives::ButtonBase>()) return nullptr;
        if (auto item = at.try_as<controls::GridViewItem>()) return item;
        at = media::VisualTreeHelper::GetParent(at);
    }
    return nullptr;
}

void Toggle(const controls::GridViewItem& item) {
    State& state = TheState();
    auto grid = state.grid.get();
    if (!grid || !item) return;
    const int index = grid.IndexFromContainer(item);
    if (index < 0) return;
    if (index == state.open.index) return Collapse();
    Expand(item, index);
}

// A click is a left press and release on a tile, not a Tapped: a tile partly out of view scrolls in under the
// pressed pointer, and WinUI then raises no Tapped. Where the pointer stands in the window tells a click from a drag.
void OnPressed(const winrt::Windows::Foundation::IInspectable&, const xaml::Input::PointerRoutedEventArgs& args) {
    try {
        State& state = TheState();
        state.pressedTile = nullptr;
        auto grid = state.grid.get();
        if (!grid || !g_wanted.load() || ModifierDown()) return;  // Ctrl or Shift: Apple's multiple selection
        auto point = args.GetCurrentPoint(nullptr);
        if (!point.Properties().IsLeftButtonPressed()) return;
        auto tile = TileOf(args.OriginalSource(), grid);
        if (!tile) return;
        state.pressedTile = winrt::make_weak(tile);
        state.pressedAt = point.Position();
    } catch (...) {
        // A handler runs inside Apple Music's XAML frame: nothing may escape it.
    }
}

void OnReleased(const winrt::Windows::Foundation::IInspectable&, const xaml::Input::PointerRoutedEventArgs& args) {
    try {
        State& state = TheState();
        auto tile = state.pressedTile.get();
        state.pressedTile = nullptr;
        if (!tile || !g_wanted.load() || ModifierDown()) return;
        winrt::Windows::Foundation::Point at = args.GetCurrentPoint(nullptr).Position();
        if (std::hypot(at.X - state.pressedAt.X, at.Y - state.pressedAt.Y) > kClickSlop) return;
        const uint64_t now = GetTickCount64();
        if (tile == state.clickedTile.get() && now - state.clickedTick < GetDoubleClickTime()) return;  // its second click
        state.clickedTile = winrt::make_weak(tile);
        state.clickedTick = now;
        Toggle(tile);
    } catch (...) {
    }
}

void OnKey(const winrt::Windows::Foundation::IInspectable&, const xaml::Input::KeyRoutedEventArgs& args) {
    try {
        State& state = TheState();
        auto grid = state.grid.get();
        if (!grid || !g_wanted.load()) return;
        if (args.Key() == winrt::Windows::System::VirtualKey::Escape && state.open.index >= 0) {
            const int index = state.open.index;
            Collapse();
            // The focus was maybe in the panel, now gone: it goes back to the album's tile.
            if (auto tile = grid.ContainerFromIndex(index).try_as<xaml::Controls::Control>()) {
                tile.Focus(xaml::FocusState::Keyboard);
            }
            args.Handled(true);
        } else if (args.Key() == winrt::Windows::System::VirtualKey::Enter) {
            if (auto item = args.OriginalSource().try_as<controls::GridViewItem>()) {
                Toggle(item);
                args.Handled(true);
            }
        }
    } catch (...) {
    }
}

bool LibraryAlbumGrid(const controls::GridView& grid) {
    for (int i = 0; i < kTilesLookedAt; i++) {
        auto item = grid.ContainerFromIndex(i).try_as<controls::GridViewItem>();
        if (!item) return false;
        if (AlbumIdOf(ContentDescriptorOf(ModelOf(item)))) return true;
    }
    return false;
}

controls::GridView FindGrid() {
    xaml::Window window = AppWindowOf(WindowRole::Main);
    if (!window || !window.Content()) return nullptr;
    controls::GridView found{nullptr};
    Walk(window.Content(), kSearchLimit, [&](const xaml::DependencyObject& object) {
        auto page = object.try_as<controls::Grid>();
        if (!page || (page.Name() != kPageGrid && page.Name() != kGroupedPageGrid)) return false;
        int count = media::VisualTreeHelper::GetChildrenCount(page);
        for (int i = 0; i < count && !found; i++) {
            auto grid = media::VisualTreeHelper::GetChild(page, i).try_as<controls::GridView>();
            if (grid && winrt::get_class_name(grid) == kGridClass && LibraryAlbumGrid(grid)) found = grid;
        }
        return static_cast<bool>(found);
    });
    return found;
}

void Detach() {
    State& state = TheState();
    if (!state.attached) return;
    state.open = Open{};
    if (auto grid = state.grid.get()) {
        grid.IsItemClickEnabled(state.clickWasEnabled);
        grid.RemoveHandler(xaml::UIElement::PointerPressedEvent(), state.pressed);
        grid.RemoveHandler(xaml::UIElement::PointerReleasedEvent(), state.released);
        grid.RemoveHandler(xaml::UIElement::KeyDownEvent(), state.keys);
    }
    if (state.wrap) {
        state.wrap.LayoutUpdated(state.layoutToken);
        for (const xaml::UIElement& child : state.wrap.Children()) {
            child.TranslationTransition(nullptr);
            child.Translation({0, 0, 0});
        }
        state.wrap.Margin(state.wrapMargin);
    }
    if (state.overlay) {
        if (auto parent = media::VisualTreeHelper::GetParent(state.overlay).try_as<controls::Panel>()) {
            uint32_t at = 0;
            if (parent.Children().IndexOf(state.overlay, at)) parent.Children().RemoveAt(at);
        }
    }
    state.wrap = nullptr;
    state.scroller = nullptr;
    state.overlay = nullptr;
    state.grid = nullptr;
    state.attached = false;
    if (!g_wanted.load()) Note("Album click: the library's albums open their page again");
}

bool Attach(const controls::GridView& grid) {
    State& state = TheState();
    auto parent = media::VisualTreeHelper::GetParent(grid).try_as<controls::Panel>();
    auto wrap = grid.ItemsPanelRoot().try_as<controls::ItemsWrapGrid>();
    auto scroller = FindNamed<controls::ScrollViewer>(grid, kScrollerName, kItemSearchLimit);
    uint32_t at = 0;
    if (!parent || !wrap || !scroller || !parent.Children().IndexOf(grid, at)) return false;
    state.overlay = controls::Canvas();  // no background: clicks beside the panel reach the grid
    controls::Grid::SetRow(state.overlay, controls::Grid::GetRow(grid));
    controls::Grid::SetColumn(state.overlay, controls::Grid::GetColumn(grid));
    controls::Grid::SetRowSpan(state.overlay, controls::Grid::GetRowSpan(grid));
    controls::Grid::SetColumnSpan(state.overlay, controls::Grid::GetColumnSpan(grid));
    parent.Children().InsertAt(at + 1, state.overlay);  // under what Apple draws above the grid
    state.grid = winrt::make_weak(grid);
    state.wrap = wrap;
    state.scroller = scroller;
    state.wrapMargin = wrap.Margin();
    state.clickWasEnabled = grid.IsItemClickEnabled();
    grid.IsItemClickEnabled(false);
    state.pressed = winrt::box_value(xaml::Input::PointerEventHandler(&OnPressed));
    state.released = winrt::box_value(xaml::Input::PointerEventHandler(&OnReleased));
    state.keys = winrt::box_value(xaml::Input::KeyEventHandler(&OnKey));
    grid.AddHandler(xaml::UIElement::PointerPressedEvent(), state.pressed, true);
    grid.AddHandler(xaml::UIElement::PointerReleasedEvent(), state.released, true);
    grid.AddHandler(xaml::UIElement::KeyDownEvent(), state.keys, true);
    state.overlay.AddHandler(xaml::UIElement::KeyDownEvent(), state.keys, true);  // Escape from the panel's buttons
    state.wheel = winrt::box_value(xaml::Input::PointerEventHandler(&OnWheel));
    state.overlay.AddHandler(xaml::UIElement::PointerWheelChangedEvent(), state.wheel, true);
    state.layoutToken = wrap.LayoutUpdated([](auto&&, auto&&) {
        try {
            Reflow();
        } catch (...) {
        }
    });
    if (!state.slide) {
        state.slide = xaml::Vector3Transition();
        state.slide.Duration(kSlide);
    }
    state.attached = true;
    Note("Album click: the library's albums open their tracks in the grid");
    return true;
}

}  // namespace

void SetAlbumExpand(bool expand, bool colors) noexcept {
    g_colors.store(colors);
    g_wanted.store(expand);
}

void KeepAlbumExpand(Journal& journal) noexcept {
    try {
        State& state = TheState();
        state.journal = &journal;
        auto grid = state.grid.get();
        if (state.attached && (!g_wanted.load() || !grid || !grid.IsLoaded())) Detach();
        // Another song plays: the open list moves its speaker to it.
        if (state.attached && state.open.index >= 0 && state.open.panel &&
            CurrentNowPlaying().trackKey != state.open.playingKey) {
            state.open.playingKey = CurrentNowPlaying().trackKey;
            BuildPanel(state, false);
            Reflow();
        }
        if (!g_wanted.load() || state.attached) return;
        const auto now = std::chrono::steady_clock::now();
        if (now < state.nextSearch) return;
        state.nextSearch = now + kSearchEvery;
        if (controls::GridView found = FindGrid()) Attach(found);
    } catch (...) {
        // Nothing may escape into Apple Music's XAML frame.
    }
}

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
