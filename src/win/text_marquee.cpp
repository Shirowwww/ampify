#include "text_marquee.hpp"

#if defined(AMPIFY_WITH_XAML)

#include "win_common.hpp"
#include "xaml_chrome.hpp"

#include "../core/journal.hpp"

#ifdef GetCurrentTime
#undef GetCurrentTime  // windows.h macro that breaks Storyboard::GetCurrentTime in the projection
#endif

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.Numerics.h>
#include <winrt/Windows.UI.Text.h>
#include <winrt/Microsoft.UI.Composition.h>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Microsoft.UI.Windowing.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Data.h>
#include <winrt/Microsoft.UI.Xaml.Hosting.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace ampify::win {

namespace {

namespace xaml = winrt::Microsoft::UI::Xaml;
namespace controls = xaml::Controls;
namespace primitives = xaml::Controls::Primitives;
namespace media = xaml::Media;
namespace comp = winrt::Microsoft::UI::Composition;
namespace dispatching = winrt::Microsoft::UI::Dispatching;
using winrt::Windows::Foundation::IInspectable;

constexpr auto kEvaluateEvery = std::chrono::milliseconds(100);  // hit tests while the pointer moves, at most
constexpr double kSpeed = 40;           // pixels a second, slow enough to read
constexpr double kRestStart = 0.9;      // seconds at the start before it moves
constexpr double kRestEnd = 1.2;        // seconds at the end
constexpr double kBackSpeed = 160;      // pixels a second on the way back
constexpr double kMinTravel = 0.6;      // seconds, for a text cut by a few letters
constexpr double kSlack = 2;            // pixels past the measured width, for rounding and the last glyph's overhang
constexpr double kMinCut = 3;           // a text cut by less shows whole enough
constexpr int kMaxTexts = 4;            // cut texts of one row that scroll together
constexpr int kRowSearchLimit = 300;    // elements looked at under a row

std::atomic<bool> g_enabled{true};  // apple.marquee, set from the settings' thread

// One text that scrolls, and what it had before: its local margin and trimming (or UnsetValue), its visual's clip.
struct Running {
    controls::TextBlock text{nullptr};
    IInspectable margin{nullptr};
    IInspectable trimming{nullptr};
    comp::Visual visual{nullptr};
    comp::CompositionClip clipBefore{nullptr};
    comp::CompositionPropertySet distance{nullptr};
    comp::InsetClip clip{nullptr};
    int64_t textToken = 0;
};

// An element with the handlers: the main window's content (Apple's) or a popup's child (Apple's or Ampify's).
struct Hooked {
    xaml::UIElement element{nullptr};
    controls::Primitives::Popup popup{nullptr};  // null for the window's content
};

// Interface thread only. Never freed: releasing WinRT objects while the process exits can crash it.
struct Marquee {
    bool told = false;
    IInspectable moved{nullptr};   // the boxed handlers, kept to take them off again
    IInspectable exited{nullptr};
    IInspectable wheel{nullptr};
    std::vector<Hooked> hooked;
    IInspectable anchor{nullptr};  // the text or row the pointer is on, held while it is
    std::vector<Running> running;
    xaml::UIElement lastRoot{nullptr};
    winrt::Windows::Foundation::Point lastPoint{};
    std::chrono::steady_clock::time_point lastEvaluation{};
    dispatching::DispatcherQueueTimer trailing{nullptr};  // the last move of a burst is looked at too
    dispatching::DispatcherQueueTimer watch{nullptr};     // the real cursor, while a text scrolls
    xaml::UIElement content{nullptr};                     // the main window's content, and its window
    HWND window = nullptr;
};

Marquee& State() {
    static Marquee* state = new Marquee();
    return *state;
}

bool Scrollable(const controls::TextBlock& text) {
    if (!text.IsTextTrimmed() || text.TextWrapping() != xaml::TextWrapping::NoWrap) return false;
    if (text.Name() == L"ScrollingText") return false;  // Apple's ticker tape scrolls on its own
    auto alignment = text.HorizontalAlignment();
    bool leftEdge = alignment == xaml::HorizontalAlignment::Left || alignment == xaml::HorizontalAlignment::Stretch;
    return leftEdge && text.FlowDirection() == xaml::FlowDirection::LeftToRight && text.ActualWidth() > 8;
}

// The width of the whole text on one line, measured on the text itself, untrimmed: a copy outside the tree came out
// about 15 px short, which left the end cut. The margin it is about to get asks for a new layout anyway.
double NaturalWidth(const controls::TextBlock& text) {
    constexpr float kOpen = std::numeric_limits<float>::infinity();
    text.Measure({kOpen, kOpen});
    xaml::Thickness margin = text.Margin();
    return text.DesiredSize().Width - margin.Left - margin.Right;
}

// One round trip over `cut` pixels, repeated: rest, move to the end, rest, back.
comp::ScalarKeyFrameAnimation RoundTrip(const comp::Compositor& compositor, float cut) {
    double travel = std::max(kMinTravel, cut / kSpeed);
    double back = std::max(kMinTravel / 2, cut / kBackSpeed);
    double total = kRestStart + travel + kRestEnd + back;
    auto at = [total](double seconds) { return static_cast<float>(seconds / total); };
    auto linear = compositor.CreateLinearEasingFunction();
    comp::ScalarKeyFrameAnimation animation = compositor.CreateScalarKeyFrameAnimation();
    animation.InsertKeyFrame(0.0f, 0.0f, linear);
    animation.InsertKeyFrame(at(kRestStart), 0.0f, linear);
    animation.InsertKeyFrame(at(kRestStart + travel), cut, linear);
    animation.InsertKeyFrame(at(kRestStart + travel + kRestEnd), cut, linear);
    animation.InsertKeyFrame(1.0f, 0.0f);  // the compositor's default easing: eases back
    animation.Duration(std::chrono::duration_cast<winrt::Windows::Foundation::TimeSpan>(std::chrono::duration<double>(total)));
    animation.IterationBehavior(comp::AnimationIterationBehavior::Forever);
    return animation;
}

void PutBack(const controls::TextBlock& text, const xaml::DependencyProperty& property, const IInspectable& value) noexcept {
    try {
        PutBackLocal(text, property, value);
    } catch (...) {  // a text gone from the tree: nothing left to put back
    }
}

// Each step on its own, the text first: whatever fails after, it is whole and cut with "…" again. A run that Start left
// half made has null members, skipped.
void Stop(Running& run) noexcept {
    PutBack(run.text, xaml::FrameworkElement::MarginProperty(), run.margin);
    PutBack(run.text, controls::TextBlock::TextTrimmingProperty(), run.trimming);
    try {
        if (run.textToken) run.text.UnregisterPropertyChangedCallback(controls::TextBlock::TextProperty(), run.textToken);
    } catch (...) {
        // The element may be torn down already: nothing is left to put back.
    }
    try {
        if (run.distance) run.distance.StopAnimation(L"d");
        if (run.clip) {
            run.clip.StopAnimation(L"LeftInset");
            run.clip.StopAnimation(L"RightInset");
        }
        if (run.visual) {
            run.visual.StopAnimation(L"Translation.X");
            run.visual.Properties().InsertVector3(L"Translation", {0, 0, 0});
            run.visual.Clip(run.clipBefore);
        }
    } catch (...) {
        // The element may be torn down already: nothing is left to put back.
    }
}

void StopAll(Marquee& state) {
    for (Running& run : state.running) Stop(run);
    state.running.clear();
    state.anchor = nullptr;
    if (state.watch) state.watch.Stop();
}

// The text, untrimmed, lies past its cell by a negative margin, so its whole line is laid out; a clip on its visual
// keeps the cell's width in view while the visual slides under it. Both follow one distance on the compositor.
void Run(Marquee& state, Running& run, double width);

void Start(Marquee& state, const controls::TextBlock& text) {
    double width = text.ActualWidth();
    Running run;
    run.text = text;
    run.margin = text.ReadLocalValue(xaml::FrameworkElement::MarginProperty());
    run.trimming = text.ReadLocalValue(controls::TextBlock::TextTrimmingProperty());
    // A bound margin or trimming could not be put back as it was: such a text is left alone.
    if (run.margin.try_as<xaml::Data::BindingExpressionBase>() || run.trimming.try_as<xaml::Data::BindingExpressionBase>()) return;
    try {
        Run(state, run, width);
    } catch (...) {
        Stop(run);
    }
}

void Run(Marquee& state, Running& run, double width) {
    const controls::TextBlock& text = run.text;
    text.TextTrimming(xaml::TextTrimming::None);
    double cut = std::ceil(NaturalWidth(text) - width + kSlack);
    if (cut < kMinCut + kSlack) return Stop(run);
    xaml::Thickness margin = text.Margin();
    text.Margin({margin.Left, margin.Top, margin.Right - cut, margin.Bottom});
    xaml::Hosting::ElementCompositionPreview::SetIsTranslationEnabled(text, true);
    run.visual = xaml::Hosting::ElementCompositionPreview::GetElementVisual(text);
    comp::Compositor compositor = run.visual.Compositor();
    run.clipBefore = run.visual.Clip();
    run.distance = compositor.CreatePropertySet();
    run.distance.InsertScalar(L"d", 0.0f);
    run.clip = compositor.CreateInsetClip();
    run.visual.Clip(run.clip);
    auto follow = [&](const wchar_t* expression) {
        comp::ExpressionAnimation animation = compositor.CreateExpressionAnimation(expression);
        animation.SetReferenceParameter(L"p", run.distance);
        animation.SetReferenceParameter(L"v", run.visual);
        animation.SetScalarParameter(L"w", static_cast<float>(width));
        return animation;
    };
    run.visual.StartAnimation(L"Translation.X", follow(L"-p.d"));
    run.clip.StartAnimation(L"LeftInset", follow(L"p.d"));
    run.clip.StartAnimation(L"RightInset", follow(L"Max(0, v.Size.X - p.d - w)"));
    run.distance.StartAnimation(L"d", RoundTrip(compositor, static_cast<float>(cut)));
    // A row Apple recycles for another song gets its new text: the scroll stops there.
    run.textToken = text.RegisterPropertyChangedCallback(
        controls::TextBlock::TextProperty(), [](xaml::DependencyObject const&, xaml::DependencyProperty const&) noexcept {
            try {
                StopAll(State());
            } catch (...) {
                // A handler runs inside Apple Music's XAML frame: nothing may escape it.
            }
        });
    state.running.push_back(run);
}

// Every cut text of a row, a card or a button, a few at most.
std::vector<controls::TextBlock> CutTextsUnder(const xaml::DependencyObject& root) {
    std::vector<controls::TextBlock> texts;
    std::vector<xaml::DependencyObject> queue{root};
    for (size_t i = 0; i < queue.size() && i < static_cast<size_t>(kRowSearchLimit); ++i) {
        if (auto text = queue[i].try_as<controls::TextBlock>(); text && Scrollable(text)) {
            texts.push_back(text);
            if (texts.size() == static_cast<size_t>(kMaxTexts)) break;
        }
        int count = media::VisualTreeHelper::GetChildrenCount(queue[i]);
        for (int child = 0; child < count; ++child) queue.push_back(media::VisualTreeHelper::GetChild(queue[i], child));
    }
    return texts;
}

bool IsRunning(const Marquee& state, const xaml::UIElement& element) {
    return std::any_of(state.running.begin(), state.running.end(), [&](const Running& run) { return run.text == element; });
}

void Evaluate(Marquee& state);

// A pointer that leaves in one jump, or a window that comes over Apple Music, may send no PointerExited: while a text
// scrolls, the real cursor is looked at. Over another process the scroll stops; over the main window it is hit tested.
void CheckPointer(Marquee& state) {
    std::optional<CursorSpot> spot = CursorOver(state.window);
    if (!spot) return;
    if (!spot->ourProcess) return StopAll(state);
    // A popup closed under the pointer (a menu item chosen, Escape) sends none either, nor a text Apple took away.
    bool gone = std::any_of(state.running.begin(), state.running.end(),
                            [](const Running& run) { return !run.text.IsLoaded() || !run.text.XamlRoot(); });
    if (gone) return StopAll(state);
    if (!state.content || !state.window) return;
    if (state.lastRoot != state.content) {
        auto hooked = std::find_if(state.hooked.begin(), state.hooked.end(),
                                   [&](const Hooked& h) { return h.element == state.lastRoot; });
        if (hooked == state.hooked.end() || !hooked->popup || !hooked->popup.IsOpen()) StopAll(state);
        return;
    }
    if (spot->root != state.window || !spot->client) return StopAll(state);
    ClientPixel cursor = *spot->client;
    xaml::XamlRoot root = state.content.XamlRoot();
    double scale = root ? root.RasterizationScale() : 1.0;
    state.lastPoint = {static_cast<float>(cursor.x / scale), static_cast<float>(cursor.y / scale)};
    Evaluate(state);
}

void Watch(Marquee& state) {
    if (!state.watch) {
        state.watch = dispatching::DispatcherQueue::GetForCurrentThread().CreateTimer();
        state.watch.Interval(kCursorPollEvery);  // the real cursor, while a text scrolls
        state.watch.Tick([](auto const&, auto const&) noexcept {
            try {
                Marquee& state = State();
                if (state.running.empty()) state.watch.Stop();
                else CheckPointer(state);
            } catch (...) {
                // A handler runs inside Apple Music's XAML frame: nothing may escape it.
            }
        });
    }
    if (!state.watch.IsRunning()) state.watch.Start();
}

// What the pointer is on: the first cut text under it, or else the row, card or button it is in, topmost first.
void Evaluate(Marquee& state) {
    state.lastEvaluation = std::chrono::steady_clock::now();
    if (!g_enabled) return StopAll(state);  // turned off since the handlers' last check
    if (!state.lastRoot) return;
    IInspectable anchor{nullptr};
    std::vector<controls::TextBlock> texts;
    for (const xaml::UIElement& element :
         media::VisualTreeHelper::FindElementsInHostCoordinates(state.lastPoint, state.lastRoot)) {
        // Still on what scrolls: a text that scrolls is no longer cut, and wider, so it must not count as new.
        if (element == state.anchor || IsRunning(state, element)) return;
        if (auto text = element.try_as<controls::TextBlock>()) {
            if (!Scrollable(text)) continue;
            anchor = text;
            texts = {text};
            break;
        }
        if (element.try_as<primitives::SelectorItem>() || element.try_as<primitives::ButtonBase>()) {
            anchor = element;  // even with no cut text: a row is as far up as it looks
            texts = CutTextsUnder(element);
            break;
        }
    }
    if (anchor == state.anchor) return;
    StopAll(state);
    state.anchor = anchor;
    for (const controls::TextBlock& text : texts) Start(state, text);
    if (!state.running.empty()) Watch(state);
}

void OnMoved(const IInspectable& sender, const xaml::Input::PointerRoutedEventArgs& args) {
    Marquee& state = State();
    state.lastRoot = sender.try_as<xaml::UIElement>();
    state.lastPoint = args.GetCurrentPoint(nullptr).Position();
    if (std::chrono::steady_clock::now() - state.lastEvaluation >= kEvaluateEvery) return Evaluate(state);
    if (!state.trailing) {
        state.trailing = dispatching::DispatcherQueue::GetForCurrentThread().CreateTimer();
        state.trailing.IsRepeating(false);
        state.trailing.Interval(kEvaluateEvery);
        state.trailing.Tick([](auto const&, auto const&) noexcept {
            try {
                Evaluate(State());
            } catch (...) {
                // A handler runs inside Apple Music's XAML frame: nothing may escape it.
            }
        });
    }
    if (!state.trailing.IsRunning()) state.trailing.Start();
}

void MakeHandlers(Marquee& state) {
    if (state.moved) return;
    state.moved = winrt::box_value(xaml::Input::PointerEventHandler(
        [](IInspectable const& sender, xaml::Input::PointerRoutedEventArgs const& args) noexcept {
            try {
                OnMoved(sender, args);
            } catch (...) {
                // A handler runs inside Apple Music's XAML frame: nothing may escape it.
            }
        }));
    auto stop = []() {
        Marquee& state = State();
        if (state.trailing) state.trailing.Stop();
        StopAll(state);
    };
    // PointerExited bubbles: a child the pointer leaves for its neighbour must not stop the row, only the window or
    // the popup itself (the watch catches a pointer that leaves without it).
    state.exited = winrt::box_value(xaml::Input::PointerEventHandler(
        [stop](IInspectable const& sender, xaml::Input::PointerRoutedEventArgs const& args) noexcept {
            try {
                if (args.OriginalSource() == sender) stop();
            } catch (...) {
                // A handler runs inside Apple Music's XAML frame: nothing may escape it.
            }
        }));
    state.wheel = winrt::box_value(xaml::Input::PointerEventHandler(  // a list that scrolls moves the rows
        [stop](IInspectable const&, xaml::Input::PointerRoutedEventArgs const&) noexcept {
            try {
                stop();
            } catch (...) {
                // A handler runs inside Apple Music's XAML frame: nothing may escape it.
            }
        }));
}

void Hook(Marquee& state, const xaml::UIElement& element, const primitives::Popup& popup) {
    element.AddHandler(xaml::UIElement::PointerMovedEvent(), state.moved, true);
    element.AddHandler(xaml::UIElement::PointerExitedEvent(), state.exited, true);
    element.AddHandler(xaml::UIElement::PointerWheelChangedEvent(), state.wheel, true);
    state.hooked.push_back({element, popup});
}

void Unhook(Marquee& state, const Hooked& hooked) {
    try {
        hooked.element.RemoveHandler(xaml::UIElement::PointerMovedEvent(), state.moved);
        hooked.element.RemoveHandler(xaml::UIElement::PointerExitedEvent(), state.exited);
        hooked.element.RemoveHandler(xaml::UIElement::PointerWheelChangedEvent(), state.wheel);
    } catch (...) {
        // The element may be torn down already: nothing is left to put back.
    }
}

bool IsHooked(const Marquee& state, const xaml::UIElement& element) {
    return std::any_of(state.hooked.begin(), state.hooked.end(), [&](const Hooked& h) { return h.element == element; });
}

// The window's content once, and each open popup's child; a closed popup's child is let go.
void HookAll(Marquee& state, const xaml::UIElement& content) {
    std::erase_if(state.hooked, [&](const Hooked& hooked) {
        bool closed = hooked.popup && (!hooked.popup.IsOpen() || hooked.popup.Child() != hooked.element);
        bool replaced = !hooked.popup && hooked.element != content;
        if (closed || replaced) Unhook(state, hooked);
        return closed || replaced;
    });
    if (!IsHooked(state, content)) Hook(state, content, nullptr);
    if (!content.XamlRoot()) return;
    for (const primitives::Popup& popup : media::VisualTreeHelper::GetOpenPopupsForXamlRoot(content.XamlRoot())) {
        if (auto child = popup.Child(); child && !IsHooked(state, child)) Hook(state, child, popup);
    }
}

}  // namespace

void KeepTextMarquee(Journal& journal) noexcept {
    try {
        Marquee& state = State();
        if (!g_enabled) {
            if (state.trailing) state.trailing.Stop();
            StopAll(state);
            for (const Hooked& hooked : state.hooked) Unhook(state, hooked);
            state.hooked.clear();
            return;
        }
        xaml::Window window = AppWindowOf(WindowRole::Main);
        xaml::UIElement content = window ? window.Content() : nullptr;
        if (!content) return;
        state.content = content;
        state.window = WindowHandle(window);
        MakeHandlers(state);
        HookAll(state, content);
        if (!state.told) journal.Write("Text marquee: a text cut with an ellipsis scrolls under the pointer");
        state.told = true;
    } catch (...) {
        // Nothing may escape into Apple Music's XAML frame.
    }
}

void SetTextMarquee(bool enabled) noexcept {
    g_enabled = enabled;
}

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
