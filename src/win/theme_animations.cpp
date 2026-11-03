// Written against the C++/WinRT projection of the Windows App SDK, like xaml_backend.cpp.

#if defined(AMPIFY_WITH_XAML)

#include "theme_animations.hpp"

#include <algorithm>
#include <chrono>
#include <optional>

#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Windows.Foundation.Numerics.h>
#include <winrt/Windows.UI.ViewManagement.h>

// After the projection: its macros (GetCurrentTime, GetClassName) would rename members of WinRT types.
#include <windows.h>

#include "../core/theme/animation.hpp"

namespace ampify::win {

namespace {

namespace th = ampify::theme;
namespace composition = winrt::Microsoft::UI::Composition;
namespace xaml = winrt::Microsoft::UI::Xaml;
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
        case Channel::Opacity: break;
    }
    return L"Opacity";
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
        if (track.points.empty()) continue;
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
        case Channel::Opacity: break;
    }
    return {channel, element.Opacity(), 0};
}

void Apply(const xaml::UIElement& element, const std::vector<th::ChannelValue>& values) {
    for (const th::ChannelValue& value : values) {
        const float x = static_cast<float>(value.x);
        const float y = static_cast<float>(value.y);
        switch (value.channel) {
            case Channel::Scale: element.Scale(float3(x, y, 1.f)); break;
            case Channel::Rotation: element.Rotation(x); break;
            case Channel::Translation: element.Translation(float3(x, y, element.Translation().z)); break;
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
    running.text = text;
    if (!MotionAllowed()) {
        m_running.emplace(id, std::move(running));  // kept as written, so it is not tried again on every pass
        return true;
    }
    try {
        composition::Compositor compositor = xaml::Media::CompositionTarget::GetCompositorForCurrentThread();
        if (Turns(*animations)) Center(running, compositor, element, origin);
        std::vector<th::Animation> loops;
        for (const th::Animation& animation : *animations) {
            if (animation.iterations == 0) {
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
    xaml::UIElement element = running.element.get();
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
    if (!m_loopTimer) {
        auto queue = winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();
        if (!queue) return;
        m_loopTimer = queue.CreateTimer();
        m_loopTimer.IsRepeating(true);
        std::weak_ptr<bool> alive = m_alive;
        m_loopTimer.Tick([this, alive](auto const&, auto const&) noexcept {
            if (!alive.expired()) StepLoops();
        });
    }
    m_loopTimer.Interval(Span(1000.0 / kLoopFps));
    if (!m_loopTimer.IsRunning()) m_loopTimer.Start();
}

void ThemeAnimations::StepLoops() {
    const bool shown = !m_window || (IsWindowVisible(m_window) && !IsIconic(m_window));
    const auto now = std::chrono::steady_clock::now();
    bool any = false;
    for (auto& [id, running] : m_running) {
        if (running.loops.empty()) continue;
        any = true;
        xaml::UIElement element = shown ? running.element.get() : nullptr;
        if (!element) continue;
        const double ms = std::chrono::duration<double, std::milli>(now - running.started).count();
        std::vector<th::ChannelValue> values;
        for (const th::Animation& loop : running.loops) {
            for (const th::ChannelValue& value : th::SampleAnimation(loop, ms, running.own)) values.push_back(value);
        }
        try {
            Apply(element, values);
        } catch (...) {
        }
    }
    if (!any) {
        m_loopTimer.Stop();
        return;
    }
    m_loopTimer.Interval(Span(shown ? 1000.0 / kLoopFps : kHiddenCheckMs));
}

std::string ThemeAnimations::Read(theme::NodeId id) const {
    auto known = m_running.find(id);
    return known == m_running.end() ? std::string() : known->second.text;
}

void ThemeAnimations::Sweep() {
    for (auto it = m_running.begin(); it != m_running.end();) {
        if (it->second.element.get()) {
            ++it;
            continue;
        }
        for (const auto& timer : it->second.ends) timer.Stop();
        it = m_running.erase(it);
    }
}

void ThemeAnimations::Stop(Running& running) {
    for (const auto& timer : running.ends) timer.Stop();
    xaml::UIElement element = running.element.get();
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
    if (!running.loops.empty()) attempt([&] { Apply(element, running.own); });
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
    timer.Tick([element = running.element, parts = std::move(parts)](auto const&, auto const&) noexcept {
        try {
            if (xaml::UIElement target = element.get()) {
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
