#include "lyrics_window.hpp"

#if defined(AMPIFY_WITH_XAML)

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Microsoft.UI.Composition.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Hosting.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <optional>
#include <vector>

#include "../core/lyrics_window.hpp"
#include "../core/ui_watchdog.hpp"

namespace ampify::win {

namespace {

namespace xaml = winrt::Microsoft::UI::Xaml;
namespace controls = xaml::Controls;
namespace media = xaml::Media;
namespace comp = winrt::Microsoft::UI::Composition;
namespace hosting = xaml::Hosting;

constexpr wchar_t kListName[] = L"TimeBasedLyricsItemsControl";  // Apple's list of synced lines (docs/themes/names.md)
constexpr wchar_t kFooterName[] = L"TimeBasedLyricsFooter";      // the writers' credits after the last line
constexpr int kSearchEvery = 4;                                   // ticks of a quarter second between two searches
constexpr std::size_t kSearchLimit = 3000;                        // elements walked in the player to find the list
constexpr std::size_t kLineSearchLimit = 40;                      // elements walked in a line to find its states
constexpr int kAreaSearchDepth = 12;                              // parents walked up from the lines to their scroller
constexpr auto kFade = std::chrono::milliseconds(250);            // a line fading in or out
constexpr auto kGlide = std::chrono::milliseconds(300);           // the block moving, as Apple's own scroll does
constexpr auto kLineAfterScroll = std::chrono::milliseconds(150);  // Apple lights a line 15 to 30 ms after its scroll

std::atomic<int> g_before{kAllLines};
std::atomic<int> g_after{kAllLines};

// Interface thread only; never destroyed: it holds XAML references, released with the process (xaml-lessons.md).
struct Window {
    winrt::weak_ref<controls::Panel> lines;  // the list's items panel, one container per line
    winrt::weak_ref<controls::ScrollViewer> scroller;  // Apple's scroller around them: Ampify moves it as a whole
    winrt::weak_ref<xaml::UIElement> footer;           // the credits after the last line
    std::vector<xaml::VisualStateGroup> groups;  // each line's group with Apple's "Current" state, by index
    std::vector<xaml::VisualStateGroup::CurrentStateChanged_revoker> watches;
    controls::ScrollViewer::ViewChanging_revoker scrolling;
    controls::ScrollViewer::ViewChanged_revoker scrolled;
    std::optional<double> scrollEnd;  // where Apple's scroll in flight ends, while one is
    SungLines sung;
    std::chrono::steady_clock::time_point sungAt;  // when the sung lines last changed
    std::chrono::steady_clock::time_point waitsForLine;  // until then the block stays put: a scroll's line is to light
    int ticks = 0;
    bool touched = false;  // Ampify faded lines or moved the block: they go back when the setting does
};

Window& State() {
    static Window* state = new Window();
    return *state;
}

bool IsCurrent(const xaml::VisualStateGroup& group) {
    xaml::VisualState state = group ? group.CurrentState() : nullptr;
    return state && state.Name() == L"Current";
}

// The states of a line: the group with a state named "Current", on its KaraokeLyricsLine a few levels down.
xaml::VisualStateGroup CurrentGroupOf(const xaml::DependencyObject& line) {
    std::vector<xaml::DependencyObject> queue{line};
    for (std::size_t i = 0; i < queue.size() && queue.size() < kLineSearchLimit; i++) {
        if (auto element = queue[i].try_as<xaml::FrameworkElement>()) {
            for (const xaml::VisualStateGroup& group : xaml::VisualStateManager::GetVisualStateGroups(element)) {
                for (const xaml::VisualState& state : group.States()) {
                    if (state.Name() == L"Current") return group;
                }
            }
        }
        int count = media::VisualTreeHelper::GetChildrenCount(queue[i]);
        for (int child = 0; child < count; child++) queue.push_back(media::VisualTreeHelper::GetChild(queue[i], child));
    }
    return nullptr;
}

xaml::FrameworkElement FindNamed(const xaml::DependencyObject& root, std::wstring_view name, std::size_t limit) {
    ampify::ActivityScope activity("full-screen lyrics search");
    std::vector<xaml::DependencyObject> queue{root};
    for (std::size_t i = 0; i < queue.size() && queue.size() < limit; i++) {
        if (auto element = queue[i].try_as<xaml::FrameworkElement>(); element && element.Name() == name) return element;
        int count = media::VisualTreeHelper::GetChildrenCount(queue[i]);
        for (int child = 0; child < count; child++) queue.push_back(media::VisualTreeHelper::GetChild(queue[i], child));
    }
    return nullptr;
}

void Apply(Window& state);

// One group per line, watched, so the lines follow Apple's sung line at once rather than at the next tick.
void WatchLines(Window& state, const controls::Panel& lines) {
    state.watches.clear();
    state.groups.clear();
    for (const xaml::UIElement& line : lines.Children()) {
        xaml::VisualStateGroup group = CurrentGroupOf(line);
        state.groups.push_back(group);
        if (!group) continue;
        state.watches.push_back(group.CurrentStateChanged(winrt::auto_revoke, [](auto&&, auto&&) noexcept {
            try {
                Apply(State());
            } catch (...) {
            }
        }));
    }
}

void Fade(const xaml::UIElement& element, bool shown) {
    double opacity = shown ? 1.0 : 0.0;
    if (element.Opacity() == opacity) return;
    if (!element.OpacityTransition()) {
        xaml::ScalarTransition fade;
        fade.Duration(kFade);
        element.OpacityTransition(fade);
    }
    element.Opacity(opacity);
    element.IsHitTestVisible(shown);  // a line faded out takes no click: a click on a lyrics line seeks there
}

// The block's shift, through its composition visual's Translation, gliding by an implicit animation. Not XAML's own
// Translation: a filter may hold the element's visual (src/win/element_blur.*, the lyrics' blur), which refuses
// XAML's then, and its copies follow this one.
void Move(const xaml::UIElement& block, double shift) {
    comp::Visual visual = hosting::ElementCompositionPreview::GetElementVisual(block);
    hosting::ElementCompositionPreview::SetIsTranslationEnabled(block, true);
    winrt::Windows::Foundation::Numerics::float3 now{};
    visual.Properties().TryGetVector3(L"Translation", now);
    if (std::abs(now.y - shift) < 0.5) return;
    if (!visual.ImplicitAnimations()) {
        comp::Compositor compositor = visual.Compositor();
        comp::Vector3KeyFrameAnimation glide = compositor.CreateVector3KeyFrameAnimation();
        glide.Target(L"Translation");
        glide.InsertExpressionKeyFrame(1.0f, L"this.FinalValue",
                                       compositor.CreateCubicBezierEasingFunction({0.2f, 0.0f}, {0.0f, 1.0f}));
        glide.Duration(kGlide);
        comp::ImplicitAnimationCollection animations = compositor.CreateImplicitAnimationCollection();
        animations.Insert(L"Translation", glide);
        visual.ImplicitAnimations(animations);
    }
    visual.Properties().InsertVector3(L"Translation", {now.x, static_cast<float>(shift), now.z});
}

float ShiftOf(const xaml::UIElement& block) {
    winrt::Windows::Foundation::Numerics::float3 now{};
    hosting::ElementCompositionPreview::GetElementVisual(block).Properties().TryGetVector3(L"Translation", now);
    return now.y;
}

// How far down `element` is laid out in `ancestor`: the sum of the layout offsets on the way up, which no scroll and
// no Translation changes.
std::optional<float> LayoutTop(const xaml::UIElement& element, const xaml::UIElement& ancestor) {
    float top = 0;
    xaml::DependencyObject at = element;
    for (int depth = 0; at && depth < kAreaSearchDepth * 2; depth++) {
        if (at == ancestor) return top;
        if (auto node = at.try_as<xaml::UIElement>()) top += node.ActualOffset().y;
        at = media::VisualTreeHelper::GetParent(at);
    }
    return std::nullopt;
}

// The scroller moves as a whole, so Apple keeps scrolling the sung line to its own place in it (about a third of the
// way down) with its own smooth scroll, and the block's shift only adds the distance from there to the middle. Moving
// the lines inside the scroller instead made Apple, which measures a line where it shows, scroll the extra distance
// too: the block jumped a screen away at each line and came back. The line is measured where Apple's scroll ends
// (its layout in the scrolled content, less the scroll's final offset), so the shift is known as the scroll starts
// and glides along with it, rather than catching up once it has ended.
double CentringShift(const Window& state, const controls::ScrollViewer& scroller, const controls::Panel& lines,
                     int before, int after) {
    auto children = lines.Children();
    const SungLines sung = state.sung;
    const bool singing = sung.first >= 0 && static_cast<uint32_t>(sung.last) < children.Size();
    if (!singing) return LyricsCentringShift({}, 0, before, after, false);
    auto content = scroller.Content().try_as<xaml::UIElement>();
    if (!content) return ShiftOf(scroller);
    xaml::UIElement last = children.GetAt(static_cast<uint32_t>(sung.last));
    auto top = LayoutTop(children.GetAt(static_cast<uint32_t>(sung.first)), content);
    auto lastTop = LayoutTop(last, content);
    if (!top || !lastTop) return ShiftOf(scroller);
    const double zoom = scroller.ZoomFactor();
    const double origin = content.ActualOffset().y;  // the content's margin in the scrolled extent
    const double end = state.scrollEnd.value_or(scroller.VerticalOffset());
    const SungSpan span{(origin + *top) * zoom - end, (origin + *lastTop + last.ActualSize().y) * zoom - end};
    return LyricsCentringShift(span, scroller.ActualHeight(), before, after, true);
}

void Recentre(Window& state) {
    controls::Panel lines = state.lines.get();
    controls::ScrollViewer scroller = state.scroller.get();
    if (!lines || !scroller || std::chrono::steady_clock::now() < state.waitsForLine) return;
    Move(scroller, CentringShift(state, scroller, lines, g_before.load(), g_after.load()));
}

void WatchScroll(Window& state, const controls::ScrollViewer& scroller) {
    using Changing = controls::ScrollViewerViewChangingEventArgs;
    using Changed = controls::ScrollViewerViewChangedEventArgs;
    state.scrolling = scroller.ViewChanging(winrt::auto_revoke, [](auto&&, const Changing& args) noexcept {
        try {
            Window& state = State();
            state.scrollEnd = args.FinalView().VerticalOffset();
            // Apple scrolls to a line a few frames before it lights it (1.1792). Centred at once on the sung line,
            // which is then the one ending, the block went a whole line down and came back as the new line lit:
            // only a scroll that comes after its line is followed at once, the others once their line lights.
            const auto now = std::chrono::steady_clock::now();
            const double ms = std::chrono::duration<double, std::milli>(now - state.sungAt).count();
            state.waitsForLine = ScrollFollowsSungLine(ms) ? now : now + kLineAfterScroll;
            if (state.touched) Recentre(state);
        } catch (...) {
        }
    });
    state.scrolled = scroller.ViewChanged(winrt::auto_revoke, [](auto&&, const Changed& args) noexcept {
        if (!args.IsIntermediate()) State().scrollEnd.reset();
    });
}

// Apple's scroller above the lines; the list itself is as tall as all its lines.
controls::ScrollViewer ScrollerOf(const controls::Panel& lines) {
    xaml::DependencyObject at = lines;
    for (int depth = 0; at && depth < kAreaSearchDepth; depth++) {
        if (auto scroller = at.try_as<controls::ScrollViewer>()) return scroller;
        at = media::VisualTreeHelper::GetParent(at);
    }
    return nullptr;
}

void Apply(Window& state) {
    controls::Panel lines = state.lines.get();
    if (!lines) return;
    const int before = g_before.load();
    const int after = g_after.load();
    const bool all = before == kAllLines && after == kAllLines;
    if (all && !state.touched) return;
    auto children = lines.Children();
    if (children.Size() != state.groups.size()) WatchLines(state, lines);
    SungLines sung;  // a duet can sing two lines at once: the first and the last of them
    for (std::size_t index = 0; index < state.groups.size(); index++) {
        if (!IsCurrent(state.groups[index])) continue;
        if (sung.first < 0) sung.first = static_cast<int>(index);
        sung.last = static_cast<int>(index);
    }
    for (uint32_t index = 0; index < children.Size(); index++) {
        Fade(children.GetAt(index), LyricLineShown(static_cast<int>(index), sung, before, after));
    }
    // The credits come with the last line, not over the lines above it.
    if (auto footer = state.footer.get(); footer && children.Size() > 0) {
        Fade(footer, LyricLineShown(static_cast<int>(children.Size()) - 1, sung, before, after));
    }
    if (sung.first != state.sung.first || sung.last != state.sung.last) {
        state.sungAt = std::chrono::steady_clock::now();
        state.waitsForLine = {};
    }
    state.sung = sung;
    state.touched = !all;
    Recentre(state);
}

}  // namespace

void FollowLyricsWindow(const xaml::UIElement& root) noexcept {
    try {
        Window& state = State();
        controls::Panel lines = state.lines.get();
        bool attached = lines && lines.XamlRoot();
        if (!attached) {
            const bool wanted = g_before.load() != kAllLines || g_after.load() != kAllLines;
            if (!wanted || !root || state.ticks++ % kSearchEvery != 0) return;
            auto list = FindNamed(root, kListName, kSearchLimit).try_as<controls::ItemsControl>();
            lines = list ? list.ItemsPanelRoot() : nullptr;
            controls::ScrollViewer scroller = lines ? ScrollerOf(lines) : nullptr;
            if (!scroller) return;
            state.lines = winrt::make_weak(lines);
            state.scroller = winrt::make_weak(scroller);
            auto footer = FindNamed(scroller, kFooterName, kSearchLimit);
            state.footer = footer ? winrt::make_weak(footer.as<xaml::UIElement>()) : winrt::weak_ref<xaml::UIElement>{};
            state.groups.clear();
            state.watches.clear();
            state.scrollEnd.reset();
            state.sung = {};
            state.touched = false;
            WatchScroll(state, scroller);
        }
        Apply(state);
    } catch (...) {
    }
}

void ReleaseLyricsWindow() noexcept {
    try {
        Window& state = State();
        if (controls::Panel lines = state.lines.get(); lines && state.touched) {
            for (const xaml::UIElement& line : lines.Children()) Fade(line, true);
            if (auto footer = state.footer.get()) Fade(footer, true);
            if (auto scroller = state.scroller.get()) Move(scroller, 0);
        }
        state.watches.clear();
        state.groups.clear();
        state.scrolling.revoke();
        state.scrolled.revoke();
        state.scrollEnd.reset();
        state.sung = {};
        state.lines = nullptr;
        state.scroller = nullptr;
        state.footer = nullptr;
        state.touched = false;
        state.ticks = 0;
    } catch (...) {
    }
}

void SetLyricsWindow(int before, int after) noexcept {
    g_before = before;
    g_after = after;
}

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
