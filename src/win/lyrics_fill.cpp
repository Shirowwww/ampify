#include "lyrics_fill.hpp"
#include "xaml_ending.hpp"

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

#include "../core/amp_abi.hpp"
#include "../core/lyrics_fill.hpp"
#include "../core/now_playing.hpp"
#include "winrt_abi.hpp"
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
constexpr auto kHostSearchEvery = std::chrono::seconds(1);   // while Apple builds its lines
constexpr auto kHostCheckEvery = std::chrono::seconds(15);   // once they stand
constexpr auto kLinesSettle = std::chrono::seconds(5);
constexpr auto kViewSearchEvery = std::chrono::seconds(1);   // while no lyrics view shows (a walk is ~4 ms)
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
constexpr wchar_t kSung[] = L"Sung";
constexpr wchar_t kUnsung[] = L"Unsung";
constexpr wchar_t kLift[] = L"Lift";  // and the word's index
// How far a word rises as it is sung, in heights of its row: 2.15 in the side pane, near Apple's own.
constexpr float kLiftRowHeights = 0.05f;
// Apple's own lift on Windows (AMP.UI.Views 1.1792, read in its code and its words' Offsets): kAppleLift up, over at
// least kAppleLiftMinMs.
constexpr float kAppleLift = 2.0f;
constexpr double kAppleLiftMinMs = 400;
// The soft edge's width, in heights of its row: the colour turns from sung to unsung over about the height of the
// letters (a Hangul syllable, two Latin letters), so the romaji's smaller type gets a narrower one.
constexpr float kSoftEdgeRowHeights = 0.6f;

std::atomic<bool> g_smooth{true};
std::atomic<bool> g_softEdge{false};
std::atomic<Journal*> g_journal{nullptr};

void Note(const std::string& line) {
    if (Journal* journal = g_journal.load()) journal->Write("Lyrics fill: " + line);
}

int64_t Tick() { return static_cast<int64_t>(GetTickCount64()); }

// One row of a line being sung: Apple's gradient behind its mask, and Ampify's in its place. With the soft edge, the
// words Ampify lifts itself (`lifted`), on the row's timeline.
struct Row {
    comp::SpriteVisual sprite{nullptr};
    comp::CompositionMaskBrush mask{nullptr};
    comp::CompositionLinearGradientBrush apple{nullptr};
    comp::CompositionLinearGradientBrush ours{nullptr};
    comp::CompositionPropertySet edge{nullptr};
    std::vector<comp::AnimationController> controllers;  // the edge's (or both stops'), the lifts': one timeline
    std::vector<comp::Visual> lifted;
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
    std::vector<comp::SpriteVisual> shown;  // every row the host showed when bound, Apple's own included
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
    std::vector<comp::Visual> words;  // the visual of each word, drawn into the row's mask
    bool spoken = false;  // a pronunciation row (romaji), under the sung row it spells
    float top = 0;        // where the row starts in the host, the offset of its layer included
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

// The words drawn into the row's surface, as parts of the sprite's width, and their visuals.
void ReadWords(const comp::CompositionVisualSurface& surface, float rowWidth, FoundRow& row) {
    std::vector<WordSpan>& spans = row.spans;
    auto source = surface.SourceVisual().try_as<comp::ContainerVisual>();
    if (!source || rowWidth <= 0) return;
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
        row.words.push_back(word);
    }
}

// The words' spans along the row's gradient (SpansAlong): Apple's runs left to right, from (0,0) to (1,0) of the row.
std::vector<WordSpan> AlongGradient(const comp::CompositionLinearGradientBrush& gradient,
                                    const std::vector<WordSpan>& spans, float rowWidth) {
    float startX = gradient.StartPoint().x;
    float endX = gradient.EndPoint().x;
    if (gradient.MappingMode() == comp::CompositionMappingMode::Absolute) {
        if (rowWidth <= 0) return {};
        startX /= rowWidth;
        endX /= rowWidth;
    }
    return SpansAlong(spans, startX, endX);
}

// The masked rows among a layer's children, each with its top in the host (`shift`: the layer's own offset).
void AddRowsOf(const comp::ContainerVisual& layer, float shift, std::vector<FoundRow>& rows) {
    for (const comp::Visual& child : layer.Children()) {
        auto sprite = child.try_as<comp::SpriteVisual>();
        auto mask = sprite ? sprite.Brush().try_as<comp::CompositionMaskBrush>() : nullptr;
        auto words = mask ? mask.Mask().try_as<comp::CompositionSurfaceBrush>() : nullptr;
        auto surface = words ? words.Surface().try_as<comp::CompositionVisualSurface>() : nullptr;
        auto source = mask ? mask.Source().try_as<comp::CompositionLinearGradientBrush>() : nullptr;
        if (!surface || !source) continue;
        FoundRow row{sprite, mask, source, {}, {}, false, shift + sprite.Offset().y};
        ReadWords(surface, sprite.Size().x, row);
        row.spans = AlongGradient(source, row.spans, sprite.Size().x);
        rows.push_back(std::move(row));
    }
}

// The rows of a host Apple is singing now, top to bottom; none while it draws the line plainly. With Show
// Pronunciation, Apple puts the romaji either among the sung rows or, in a narrow pane, in a layer of their own
// beside the sung rows' layer (issue #5): the rows of each layer are read then.
std::vector<FoundRow> RowsOf(const controls::Border& host) {
    std::vector<FoundRow> rows;
    auto root = xaml::Hosting::ElementCompositionPreview::GetElementChildVisual(host).try_as<comp::ContainerVisual>();
    comp::ContainerVisual layer = PastWrappers(root);
    if (!layer) return rows;
    AddRowsOf(layer, 0, rows);
    if (rows.empty()) {
        for (const comp::Visual& child : layer.Children()) {
            if (auto inner = child.try_as<comp::ContainerVisual>()) AddRowsOf(inner, inner.Offset().y, rows);
        }
    }
    std::stable_sort(rows.begin(), rows.end(), [](const FoundRow& a, const FoundRow& b) { return a.top < b.top; });
    std::vector<double> heights;
    for (const FoundRow& row : rows) heights.push_back(row.sprite.Size().y);
    const std::vector<bool> spoken = PronunciationRows(heights);
    for (size_t i = 0; i < rows.size(); i++) rows[i].spoken = spoken[i];
    return rows;
}

// Apple's gradient: four stops, the sung colour up to the edge and the unsung one after it (ShapedAsAppleFill).
bool ShapedAsApples(const comp::CompositionLinearGradientBrush& brush) {
    std::vector<FillStop> stops;
    for (const comp::CompositionColorGradientStop& stop : brush.ColorStops()) {
        const winrt::Windows::UI::Color c = stop.Color();
        stops.push_back({stop.Offset(), static_cast<uint32_t>(c.A) << 24 | static_cast<uint32_t>(c.R) << 16 |
                                            static_cast<uint32_t>(c.G) << 8 | c.B});
    }
    return ShapedAsAppleFill(stops);
}

void CopyColours(const comp::CompositionLinearGradientBrush& from, const comp::CompositionLinearGradientBrush& to) {
    auto source = from.ColorStops();
    auto target = to.ColorStops();
    for (uint32_t i = 0; i < 4; i++) {
        winrt::Windows::UI::Color colour = source.GetAt(i).Color();
        if (target.GetAt(i).Color() != colour) target.GetAt(i).Color(colour);
    }
}

// Moves the scalar `name` of the row's property set along `path` over the row's whole timeline, straight between keys.
void StartPath(Row& row, const comp::Compositor& compositor, const wchar_t* name, const std::vector<FillKey>& path) {
    row.edge.InsertScalar(name, static_cast<float>(path.front().edge));
    auto move = compositor.CreateScalarKeyFrameAnimation();
    auto linear = compositor.CreateLinearEasingFunction();
    for (const FillKey& key : path) {
        move.InsertKeyFrame(static_cast<float>(key.atMs / row.durationMs), static_cast<float>(key.edge), linear);
    }
    move.InsertKeyFrame(1.0f, static_cast<float>(path.back().edge), linear);
    move.Duration(std::chrono::milliseconds(static_cast<int64_t>(row.durationMs)));
    row.edge.StartAnimation(name, move);
    if (auto controller = row.edge.TryGetAnimationController(name)) row.controllers.push_back(controller);
}

// Ampify's gradient for a row: Apple's colours and direction, its two middle stops following scalars of a property
// set that key-frame animations move: both the "Edge" (crisp), or each its own path (soft), kept within the row so the
// stops stay in order.
Row MakeRow(const FoundRow& found, const RowFillPlan& plan, int64_t lineStartMs) {
    comp::Compositor compositor = found.sprite.Compositor();
    Row row;
    row.sprite = found.sprite;
    row.mask = found.mask;
    row.apple = found.source;
    row.lineStartMs = lineStartMs;
    row.durationMs = plan.LastMs() + kAfterLineMs;
    row.ours = compositor.CreateLinearGradientBrush();
    row.ours.StartPoint(found.source.StartPoint());
    row.ours.EndPoint(found.source.EndPoint());
    row.ours.MappingMode(found.source.MappingMode());
    auto appleStops = found.source.ColorStops();
    const float sungAt = static_cast<float>(plan.Sung().front().edge);
    const float unsungAt = static_cast<float>(plan.Unsung().front().edge);
    const float offsets[] = {0.0f, std::clamp(sungAt, 0.0f, 1.0f), std::clamp(unsungAt, 0.0f, 1.0f), 1.0f};
    for (uint32_t i = 0; i < 4; i++) {
        row.ours.ColorStops().Append(compositor.CreateColorGradientStop(offsets[i], appleStops.GetAt(i).Color()));
    }
    row.edge = compositor.CreatePropertySet();
    const std::wstring sungName = plan.Soft() ? kSung : kEdge;
    const std::wstring unsungName = plan.Soft() ? kUnsung : kEdge;
    StartPath(row, compositor, sungName.c_str(), plan.Sung());
    if (plan.Soft()) StartPath(row, compositor, unsungName.c_str(), plan.Unsung());
    auto sung = compositor.CreateExpressionAnimation(L"Clamp(fill." + sungName + L", 0, 1)");
    auto unsung = compositor.CreateExpressionAnimation(L"Clamp(fill." + unsungName + L", 0, 1)");
    sung.SetReferenceParameter(L"fill", row.edge);
    unsung.SetReferenceParameter(L"fill", row.edge);
    row.ours.ColorStops().GetAt(1).StartAnimation(L"Offset", sung);
    row.ours.ColorStops().GetAt(2).StartAnimation(L"Offset", unsung);
    return row;
}

// Where Apple's words rest: centred in the row (1.1792: 43.04 high, words 28.64, resting at 7.2), since Apple keeps a
// line's words up after a pause or a jump back. Without a word height, Apple's last Offsets decide (it sets a word
// kAppleLift up once its lift ends): the lower words rest; all alike, they rest unless every word had its lift.
float RestOf(const FoundRow& found, std::span<const LyricWord> words, int64_t lineStartMs, double atMs) {
    const float wordHeight = found.words.front().Size().y;
    if (wordHeight > 0 && wordHeight < found.sprite.Size().y) return (found.sprite.Size().y - wordHeight) / 2;
    float lowest = found.words.front().Offset().y;  // the largest Offset: the furthest down
    float highest = lowest;
    for (const comp::Visual& word : found.words) {
        lowest = std::max(lowest, word.Offset().y);
        highest = std::min(highest, word.Offset().y);
    }
    if (lowest - highest > kAppleLift / 2) return lowest;
    const double lastStart = static_cast<double>(words.back().startMs - lineStartMs);
    const double lastEnd = static_cast<double>(words.back().endMs - lineStartMs);
    const bool allUp = atMs >= std::min(lastEnd, lastStart + kAppleLiftMinMs);
    return allUp ? lowest + kAppleLift : lowest;
}

// With the soft edge, Ampify lifts the row's words as the iPhone app does (LiftPath), on the row's timeline: a scalar
// "Lift<i>" of the row's property set for each word. Apple keeps animating each word's Offset its own way (issue #5: up
// kAppleLift in 400 ms to 3 s, started again at each step of its once-a-second clock); each word's TransformMatrix
// takes that motion back out, read from the Offset the compositor draws, and puts Ampify's lift in its place.
void LiftWords(Row& row, const FoundRow& found, std::span<const LyricWord> words, int64_t lineStartMs, double atMs) {
    if (found.words.size() != words.size() || found.words.empty()) return;
    comp::Compositor compositor = found.sprite.Compositor();
    const float rest = RestOf(found, words, lineStartMs, atMs);
    const float height = kLiftRowHeights * found.sprite.Size().y;
    for (size_t i = 0; i < words.size(); i++) {
        const double start = static_cast<double>(words[i].startMs - lineStartMs);
        const double end = static_cast<double>(words[i].endMs - lineStartMs);
        const std::wstring name = kLift + std::to_wstring(i);
        StartPath(row, compositor, name.c_str(), LiftPath(start, end));
        auto lift = compositor.CreateExpressionAnimation(
            L"Matrix4x4.CreateTranslation(Vector3(0, rest - this.Target.Offset.Y - height * fill." + name + L", 0))");
        lift.SetReferenceParameter(L"fill", row.edge);
        lift.SetScalarParameter(L"rest", rest);
        lift.SetScalarParameter(L"height", height);
        found.words[i].StartAnimation(L"TransformMatrix", lift);
        row.lifted.push_back(found.words[i]);
    }
}

void Unlift(Row& row) {
    for (comp::Visual& word : row.lifted) {
        try {
            word.StopAnimation(L"TransformMatrix");
            word.TransformMatrix(winrt::Windows::Foundation::Numerics::float4x4::identity());
        } catch (const winrt::hresult_error&) {
            // Apple closed the word's visual: nothing left to put back.
        }
    }
    row.lifted.clear();
}

// Puts the row's animations where the song is, paused while the song does not move. Only when it drifted or the song
// started or stopped moving: each move is a small jump of the edge.
void Sync(Row& row, const NowPlaying& now, int64_t tick, bool force) {
    if (row.controllers.empty()) return;
    const int64_t position = now.PositionAt(tick);
    const int64_t predicted = row.syncedPositionMs + (row.running ? tick - row.syncedTick : 0);
    if (!force && row.running == now.moving && std::llabs(position - predicted) <= kDriftMs) return;
    const double progress = std::clamp((position - row.lineStartMs) / row.durationMs, 0.0, 1.0);
    for (comp::AnimationController& controller : row.controllers) {
        controller.Progress(static_cast<float>(progress));
        if (now.moving) {
            controller.Resume();
        } else {
            controller.Pause();
        }
    }
    row.syncedPositionMs = position;
    row.syncedTick = tick;
    row.running = now.moving;
}

void GiveBack(Row& row) {
    if (row.mask && row.mask.Source() == row.ours) row.mask.Source(row.apple);
    Unlift(row);
}

// The words the sung rows (or the pronunciation rows) hold, or nullopt when a row is not drawn as Apple's are
// expected to be.
std::optional<size_t> WordCount(const std::vector<FoundRow>& found, bool spoken = false) {
    size_t count = 0;
    for (const FoundRow& row : found) {
        if (!ShapedAsApples(row.source)) {
            static std::atomic<bool> noted{false};
            if (!noted.exchange(true)) Note("Apple's gradient is not the four stops expected: Apple's own fill");
            return std::nullopt;
        }
        if (row.spoken == spoken) count += row.spans.size();
    }
    return count;
}

// The KaraokeLyricsLine a host stands in, or null.
xaml::DependencyObject LineOf(const xaml::DependencyObject& host) {
    xaml::DependencyObject at = host;
    for (int depth = 0; at && depth < kLineSearchDepth; ++depth) {
        if (winrt::get_class_name(at) == kLineClass) return at;
        at = media::VisualTreeHelper::GetParent(at);
    }
    return nullptr;
}

// The start Apple bound the host's line to, or nullopt.
std::optional<int64_t> AppleStartOf(const xaml::DependencyObject& host) {
    xaml::DependencyObject line = LineOf(host);
    if (!line) return std::nullopt;
    rt::Ref unknown(winrt::detach_abi(line.as<winrt::Windows::Foundation::IUnknown>()));
    rt::Ref view;
    int64_t ticks = -1;  // Windows.Foundation.TimeSpan, 100 ns
    if (!rt::Query(unknown, rt::GuidOf(amp::kIidKaraokeLyricsLineView), view) ||
        FAILED(rt::CallSlot(view, amp::kKaraokeLyricsLineViewStart, &ticks)) || ticks < 0) {
        return std::nullopt;
    }
    return TicksToMs(ticks);
}

// Each row as "words@height", top to bottom: what a line Ampify could not match was drawn as.
std::string RowShapes(const std::vector<FoundRow>& found) {
    std::string shapes;
    for (const FoundRow& row : found) {
        if (!shapes.empty()) shapes += ", ";
        shapes += std::to_string(row.spans.size()) + "@" + std::to_string(static_cast<int>(row.sprite.Size().y));
        if (row.spoken) shapes += " pronunciation";
    }
    return shapes;
}

// Why a host's rows (or part of them) were left to Apple, once per song on each interface thread.
void NoteLeftToApple(const NowPlaying& now, const std::string& why) {
    thread_local std::string notedTrack;
    if (notedTrack == now.trackKey) return;
    notedTrack = now.trackKey;
    Note(why);
}

// That the romaji rows follow the sung words, once per song on each interface thread: a report then shows it.
void NotePronunciationFilled(const NowPlaying& now, const std::vector<FoundRow>& found) {
    thread_local std::string notedTrack;
    if (notedTrack == now.trackKey) return;
    notedTrack = now.trackKey;
    Note("pronunciation rows filled with the sung words (rows " + RowShapes(found) + ")");
}

// The line of the song a main host's rows show: Apple masks the main rows of the line being sung only.
std::optional<size_t> MainLine(const controls::Border& border, const std::vector<FoundRow>& found, size_t count,
                               const NowPlaying& now, int64_t tick) {
    std::optional<size_t> index = LineOfHost(*now.lyrics, count, false, now.PositionAt(tick), AppleStartOf(border));
    thread_local std::string notedTrack;  // a follower per interface thread
    if (!index && notedTrack != now.trackKey) {
        notedTrack = now.trackKey;
        Note("a line of " + std::to_string(count) + " words (rows " + RowShapes(found) +
             ") matches no line of the song: Apple's own fill");
    }
    return index;
}

// The soft edge's width on a row, as a part of the row's width; 0 for Apple's crisp edge.
float SoftWidthOf(const FoundRow& row, bool softEdge) {
    const winrt::Windows::Foundation::Numerics::float2 size = row.sprite.Size();
    return softEdge && size.x > 0 ? kSoftEdgeRowHeights * size.y / size.x : 0.0f;
}

// Lays `words` on the host's sung rows, or on its pronunciation rows, in order.
void BindRows(Host& host, std::vector<FoundRow>& found, bool spoken, const std::vector<LyricWord>& words,
              const LyricLine& line, const NowPlaying& now, int64_t tick, bool softEdge) {
    size_t first = 0;
    for (FoundRow& row : found) {
        if (row.spoken != spoken) continue;
        std::span<const LyricWord> rowWords(words.data() + first, row.spans.size());
        first += row.spans.size();
        const std::optional<RowFillPlan> plan =
            RowFillPlan::Of(rowWords, row.spans, line.startMs, SoftWidthOf(row, softEdge));
        if (!plan) continue;
        Row bound = MakeRow(row, *plan, line.startMs);
        if (plan->Soft()) {
            LiftWords(bound, row, rowWords, line.startMs, static_cast<double>(now.PositionAt(tick) - line.startMs));
        }
        Sync(bound, now, tick, true);
        row.mask.Source(bound.ours);
        host.rows.push_back(std::move(bound));
    }
}

// Lays line `index` on the host's rows, or leaves them to Apple. True when Ampify drives them. The pronunciation rows
// spell the sung words one for one in Apple's lyrics (issue #5): they follow the same times, or stay Apple's.
bool Bind(Host& host, std::vector<FoundRow>& found, const NowPlaying& now, int64_t tick, std::optional<size_t> index,
          bool softEdge) {
    std::optional<size_t> count = WordCount(found);
    if (!count) return false;
    if (!host.background) index = MainLine(host.border.get(), found, *count, now, tick);  // Follow holds the border
    if (!index || *index >= now.lyrics->lines.size()) return false;
    const LyricLine& line = now.lyrics->lines[*index];
    std::vector<LyricWord> words = WordsOfHost(line, host.background);
    if (words.size() != *count) {
        NoteLeftToApple(now, "rows of " + std::to_string(*count) + " words (" + RowShapes(found) + ") for a line of " +
                                 std::to_string(words.size()) + ": Apple's own fill");
        return false;
    }
    host.line = index;
    host.lineStartMs = line.startMs;
    host.lineEndMs = line.endMs;
    BindRows(host, found, false, words, line, now, tick, softEdge);
    const size_t spoken = WordCount(found, true).value_or(0);
    if (spoken == words.size()) {
        BindRows(host, found, true, words, line, now, tick, softEdge);
        NotePronunciationFilled(now, found);
    } else if (spoken > 0) {
        NoteLeftToApple(now, "pronunciation rows of " + std::to_string(spoken) + " words (" + RowShapes(found) +
                                 ") for a line of " + std::to_string(words.size()) + ": Apple fills them");
    }
    host.shown.clear();
    for (const FoundRow& row : found) host.shown.push_back(row.sprite);
    return !host.rows.empty();
}

void Release(Host& host) {
    for (Row& row : host.rows) GiveBack(row);
    host.rows.clear();
    host.shown.clear();
    host.line.reset();
}

bool SameRows(const std::vector<FoundRow>& found, const Host& host) {
    if (found.size() != host.shown.size()) return false;
    for (size_t i = 0; i < found.size(); i++) {
        if (found[i].sprite != host.shown[i]) return false;
    }
    return true;
}

// Whether bound rows still show what they were bound to: the same sprites, and for a main host a line under way, for
// a background host the line its main host sings.
bool StillBound(const Host& host, const std::vector<FoundRow>& found, const NowPlaying& now, int64_t tick,
                const Host* main) {
    if (!SameRows(found, host)) return false;
    if (host.background) return main && main->line == host.line;
    return LineUnderway(host.lineStartMs, host.lineEndMs, now.PositionAt(tick));
}

// Follows one host: binds its rows when Apple starts singing its line, keeps them on the song, gives them back when
// Apple draws the line plainly again, rebuilt it, or the song left the line. Apple keeps background rows masked all
// along: they follow their line's main host (`main`), bound only while it is. A host found idle waits kIdleMs before
// it is read again, unless `hot` (near the line sung last).
void Follow(Host& host, const NowPlaying& now, int64_t tick, const Host* main, bool hot, bool softEdge) {
    controls::Border border = host.border.get();
    if (!border || (host.background && (!main || !main->line))) return Release(host);
    if (host.rows.empty() && tick < host.retryAt) return;
    std::vector<FoundRow> found = RowsOf(border);
    if (!host.rows.empty() && !StillBound(host, found, now, tick, main)) {
        Release(host);
        found = RowsOf(border);  // read again: the rows showed Ampify's gradient
    }
    if (host.rows.empty()) {
        const bool bound = !found.empty() && Bind(host, found, now, tick, main ? main->line : std::nullopt, softEdge);
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
    // When Apple last built its lines: a new song, or the view shown again (the pane reopened rebuilds them).
    std::chrono::steady_clock::time_point builtAt{};
    std::string trackKey;
    bool shown = false;
    int lastBound = -1;  // the index of the main host bound last, -1 before any
};

// Interface thread of its windows only; never destroyed, like the other static services (xaml-lessons.md).
struct Follower {
    dispatching::DispatcherQueue queue{nullptr};
    dispatching::DispatcherQueueTimer timer{nullptr};
    std::vector<View> views;
    std::chrono::steady_clock::time_point lastViewSearch{};
    bool softEdge = false;  // the edge the rows were bound with: a change binds them again
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
    xaml::DependencyObject line = LineOf(host);
    return line ? winrt::get_abi(line.as<winrt::Windows::Foundation::IUnknown>()) : nullptr;
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
void FollowHosts(View& view, const NowPlaying& now, int64_t tick, bool softEdge) {
    const int last = view.lastBound;
    for (size_t i = 0; i < view.hosts.size(); i++) {
        Host& host = view.hosts[i];
        if (host.background) continue;
        const int at = static_cast<int>(i);
        Follow(host, now, tick, nullptr, last < 0 || (at >= last - kHotBefore && at <= last + kHotAfter), softEdge);
        if (!host.rows.empty()) view.lastBound = at;
    }
    for (Host& host : view.hosts) {
        if (host.background) Follow(host, now, tick, MainOf(view, host), false, softEdge);
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
    if (const bool soft = g_softEdge.load(); soft != follower.softEdge) {
        ReleaseAll(follower);
        follower.softEdge = soft;
    }
    const auto clock = std::chrono::steady_clock::now();
    bool shown = false;
    for (const View& view : follower.views) {
        xaml::FrameworkElement element = view.element.get();
        shown = shown || (element && element.IsLoaded() && element.ActualHeight() > 0);
    }
    // A view that hides is gone for good: Apple builds a new one when its pane opens again.
    const auto every = shown ? kViewCheckEvery : kViewSearchEvery;
    if (clock - follower.lastViewSearch >= every) {
        follower.lastViewSearch = clock;
        FindViews(follower);
    }
    const int64_t tick = Tick();
    for (View& view : follower.views) {
        xaml::FrameworkElement element = view.element.get();
        if (!element || !element.IsLoaded() || element.ActualHeight() <= 0) {
            for (Host& host : view.hosts) Release(host);
            view.shown = false;
            continue;
        }
        if (view.trackKey != now.trackKey || !view.shown) {
            view.trackKey = now.trackKey;
            view.shown = true;
            view.builtAt = clock;
            view.nextSearch = clock;
        }
        if (clock >= view.nextSearch) {
            const bool settling = view.hosts.empty() || clock - view.builtAt < kLinesSettle;
            view.nextSearch = clock + (settling ? kHostSearchEvery : kHostCheckEvery);
            FindHosts(view, element);
        }
        FollowHosts(view, now, tick, follower.softEdge);
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
    TryEnqueueUi(queue, [queue]() noexcept {
        try {
            auto* follower = new Follower();
            follower->queue = queue;
            follower->timer = queue.CreateTimer();
            follower->timer.Interval(kTick);
            OnUiTick(follower->timer, [follower](auto&&, auto&&) noexcept {
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

void SetLyricsFill(bool smooth, bool softEdge) noexcept {
    g_smooth.store(smooth);
    g_softEdge.store(softEdge);
}

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
