#include "lyrics_fill.hpp"

#if defined(AMPIFY_WITH_XAML)

#include <winrt/Microsoft.UI.Composition.h>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Hosting.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.Numerics.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "../core/lyrics_fill.hpp"
#include "../core/now_playing.hpp"
#include "xaml_chrome.hpp"

#include <windows.h>  // last: its macros would rename members of the projection headers above

namespace ampify::win {

namespace {

namespace xaml = winrt::Microsoft::UI::Xaml;
namespace controls = xaml::Controls;
namespace media = xaml::Media;
namespace comp = winrt::Microsoft::UI::Composition;
namespace dispatching = winrt::Microsoft::UI::Dispatching;

constexpr auto kTick = std::chrono::milliseconds(100);
// The trees are walked again often only while something is missing or the song just changed: each walk is a few ms.
constexpr auto kHostSearchEvery = std::chrono::seconds(1);   // while Apple builds a new song's lines
constexpr auto kHostCheckEvery = std::chrono::seconds(15);   // once they stand
constexpr auto kSongSettles = std::chrono::seconds(5);
constexpr auto kViewSearchEvery = std::chrono::seconds(3);   // while no lyrics view shows
constexpr auto kViewCheckEvery = std::chrono::seconds(15);   // while one does
constexpr std::size_t kViewSearchLimit = 8000;  // elements walked in a window to find its lyrics views
constexpr std::size_t kHostSearchLimit = 4000;  // elements walked in a lyrics view to find its hosts
constexpr int kMaxWrapperDepth = 3;             // containers holding a single container, gone through
constexpr int64_t kDriftMs = 60;                // the compositor's edge is moved back on the song when further off
constexpr int64_t kRetryMs = 500;               // a host whose rows matched no line is looked at again after this
constexpr int64_t kIdleMs = 300;                // a host far from the line sung, found idle, is read again after this
constexpr int kHotBefore = 4;                   // hosts read at every tick around the last bound one (main and
constexpr int kHotAfter = 8;                    // background hosts alternate: about two lines before, four after)
constexpr int kLineSearchDepth = 12;            // parents walked up from a host to its line
constexpr wchar_t kLineClass[] = L"AMP.UI.Views.KaraokeLyricsLine";
constexpr double kAfterLineMs = 30000;          // the edge's animation outlasts its line: it never ends while shown
constexpr wchar_t kViewClass[] = L"AMP.UI.Views.KaraokeLyricsView";
constexpr wchar_t kMainHost[] = L"WordByWordHost";
constexpr wchar_t kBackgroundHost[] = L"BackgroundWordByWordHost";
constexpr wchar_t kEdge[] = L"Edge";

std::atomic<bool> g_smooth{true};
std::atomic<Journal*> g_journal{nullptr};

void Note(const std::string& line) {
    if (Journal* journal = g_journal.load()) journal->Write("Lyrics fill: " + line);
}

int64_t Tick() { return static_cast<int64_t>(GetTickCount64()); }

// One row of a line being sung: Apple's gradient behind its mask, and Ampify's in its place.
struct Row {
    comp::SpriteVisual sprite{nullptr};
    comp::CompositionMaskBrush mask{nullptr};
    comp::CompositionLinearGradientBrush apple{nullptr};
    comp::CompositionLinearGradientBrush ours{nullptr};
    comp::CompositionPropertySet edge{nullptr};
    comp::AnimationController controller{nullptr};
    int64_t lineStartMs = 0;
    double durationMs = 1;
    int64_t syncedPositionMs = 0;  // the song's position the animation was last put at, and when
    int64_t syncedTick = 0;
    bool running = false;
};

struct Host {
    winrt::weak_ref<controls::Border> border;
    bool background = false;
    void* lineKey = nullptr;  // the KaraokeLyricsLine the host stands in, by identity: pairs main and background hosts
    std::vector<Row> rows;    // bound while Apple sings the host's line, empty otherwise
    std::optional<size_t> line;  // the line of the song the rows are bound to
    int64_t lineStartMs = 0;
    int64_t lineEndMs = 0;
    int64_t retryAt = 0;  // after rows no line matched, the next try
};

// What one of Apple's rows holds: the sprite that shows it, its mask (the words, drawn into a visual surface) and the
// gradient behind it, with the word visuals and where they lie.
struct FoundRow {
    comp::SpriteVisual sprite{nullptr};
    comp::CompositionMaskBrush mask{nullptr};
    comp::CompositionLinearGradientBrush source{nullptr};
    std::vector<WordSpan> spans;
};

comp::ContainerVisual PastWrappers(comp::ContainerVisual layer) {
    for (int depth = 0; layer && depth < kMaxWrapperDepth && layer.Children().Count() == 1; ++depth) {
        auto only = layer.Children().First().Current().try_as<comp::ContainerVisual>();
        if (!only) break;
        layer = only;
    }
    return layer;
}

// A word's visual may have no size of its own, only letters inside it: its width then spans them.
float WidthOf(const comp::Visual& visual) {
    float width = visual.Size().x;
    auto container = visual.try_as<comp::ContainerVisual>();
    if (width > 0 || !container) return width;
    for (const comp::Visual& child : container.Children()) width = std::max(width, child.Offset().x + child.Size().x);
    return width;
}

// The words drawn into the row's surface, as parts of the sprite's width.
std::vector<WordSpan> SpansOf(const comp::CompositionVisualSurface& surface, float rowWidth) {
    std::vector<WordSpan> spans;
    auto source = surface.SourceVisual().try_as<comp::ContainerVisual>();
    if (!source || rowWidth <= 0) return spans;
    float shift = -surface.SourceOffset().x;
    comp::ContainerVisual words = source;
    for (int depth = 0; depth < kMaxWrapperDepth && words.Children().Count() == 1; ++depth) {
        auto only = words.Children().First().Current().try_as<comp::ContainerVisual>();
        if (!only || only.try_as<comp::SpriteVisual>()) break;
        shift += only.Offset().x;
        words = only;
    }
    for (const comp::Visual& word : words.Children()) {
        double from = (shift + word.Offset().x) / rowWidth;
        spans.push_back({from, from + WidthOf(word) / rowWidth});
    }
    return spans;
}

// The rows of a host Apple is singing now, top to bottom; none while it draws the line plainly.
std::vector<FoundRow> RowsOf(const controls::Border& host) {
    std::vector<FoundRow> rows;
    auto root = xaml::Hosting::ElementCompositionPreview::GetElementChildVisual(host).try_as<comp::ContainerVisual>();
    comp::ContainerVisual layer = PastWrappers(root);
    if (!layer) return rows;
    for (const comp::Visual& child : layer.Children()) {
        auto sprite = child.try_as<comp::SpriteVisual>();
        auto mask = sprite ? sprite.Brush().try_as<comp::CompositionMaskBrush>() : nullptr;
        auto words = mask ? mask.Mask().try_as<comp::CompositionSurfaceBrush>() : nullptr;
        auto surface = words ? words.Surface().try_as<comp::CompositionVisualSurface>() : nullptr;
        auto source = mask ? mask.Source().try_as<comp::CompositionLinearGradientBrush>() : nullptr;
        if (!surface || !source) continue;
        rows.push_back({sprite, mask, source, SpansOf(surface, sprite.Size().x)});
    }
    std::stable_sort(rows.begin(), rows.end(),
                     [](const FoundRow& a, const FoundRow& b) { return a.sprite.Offset().y < b.sprite.Offset().y; });
    return rows;
}

// Apple's gradient: four stops, the sung colour up to the edge and the unsung one after it.
bool ShapedAsApples(const comp::CompositionLinearGradientBrush& brush) {
    auto stops = brush.ColorStops();
    if (stops.Size() != 4) return false;
    return stops.GetAt(0).Offset() == 0 && stops.GetAt(3).Offset() == 1 &&
           std::fabs(stops.GetAt(1).Offset() - stops.GetAt(2).Offset()) < 1e-4f;
}

void CopyColours(const comp::CompositionLinearGradientBrush& from, const comp::CompositionLinearGradientBrush& to) {
    auto source = from.ColorStops();
    auto target = to.ColorStops();
    for (uint32_t i = 0; i < 4; i++) {
        winrt::Windows::UI::Color colour = source.GetAt(i).Color();
        if (target.GetAt(i).Color() != colour) target.GetAt(i).Color(colour);
    }
}

// Ampify's gradient for a row: Apple's colours and direction, its two middle stops following the "Edge" of a
// property set, which a key-frame animation moves along `path`.
Row MakeRow(const FoundRow& found, const std::vector<FillKey>& path, int64_t lineStartMs) {
    comp::Compositor compositor = found.sprite.Compositor();
    Row row;
    row.sprite = found.sprite;
    row.mask = found.mask;
    row.apple = found.source;
    row.lineStartMs = lineStartMs;
    row.durationMs = path.back().atMs + kAfterLineMs;
    row.ours = compositor.CreateLinearGradientBrush();
    row.ours.StartPoint(found.source.StartPoint());
    row.ours.EndPoint(found.source.EndPoint());
    row.ours.MappingMode(found.source.MappingMode());
    auto appleStops = found.source.ColorStops();
    const float start = static_cast<float>(path.front().edge);
    const float offsets[] = {0.0f, start, start, 1.0f};
    for (uint32_t i = 0; i < 4; i++) {
        row.ours.ColorStops().Append(compositor.CreateColorGradientStop(offsets[i], appleStops.GetAt(i).Color()));
    }
    row.edge = compositor.CreatePropertySet();
    row.edge.InsertScalar(kEdge, start);
    auto follow = compositor.CreateExpressionAnimation(L"fill.Edge");
    follow.SetReferenceParameter(L"fill", row.edge);
    row.ours.ColorStops().GetAt(1).StartAnimation(L"Offset", follow);
    row.ours.ColorStops().GetAt(2).StartAnimation(L"Offset", follow);
    auto move = compositor.CreateScalarKeyFrameAnimation();
    auto linear = compositor.CreateLinearEasingFunction();
    for (const FillKey& key : path) {
        move.InsertKeyFrame(static_cast<float>(key.atMs / row.durationMs), static_cast<float>(key.edge), linear);
    }
    move.InsertKeyFrame(1.0f, static_cast<float>(path.back().edge), linear);
    move.Duration(std::chrono::milliseconds(static_cast<int64_t>(row.durationMs)));
    row.edge.StartAnimation(kEdge, move);
    row.controller = row.edge.TryGetAnimationController(kEdge);
    return row;
}

// Puts the row's animation where the song is, paused while the song does not move. Only when it drifted or the song
// started or stopped moving: each move is a small jump of the edge.
void Sync(Row& row, const NowPlaying& now, int64_t tick, bool force) {
    if (!row.controller) return;
    const int64_t position = now.PositionAt(tick);
    const int64_t predicted = row.syncedPositionMs + (row.running ? tick - row.syncedTick : 0);
    if (!force && row.running == now.moving && std::llabs(position - predicted) <= kDriftMs) return;
    const double progress = std::clamp((position - row.lineStartMs) / row.durationMs, 0.0, 1.0);
    row.controller.Progress(static_cast<float>(progress));
    if (now.moving) {
        row.controller.Resume();
    } else {
        row.controller.Pause();
    }
    row.syncedPositionMs = position;
    row.syncedTick = tick;
    row.running = now.moving;
}

void GiveBack(Row& row) {
    if (row.mask && row.mask.Source() == row.ours) row.mask.Source(row.apple);
}

// The words the rows hold, or nullopt when a row is not drawn as Apple's are expected to be.
std::optional<size_t> WordCount(const std::vector<FoundRow>& found) {
    size_t count = 0;
    for (const FoundRow& row : found) {
        if (!ShapedAsApples(row.source)) {
            static std::atomic<bool> noted{false};
            if (!noted.exchange(true)) Note("Apple's gradient is not the four stops expected: Apple's own fill");
            return std::nullopt;
        }
        count += row.spans.size();
    }
    return count;
}

// The line of the song a main host's rows show: Apple masks the main rows of the line being sung only.
std::optional<size_t> MainLine(size_t count, const NowPlaying& now, int64_t tick) {
    std::optional<size_t> index = LineOfHost(*now.lyrics, count, false, now.PositionAt(tick));
    thread_local std::string notedTrack;  // a follower per interface thread
    if (!index && notedTrack != now.trackKey) {
        notedTrack = now.trackKey;
        Note("a line of " + std::to_string(count) + " words matches no line of the song: Apple's own fill");
    }
    return index;
}

// Lays line `index` on the host's rows, or leaves them to Apple. True when Ampify drives them.
bool Bind(Host& host, std::vector<FoundRow>& found, const NowPlaying& now, int64_t tick, std::optional<size_t> index) {
    std::optional<size_t> count = WordCount(found);
    if (!count) return false;
    if (!host.background) index = MainLine(*count, now, tick);
    if (!index || *index >= now.lyrics->lines.size()) return false;
    const LyricLine& line = now.lyrics->lines[*index];
    std::vector<LyricWord> words = WordsOfHost(line, host.background);
    if (words.size() != *count) return false;
    host.line = index;
    host.lineStartMs = line.startMs;
    host.lineEndMs = line.endMs;
    size_t first = 0;
    for (FoundRow& row : found) {
        std::span<const LyricWord> rowWords(words.data() + first, row.spans.size());
        first += row.spans.size();
        std::vector<FillKey> path = RowFillPath(rowWords, row.spans, line.startMs);
        if (path.empty()) continue;
        Row bound = MakeRow(row, path, line.startMs);
        Sync(bound, now, tick, true);
        row.mask.Source(bound.ours);
        host.rows.push_back(std::move(bound));
    }
    return !host.rows.empty();
}

void Release(Host& host) {
    for (Row& row : host.rows) GiveBack(row);
    host.rows.clear();
    host.line.reset();
}

bool SameRows(const std::vector<FoundRow>& found, const std::vector<Row>& rows) {
    if (found.size() != rows.size()) return false;
    for (size_t i = 0; i < found.size(); i++) {
        if (found[i].sprite != rows[i].sprite) return false;
    }
    return true;
}

// Whether bound rows still show what they were bound to: the same sprites, and for a main host a line under way, for
// a background host the line its main host sings.
bool StillBound(const Host& host, const std::vector<FoundRow>& found, const NowPlaying& now, int64_t tick,
                const Host* main) {
    if (!SameRows(found, host.rows)) return false;
    if (host.background) return main && main->line == host.line;
    return LineUnderway(host.lineStartMs, host.lineEndMs, now.PositionAt(tick));
}

// Follows one host: binds its rows when Apple starts singing its line, keeps them on the song, gives them back when
// Apple draws the line plainly again, rebuilt it, or the song left the line. Apple keeps background rows masked all
// along: they follow their line's main host (`main`), bound only while it is. A host found idle waits kIdleMs before
// it is read again, unless `hot` (near the line sung last).
void Follow(Host& host, const NowPlaying& now, int64_t tick, const Host* main, bool hot) {
    controls::Border border = host.border.get();
    if (!border || (host.background && (!main || !main->line))) return Release(host);
    if (host.rows.empty() && tick < host.retryAt) return;
    std::vector<FoundRow> found = RowsOf(border);
    if (!host.rows.empty() && !StillBound(host, found, now, tick, main)) {
        Release(host);
        found = RowsOf(border);  // read again: the rows showed Ampify's gradient
    }
    if (host.rows.empty()) {
        const bool bound = !found.empty() && Bind(host, found, now, tick, main ? main->line : std::nullopt);
        if (!bound) host.retryAt = tick + (!found.empty() ? kRetryMs : hot ? 0 : kIdleMs);
        return;
    }
    for (Row& row : host.rows) {
        if (row.mask.Source() != row.ours) row.mask.Source(row.ours);  // Apple put its own back
        CopyColours(row.apple, row.ours);
        Sync(row, now, tick, false);
    }
}

// One of Apple's lyrics views (the side pane, the full-screen player, the mini player) and its hosts.
struct View {
    winrt::weak_ref<xaml::FrameworkElement> element;
    std::vector<Host> hosts;
    std::chrono::steady_clock::time_point nextSearch{};
    std::chrono::steady_clock::time_point songAt{};  // when the song last changed: Apple builds its lines then
    std::string trackKey;
    int lastBound = -1;  // the index of the main host bound last, -1 before any
};

// Interface thread of its windows only; never destroyed, like the other static services (xaml-lessons.md).
struct Follower {
    dispatching::DispatcherQueue queue{nullptr};
    dispatching::DispatcherQueueTimer timer{nullptr};
    std::vector<View> views;
    std::chrono::steady_clock::time_point nextViewSearch{};
};

template <typename Match>
void Walk(const xaml::DependencyObject& root, std::size_t limit, Match&& match) {
    std::vector<xaml::DependencyObject> queue{root};
    for (std::size_t i = 0; i < queue.size() && queue.size() < limit; i++) {
        if (match(queue[i])) continue;  // nothing to find inside what matched
        int count = media::VisualTreeHelper::GetChildrenCount(queue[i]);
        for (int child = 0; child < count; child++) queue.push_back(media::VisualTreeHelper::GetChild(queue[i], child));
    }
}

// The line a host stands in, by identity, or null.
void* LineKeyOf(const xaml::DependencyObject& host) {
    xaml::DependencyObject at = host;
    for (int depth = 0; at && depth < kLineSearchDepth; ++depth) {
        if (winrt::get_class_name(at) == kLineClass) return winrt::get_abi(at.as<winrt::Windows::Foundation::IUnknown>());
        at = media::VisualTreeHelper::GetParent(at);
    }
    return nullptr;
}

void FindHosts(View& view, const xaml::FrameworkElement& element) {
    std::vector<Host> hosts;
    Walk(element, kHostSearchLimit, [&](const xaml::DependencyObject& object) {
        auto border = object.try_as<controls::Border>();
        const bool main = border && border.Name() == kMainHost;
        if (!main && !(border && border.Name() == kBackgroundHost)) return false;
        auto same = std::find_if(view.hosts.begin(), view.hosts.end(),
                                 [&](const Host& host) { return host.border.get() == border; });
        Host host = same != view.hosts.end() ? std::move(*same) : Host{winrt::make_weak(border), !main};
        host.lineKey = LineKeyOf(border);
        hosts.push_back(std::move(host));
        return true;
    });
    for (Host& gone : view.hosts) {
        if (gone.border.get()) continue;
        Release(gone);
    }
    if (hosts.size() != view.hosts.size()) view.lastBound = -1;
    view.hosts = std::move(hosts);
}

// The main host of the line a background host stands in, or null.
const Host* MainOf(const View& view, const Host& background) {
    if (!background.lineKey) return nullptr;
    for (const Host& host : view.hosts) {
        if (!host.background && host.lineKey == background.lineKey) return &host;
    }
    return nullptr;
}

// Main hosts first, then the background ones beside them, which follow their main host.
void FollowHosts(View& view, const NowPlaying& now, int64_t tick) {
    const int last = view.lastBound;
    for (size_t i = 0; i < view.hosts.size(); i++) {
        Host& host = view.hosts[i];
        if (host.background) continue;
        const int at = static_cast<int>(i);
        Follow(host, now, tick, nullptr, last < 0 || (at >= last - kHotBefore && at <= last + kHotAfter));
        if (!host.rows.empty()) view.lastBound = at;
    }
    for (Host& host : view.hosts) {
        if (host.background) Follow(host, now, tick, MainOf(view, host), false);
    }
}

// A queue's identity: two projections of the same queue may differ, their IUnknown does not.
void* QueueKey(const dispatching::DispatcherQueue& queue) {
    return queue ? winrt::get_abi(queue.as<winrt::Windows::Foundation::IUnknown>()) : nullptr;
}

std::vector<xaml::Window> WindowsOf(const Follower& follower) {
    std::vector<xaml::Window> windows;
    for (WindowRole role : {WindowRole::Main, WindowRole::Player, WindowRole::Mini}) {
        xaml::Window window = AppWindowOf(role);
        if (window && QueueKey(window.DispatcherQueue()) == QueueKey(follower.queue)) windows.push_back(window);
    }
    return windows;
}

void EnsureFollower(const dispatching::DispatcherQueue& queue);

void FindViews(Follower& follower) {
    for (WindowRole role : {WindowRole::Main, WindowRole::Player, WindowRole::Mini}) {
        if (xaml::Window window = AppWindowOf(role)) EnsureFollower(window.DispatcherQueue());
    }
    for (const xaml::Window& window : WindowsOf(follower)) {
        auto content = window.Content();
        if (!content) continue;
        Walk(content, kViewSearchLimit, [&](const xaml::DependencyObject& object) {
            if (winrt::get_class_name(object) != kViewClass) return false;
            auto element = object.try_as<xaml::FrameworkElement>();
            bool known = std::any_of(follower.views.begin(), follower.views.end(),
                                     [&](const View& view) { return view.element.get() == element; });
            if (element && !known) follower.views.push_back(View{winrt::make_weak(element), {}, {}});
            return true;
        });
    }
    std::erase_if(follower.views, [](View& view) {
        if (view.element.get()) return false;
        for (Host& host : view.hosts) Release(host);
        return true;
    });
}

bool WordTimed(const NowPlaying& now) {
    return now.lyrics && now.lyrics->synced &&
           std::any_of(now.lyrics->lines.begin(), now.lyrics->lines.end(),
                       [](const LyricLine& line) { return line.timing == LyricTiming::ByWord; });
}

void ReleaseAll(Follower& follower) {
    for (View& view : follower.views) {
        for (Host& host : view.hosts) Release(host);
    }
}

void TickFollower(Follower& follower) {
    NowPlaying now = CurrentNowPlaying();
    if (!g_smooth.load() || !WordTimed(now)) return ReleaseAll(follower);
    const auto clock = std::chrono::steady_clock::now();
    bool shown = false;
    for (const View& view : follower.views) {
        xaml::FrameworkElement element = view.element.get();
        shown = shown || (element && element.IsLoaded() && element.ActualHeight() > 0);
    }
    if (clock >= follower.nextViewSearch) {
        follower.nextViewSearch = clock + (shown ? kViewCheckEvery : kViewSearchEvery);
        FindViews(follower);
    }
    const int64_t tick = Tick();
    for (View& view : follower.views) {
        xaml::FrameworkElement element = view.element.get();
        if (!element || !element.IsLoaded() || element.ActualHeight() <= 0) {
            for (Host& host : view.hosts) Release(host);
            continue;
        }
        if (view.trackKey != now.trackKey) {
            view.trackKey = now.trackKey;
            view.songAt = clock;
            view.nextSearch = clock;
        }
        if (clock >= view.nextSearch) {
            const bool settling = view.hosts.empty() || clock - view.songAt < kSongSettles;
            view.nextSearch = clock + (settling ? kHostSearchEvery : kHostCheckEvery);
            FindHosts(view, element);
        }
        FollowHosts(view, now, tick);
    }
}

std::mutex g_followersLock;
std::vector<void*> g_followerQueues;  // the queues that have a follower, by identity

void EnsureFollower(const dispatching::DispatcherQueue& queue) {
    void* key = QueueKey(queue);
    if (!key) return;
    {
        std::lock_guard lock(g_followersLock);
        if (std::find(g_followerQueues.begin(), g_followerQueues.end(), key) != g_followerQueues.end()) return;
        g_followerQueues.push_back(key);
    }
    queue.TryEnqueue([queue]() noexcept {
        try {
            auto* follower = new Follower();
            follower->queue = queue;
            follower->timer = queue.CreateTimer();
            follower->timer.Interval(kTick);
            follower->timer.Tick([follower](auto&&, auto&&) noexcept {
                try {
                    TickFollower(*follower);
                } catch (...) {
                    // A handler runs inside Apple Music's XAML frame: nothing may escape it.
                }
            });
            follower->timer.Start();
        } catch (...) {
        }
    });
}

}  // namespace

void SetLyricsFill(bool smooth) noexcept { g_smooth.store(smooth); }

void KeepLyricsFill(Journal& journal) noexcept {
    try {
        g_journal.store(&journal);
        if (xaml::Window window = AppWindowOf(WindowRole::Main)) EnsureFollower(window.DispatcherQueue());
    } catch (...) {
        // Nothing may escape into Apple Music's XAML frame.
    }
}

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
