// Written against the C++/WinRT projection of the Windows App SDK, like xaml_backend.cpp.

#if defined(AMPIFY_WITH_XAML)

#include "theme_animations.hpp"

#include <algorithm>
#include <chrono>
#include <optional>

#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Windows.Foundation.Numerics.h>
#include <winrt/Windows.UI.ViewManagement.h>

// After the projection: its macros (GetCurrentTime, GetClassName) would rename members of WinRT types.
#include <windows.h>

#include "../core/now_playing.hpp"
#include "../core/theme/animation.hpp"

namespace ampify::win {

namespace {

namespace th = ampify::theme;
namespace composition = winrt::Microsoft::UI::Composition;
namespace xaml = winrt::Microsoft::UI::Xaml;
namespace controls = winrt::Microsoft::UI::Xaml::Controls;
namespace media = winrt::Microsoft::UI::Xaml::Media;
using winrt::Windows::Foundation::Numerics::float2;
using winrt::Windows::Foundation::Numerics::float3;
using Channel = th::AnimationTrack::Channel;

constexpr wchar_t kStartingValue[] = L"this.StartingValue";
// Steps a second of an endless animation: smooth for a slow turn or a pulse, a fraction of what a 240 Hz screen draws.
constexpr double kLoopFps = 30;
constexpr double kHiddenCheckMs = 500;  // how often a hidden or minimized window is looked at again

winrt::Windows::Foundation::TimeSpan Span(double ms) {
    return std::chrono::duration_cast<winrt::Windows::Foundation::TimeSpan>(std::chrono::duration<double, std::milli>(ms));
}

const wchar_t* TargetOf(Channel channel) {
    switch (channel) {
        case Channel::Scale: return L"Scale";
        case Channel::Rotation: return L"Rotation";
        case Channel::Translation: return L"Translation";
        case Channel::Opacity:
        case Channel::Background:  // never built for the compositor: it is stepped
            break;
    }
    return L"Opacity";
}

// The brush an element draws its background with, for the kinds of element that have one.
media::Brush BackgroundBrush(const xaml::UIElement& element) {
    if (auto control = element.try_as<controls::Control>()) return control.Background();
    if (auto panel = element.try_as<controls::Panel>()) return panel.Background();
    if (auto border = element.try_as<controls::Border>()) return border.Background();
    if (auto presenter = element.try_as<controls::ContentPresenter>()) return presenter.Background();
    return nullptr;
}

bool SetBackgroundBrush(const xaml::UIElement& element, const media::Brush& brush) {
    if (auto control = element.try_as<controls::Control>()) { control.Background(brush); return true; }
    if (auto panel = element.try_as<controls::Panel>()) { panel.Background(brush); return true; }
    if (auto border = element.try_as<controls::Border>()) { border.Background(brush); return true; }
    if (auto presenter = element.try_as<controls::ContentPresenter>()) { presenter.Background(brush); return true; }
    return false;
}

// The colour of a plain background as one number 0xAARRGGBB; transparent for any other brush or none.
double PackedBackground(const xaml::UIElement& element) {
    auto solid = BackgroundBrush(element).try_as<media::SolidColorBrush>();
    if (!solid) return 0;
    const winrt::Windows::UI::Color color = solid.Color();
    return static_cast<double>((static_cast<std::uint32_t>(color.A) << 24) | (static_cast<std::uint32_t>(color.R) << 16) |
                               (static_cast<std::uint32_t>(color.G) << 8) | static_cast<std::uint32_t>(color.B));
}

winrt::Windows::UI::Color ColorOfPacked(double packed) {
    const auto value = static_cast<std::uint32_t>(packed);
    return {static_cast<std::uint8_t>(value >> 24), static_cast<std::uint8_t>(value >> 16),
            static_cast<std::uint8_t>(value >> 8), static_cast<std::uint8_t>(value)};
}

bool HasBackground(const th::Animation& animation) {
    return std::any_of(animation.tracks.begin(), animation.tracks.end(),
                       [](const th::AnimationTrack& track) { return track.channel == Channel::Background; });
}

composition::AnimationDirection DirectionOf(th::Animation::Direction direction) {
    switch (direction) {
        case th::Animation::Direction::Reverse: return composition::AnimationDirection::Reverse;
        case th::Animation::Direction::Alternate: return composition::AnimationDirection::Alternate;
        case th::Animation::Direction::AlternateReverse: return composition::AnimationDirection::AlternateReverse;
        case th::Animation::Direction::Normal: break;
    }
    return composition::AnimationDirection::Normal;
}

// The keyframes of one track. A track without a point at 0 or at 1 starts or ends at the value the element had, as in
// CSS. `depth` is the Z of the element's Translation, which a box-shadow sets and a moving element keeps.
composition::KeyFrameAnimation Keyframes(const composition::Compositor& compositor, const th::AnimationTrack& track,
                                         const composition::CompositionEasingFunction& easing, float depth) {
    const bool fromOwn = track.points.front().offset > 0;
    const bool toOwn = track.points.back().offset < 1;
    if (track.channel == Channel::Opacity || track.channel == Channel::Rotation) {
        composition::ScalarKeyFrameAnimation scalar = compositor.CreateScalarKeyFrameAnimation();
        if (fromOwn) scalar.InsertExpressionKeyFrame(0.f, kStartingValue);
        for (const th::AnimationTrack::Point& point : track.points) {
            scalar.InsertKeyFrame(static_cast<float>(point.offset), static_cast<float>(point.x), easing);
        }
        if (toOwn) scalar.InsertExpressionKeyFrame(1.f, kStartingValue, easing);
        return scalar;
    }
    composition::Vector3KeyFrameAnimation vector = compositor.CreateVector3KeyFrameAnimation();
    const float z = track.channel == Channel::Scale ? 1.f : depth;
    if (fromOwn) vector.InsertExpressionKeyFrame(0.f, kStartingValue);
    for (const th::AnimationTrack::Point& point : track.points) {
        float3 value(static_cast<float>(point.x), static_cast<float>(point.y), z);
        vector.InsertKeyFrame(static_cast<float>(point.offset), value, easing);
    }
    if (toOwn) vector.InsertExpressionKeyFrame(1.f, kStartingValue, easing);
    return vector;
}

// One animation of the theme: a composition animation per track, all with its timing. Stopped, each gives the element
// the value it had before.
std::vector<composition::CompositionAnimation> Build(const composition::Compositor& compositor,
                                                     const th::Animation& animation, float depth) {
    const std::array<double, 4>& curve = animation.easing;
    composition::CubicBezierEasingFunction easing = compositor.CreateCubicBezierEasingFunction(
        float2(static_cast<float>(curve[0]), static_cast<float>(curve[1])),
        float2(static_cast<float>(curve[2]), static_cast<float>(curve[3])));
    std::vector<composition::CompositionAnimation> parts;
    for (const th::AnimationTrack& track : animation.tracks) {
        if (track.points.empty() || track.channel == Channel::Background) continue;
        composition::KeyFrameAnimation keyframes = Keyframes(compositor, track, easing, depth);
        keyframes.Duration(Span(animation.durationMs));
        keyframes.DelayTime(Span(animation.delayMs));
        keyframes.DelayBehavior(animation.fillBackwards ? composition::AnimationDelayBehavior::SetInitialValueBeforeDelay
                                                        : composition::AnimationDelayBehavior::SetInitialValueAfterDelay);
        keyframes.Direction(DirectionOf(animation.direction));
        keyframes.StopBehavior(composition::AnimationStopBehavior::SetToInitialValue);
        if (animation.iterations > 0 && animation.fillForwards) {
            keyframes.IterationBehavior(composition::AnimationIterationBehavior::Count);
            keyframes.IterationCount(animation.iterations);
        } else {
            keyframes.IterationBehavior(composition::AnimationIterationBehavior::Forever);
        }
        keyframes.Target(TargetOf(track.channel));
        parts.push_back(keyframes);
    }
    return parts;
}

th::ChannelValue OwnValue(const xaml::UIElement& element, Channel channel) {
    switch (channel) {
        case Channel::Scale: return {channel, element.Scale().x, element.Scale().y};
        case Channel::Rotation: return {channel, element.Rotation(), 0};
        case Channel::Translation: return {channel, element.Translation().x, element.Translation().y};
        case Channel::Background: return {channel, PackedBackground(element), 0};
        case Channel::Opacity: break;
    }
    return {channel, element.Opacity(), 0};
}

// The element's own value of a channel, from what StartLoops noted down.
th::ChannelValue OwnChannel(const std::vector<th::ChannelValue>& own, Channel channel) {
    for (const th::ChannelValue& value : own) {
        if (value.channel == channel) return value;
    }
    const double neutral = channel == Channel::Scale ? 1 : 0;
    return {channel, neutral, neutral};
}

void Apply(const xaml::UIElement& element, const std::vector<th::ChannelValue>& values) {
    for (const th::ChannelValue& value : values) {
        const float x = static_cast<float>(value.x);
        const float y = static_cast<float>(value.y);
        switch (value.channel) {
            case Channel::Scale: element.Scale(float3(x, y, 1.f)); break;
            case Channel::Rotation: element.Rotation(x); break;
            case Channel::Translation: element.Translation(float3(x, y, element.Translation().z)); break;
            case Channel::Background: SetBackgroundBrush(element, media::SolidColorBrush(ColorOfPacked(value.x))); break;
            case Channel::Opacity: element.Opacity(x); break;
        }
    }
}

bool Turns(const std::vector<th::Animation>& animations) {
    for (const th::Animation& animation : animations) {
        for (const th::AnimationTrack& track : animation.tracks) {
            if (track.channel == Channel::Scale || track.channel == Channel::Rotation) return true;
        }
    }
    return false;
}

}  // namespace

bool ThemeAnimations::Write(theme::NodeId id, const xaml::UIElement& element, const std::string& text,
                            winrt::Windows::Foundation::Point origin) {
    if (auto known = m_running.find(id); known != m_running.end()) {
        Stop(known->second);
        m_running.erase(known);
    }
    if (text.empty()) return true;
    std::optional<std::vector<th::Animation>> animations = th::DecodeAnimations(text);
    if (!animations) return false;

    Running running;
    running.element = element;
    if (!running.element.get()) running.held = element;  // made by Ampify: held strongly until Forget
    running.text = text;
    if (!MotionAllowed()) {
        m_running.emplace(id, std::move(running));  // kept as written, so it is not tried again on every pass
        return true;
    }
    try {
        composition::Compositor compositor = xaml::Media::CompositionTarget::GetCompositorForCurrentThread();
        if (Turns(*animations)) {
            Center(running, compositor, element, origin);
            if (!(origin.X == 0 && origin.Y == 0)) running.origin = origin;
        }
        std::vector<th::Animation> loops;
        for (const th::Animation& animation : *animations) {
            if (animation.iterations == 0 || HasBackground(animation)) {  // stepped: see StepLoops
                loops.push_back(animation);
                continue;
            }
            std::vector<composition::CompositionAnimation> parts = Build(compositor, animation, element.Translation().z);
            for (const composition::CompositionAnimation& part : parts) {
                element.StartAnimation(part);
                running.animations.push_back(part);
            }
            if (animation.iterations > 0 && !animation.fillForwards) {
                EndAt(running, animation.delayMs + animation.durationMs * animation.iterations, std::move(parts));
            }
        }
        if (std::any_of(loops.begin(), loops.end(), HasBackground)) {
            running.ownBackground = BackgroundBrush(element);  // what comes back when the colours stop
            running.backgroundAnimated = true;
        }
        if (!loops.empty()) StartLoops(running, std::move(loops));
    } catch (...) {
        Stop(running);  // an element Apple Music drives through its composition visual refuses these properties
        return false;
    }
    m_running.emplace(id, std::move(running));
    return true;
}

// False when "Animation effects" is off in Windows' settings (Accessibility > Visual effects): a person who turned
// motion off does not get a theme's either.
bool ThemeAnimations::MotionAllowed() {
    try {
        if (!m_settings) m_settings = winrt::Windows::UI::ViewManagement::UISettings();
        return m_settings.AnimationsEnabled();
    } catch (...) {
        return true;
    }
}

ThemeAnimations::~ThemeAnimations() {
    m_alive.reset();
    if (m_loopTimer) m_loopTimer.Stop();
}

void ThemeAnimations::StartLoops(Running& running, std::vector<th::Animation> loops) {
    xaml::UIElement element = Target(running);
    if (!element) return;
    for (const th::Animation& loop : loops) {
        for (const th::AnimationTrack& track : loop.tracks) {
            bool known = std::any_of(running.own.begin(), running.own.end(),
                                     [&](const th::ChannelValue& value) { return value.channel == track.channel; });
            if (!known) running.own.push_back(OwnValue(element, track.channel));
        }
    }
    running.loops = std::move(loops);
    running.started = std::chrono::steady_clock::now();
    running.lastStep = running.started;
    running.heldMs = 0;
    running.stillShown = false;
    if (Turns(running.loops) && running.animations.empty()) {
        // Nothing else turns the element: the centre follows its size at each step, not the expression.
        if (running.center) {
            try {
                element.StopAnimation(running.center);
            } catch (...) {
            }
            running.center = nullptr;
        }
        running.stepCentre = true;
    }
    if (!m_loopTimer) {
        auto queue = winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();
        if (!queue) return;
        m_loopTimer = queue.CreateTimer();
        m_loopTimer.IsRepeating(true);
        std::weak_ptr<bool> alive = m_alive;
        m_loopTimer.Tick([this, alive](auto const&, auto const&) noexcept {
            try {  // a XAML call that throws would end the process from a noexcept handler
                if (!alive.expired()) StepLoops();
            } catch (...) {
            }
        });
    }
    m_loopTimer.Interval(Span(1000.0 / kLoopFps));
    if (!m_loopTimer.IsRunning()) m_loopTimer.Start();
}

void ThemeAnimations::StepLoops() {
    const bool shown = !m_window || (IsWindowVisible(m_window) && !IsIconic(m_window));
    const bool playing = CurrentNowPlaying().playing;
    const auto now = std::chrono::steady_clock::now();
    const auto waits = [&](const th::Animation& loop) { return !playing && loop.whilePlaying; };
    // Paused, the timer ticks every kHiddenCheckMs: the stretch since the last tick was still a pause.
    const bool resumed = playing && !m_wasPlaying;
    m_wasPlaying = playing;
    bool any = false;
    bool moving = false;  // a loop moves: the `playing` ones hold still while no song plays
    for (auto& [id, running] : m_running) {
        if (running.loops.empty()) continue;
        any = true;
        const auto holds = [](const th::Animation& loop) { return loop.whilePlaying; };
        if (std::any_of(running.loops.begin(), running.loops.end(), waits) ||
            (resumed && std::any_of(running.loops.begin(), running.loops.end(), holds))) {
            running.heldMs += std::chrono::duration<double, std::milli>(now - running.lastStep).count();
        }
        running.lastStep = now;
        // Waiting loops show their held frame once, so the element does not stand untransformed until the first play.
        const bool still = std::all_of(running.loops.begin(), running.loops.end(), waits);
        if (still && running.stillShown) continue;  // its values stay as they are
        if (!still) {
            moving = true;
            running.stillShown = false;
        }
        xaml::UIElement element = shown ? Target(running) : nullptr;
        if (!element) continue;
        const double elapsedMs = std::chrono::duration<double, std::milli>(now - running.started).count();
        std::vector<th::ChannelValue> values;
        bool backgroundEnded = false;  // a finite animation without `forwards` that ended: the element's brush returns
        for (size_t i = 0; i < running.loops.size();) {
            const th::Animation& loop = running.loops[i];
            const double ms = loop.whilePlaying ? elapsedMs - running.heldMs : elapsedMs;
            const bool over = loop.iterations > 0 && ms >= loop.delayMs + loop.durationMs * loop.iterations;
            if (over && !loop.fillForwards) {
                // As in CSS, the element has its own values again: the channels go back, the brush is put back below.
                for (const th::AnimationTrack& track : loop.tracks) {
                    if (track.channel == Channel::Background) backgroundEnded = true;
                    else values.push_back(OwnChannel(running.own, track.channel));
                }
            } else {
                for (const th::ChannelValue& value : th::SampleAnimation(loop, ms, running.own)) values.push_back(value);
            }
            if (over) running.loops.erase(running.loops.begin() + static_cast<std::ptrdiff_t>(i));
            else i++;
        }
        try {
            if (running.stepCentre) {
                const float2 size = element.ActualSize();
                const float3 centre(size.x * running.origin.X, size.y * running.origin.Y, 0.f);
                if (centre.x != running.steppedCentre.x || centre.y != running.steppedCentre.y) {
                    element.CenterPoint(centre);
                    running.steppedCentre = centre;
                }
            }
            Apply(element, values);
        } catch (...) {
        }
        running.stillShown = still;
        if (backgroundEnded) {
            // Apart from Apply: an element that refuses another track must still get its brush back, and the engine its
            // background again, since the loop that ended is gone and nothing would come back to it.
            try {
                SetBackgroundBrush(element, running.ownBackground);
            } catch (...) {
            }
            running.backgroundAnimated = false;
            running.wantedBackground.reset();
        }
    }
    if (!any) {
        m_loopTimer.Stop();
        return;
    }
    m_loopTimer.Interval(Span(shown && moving ? 1000.0 / kLoopFps : kHiddenCheckMs));
}

bool ThemeAnimations::OwnsBackground(theme::NodeId id) const {
    auto known = m_running.find(id);
    return known != m_running.end() && known->second.backgroundAnimated;
}

void ThemeAnimations::WantBackground(theme::NodeId id, const theme::Value& value) {
    if (auto known = m_running.find(id); known != m_running.end() && known->second.backgroundAnimated) {
        known->second.wantedBackground = value;
    }
}

std::optional<theme::Value> ThemeAnimations::WantedBackground(theme::NodeId id) const {
    auto known = m_running.find(id);
    if (known == m_running.end() || !known->second.backgroundAnimated) return std::nullopt;
    return known->second.wantedBackground;
}

std::string ThemeAnimations::Read(theme::NodeId id) const {
    auto known = m_running.find(id);
    return known == m_running.end() ? std::string() : known->second.text;
}

void ThemeAnimations::Sweep() {
    for (auto it = m_running.begin(); it != m_running.end();) {
        if (Target(it->second)) {
            ++it;
            continue;
        }
        for (const auto& timer : it->second.ends) timer.Stop();
        it = m_running.erase(it);
    }
}

void ThemeAnimations::Forget(theme::NodeId id) {
    auto known = m_running.find(id);
    if (known == m_running.end()) return;
    Stop(known->second);
    m_running.erase(known);
}

xaml::UIElement ThemeAnimations::Target(const Running& running) {
    return running.held ? running.held : running.element.get();
}

void ThemeAnimations::Stop(Running& running) {
    for (const auto& timer : running.ends) timer.Stop();
    xaml::UIElement element = Target(running);
    if (!element) return;
    auto attempt = [](auto&& action) {
        try {
            action();
        } catch (...) {
        }
    };
    for (const composition::CompositionAnimation& animation : running.animations) {
        attempt([&] { element.StopAnimation(animation); });
    }
    if (!running.own.empty()) {
        std::vector<th::ChannelValue> own;
        for (const th::ChannelValue& value : running.own) {
            if (value.channel != Channel::Background) own.push_back(value);  // the brush itself comes back below
        }
        attempt([&] { Apply(element, own); });
    }
    if (running.backgroundAnimated) attempt([&] { SetBackgroundBrush(element, running.ownBackground); });
    if (running.center) attempt([&] { element.StopAnimation(running.center); });
    if (running.centered) attempt([&] { element.CenterPoint(running.centerBefore); });
}

// CSS scales and turns around the origin, which is the centre unless the theme chose another: an origin left at 0,0
// means it did not. The centre follows the element's size through an expression, or is taken once without it.
void ThemeAnimations::Center(Running& running, const composition::Compositor& compositor,
                             const xaml::UIElement& element, winrt::Windows::Foundation::Point origin) {
    if (origin.X == 0 && origin.Y == 0) origin = {0.5f, 0.5f};
    running.centerBefore = element.CenterPoint();
    running.centered = true;
    try {
        composition::ExpressionAnimation center = compositor.CreateExpressionAnimation(
            L"Vector3(this.Target.ActualSize.X * originX, this.Target.ActualSize.Y * originY, 0)");
        center.SetScalarParameter(L"originX", origin.X);
        center.SetScalarParameter(L"originY", origin.Y);
        center.Target(L"CenterPoint");
        element.StartAnimation(center);
        running.center = center;
    } catch (...) {
        float2 size = element.ActualSize();
        element.CenterPoint(float3(size.x * origin.X, size.y * origin.Y, 0));
    }
}

void ThemeAnimations::EndAt(Running& running, double ms, std::vector<composition::CompositionAnimation> parts) {
    auto queue = winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();
    if (!queue) return;
    auto timer = queue.CreateTimer();
    timer.Interval(Span(ms));
    timer.IsRepeating(false);
    timer.Tick([element = running.element, held = running.held, parts = std::move(parts)](auto const&, auto const&) noexcept {
        try {
            if (xaml::UIElement target = held ? held : element.get()) {
                for (const composition::CompositionAnimation& part : parts) target.StopAnimation(part);
            }
        } catch (...) {
        }
    });
    timer.Start();
    running.ends.push_back(timer);
}

}  // namespace ampify::win

#endif  // AMPIFY_WITH_XAML
