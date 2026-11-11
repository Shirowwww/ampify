#include "lyrics_window.hpp"

#if defined(AMPIFY_WITH_XAML)

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <vector>

#include "../core/lyrics_window.hpp"
#include "../core/ui_watchdog.hpp"

namespace ampify::win {

namespace {

namespace xaml = winrt::Microsoft::UI::Xaml;
namespace controls = xaml::Controls;
namespace media = xaml::Media;

constexpr wchar_t kListName[] = L"TimeBasedLyricsItemsControl";  // Apple's list of synced lines (docs/themes/names.md)
constexpr int kSearchEvery = 4;                                   // ticks of a quarter second between two searches
constexpr std::size_t kSearchLimit = 3000;                        // elements walked in the player to find the list
constexpr std::size_t kLineSearchLimit = 40;                      // elements walked in a line to find its states
constexpr int kAreaSearchDepth = 12;                              // parents walked up from the lines to their scroller
constexpr auto kFade = std::chrono::milliseconds(250);            // a line fading in or out
constexpr auto kGlide = std::chrono::milliseconds(300);           // the block moving, as Apple's own scroll does

std::atomic<int> g_before{kAllLines};
std::atomic<int> g_after{kAllLines};

// Interface thread only; never destroyed: it holds XAML references, released with the process (xaml-lessons.md).
struct Window {
    winrt::weak_ref<controls::Panel> lines;  // the list's items panel, one container per line
    winrt::weak_ref<xaml::FrameworkElement> area;  // what shows the lines: the scroller around them, its height
    std::vector<xaml::VisualStateGroup> groups;  // each line's group with Apple's "Current" state, by index
    std::vector<xaml::VisualStateGroup::CurrentStateChanged_revoker> watches;
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

controls::ItemsControl FindList(const xaml::UIElement& root) {
    ampify::ActivityScope activity("full-screen lyrics search");
    std::vector<xaml::DependencyObject> queue{root};
    for (std::size_t i = 0; i < queue.size() && queue.size() < kSearchLimit; i++) {
        if (auto list = queue[i].try_as<controls::ItemsControl>(); list && list.Name() == kListName) return list;
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

void Fade(const xaml::UIElement& line, bool shown) {
    double opacity = shown ? 1.0 : 0.0;
    if (line.Opacity() == opacity) return;
    if (!line.OpacityTransition()) {
        xaml::ScalarTransition fade;
        fade.Duration(kFade);
        line.OpacityTransition(fade);
    }
    line.Opacity(opacity);
    line.IsHitTestVisible(shown);  // a line faded out takes no click: a click on a lyrics line seeks there
}

void Move(const controls::Panel& lines, double shift) {
    auto now = lines.Translation();
    if (std::abs(now.y - shift) < 0.5) return;
    if (!lines.TranslationTransition()) {
        xaml::Vector3Transition glide;
        glide.Duration(kGlide);
        lines.TranslationTransition(glide);
    }
    lines.Translation({now.x, static_cast<float>(shift), now.z});
}

// Where the sung lines stand in the list's area, without the shift Ampify gave the lines (their Translation counts in
// TransformToVisual), and the shift that brings them to its middle. Read on every tick: Apple's own scroll to a new
// line takes a moment, and the shift follows it until it settles.
double CentringShift(const Window& state, const controls::Panel& lines, SungLines sung, int before, int after) {
    xaml::FrameworkElement list = state.area.get();
    auto children = lines.Children();
    const bool singing = list && sung.first >= 0 && static_cast<uint32_t>(sung.last) < children.Size();
    if (!singing) return LyricsCentringShift({}, 0, before, after, false);
    const float moved = lines.Translation().y;
    auto edge = [&](uint32_t index, bool bottom) {
        xaml::UIElement line = children.GetAt(index);
        const float y = bottom ? line.ActualSize().y : 0.0f;
        return line.TransformToVisual(list).TransformPoint({0, y}).Y - moved;
    };
    SungSpan span{edge(static_cast<uint32_t>(sung.first), false), edge(static_cast<uint32_t>(sung.last), true)};
    return LyricsCentringShift(span, list.ActualHeight(), before, after, true);
}

// The lyrics area: the first scroller above the lines (Apple scrolls them in it; the list itself is as tall as all
// its lines), else the list.
xaml::FrameworkElement AreaOf(const controls::Panel& lines, const controls::ItemsControl& list) {
    xaml::DependencyObject at = lines;
    for (int depth = 0; at && depth < kAreaSearchDepth; depth++) {
        if (auto scroller = at.try_as<controls::ScrollViewer>()) return scroller;
        at = media::VisualTreeHelper::GetParent(at);
    }
    return list;
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
    Move(lines, CentringShift(state, lines, sung, before, after));
    state.touched = !all;
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
            controls::ItemsControl list = FindList(root);
            lines = list ? list.ItemsPanelRoot() : nullptr;
            if (!lines) return;
            state.area = winrt::make_weak(AreaOf(lines, list));
            state.lines = winrt::make_weak(lines);
            state.groups.clear();
            state.watches.clear();
            state.touched = false;
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
            Move(lines, 0);
        }
        state.watches.clear();
        state.groups.clear();
        state.lines = nullptr;
        state.area = nullptr;
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
