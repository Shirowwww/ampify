#include "width_glide.hpp"

#if defined(AMPIFY_WITH_XAML)

#include <algorithm>
#include <cmath>
#include <vector>

#include <winrt/Windows.Foundation.h>

namespace xaml = winrt::Microsoft::UI::Xaml;

namespace ampify::win {
namespace {

// A write that moves back, or forward by more than this share of the parent's width, is a seek or a new song: the
// width jumps there, as Apple shows it.
constexpr double kJumpShare = 0.08;

bool Usable(double width) { return std::isfinite(width) && width >= 0; }

}  // namespace

WidthGlides::~WidthGlides() {
    for (auto& [id, glide] : m_glides) Release(glide);
}

void WidthGlides::Write(theme::NodeId id, const xaml::FrameworkElement& element, double ms) noexcept {
    try {
        auto known = m_glides.find(id);
        if (ms <= 0) {
            if (known != m_glides.end()) {
                Release(known->second);
                m_glides.erase(known);
            }
            return;
        }
        if (known != m_glides.end()) {
            known->second.capMs = ms;
            return;
        }
        Glide& glide = m_glides[id];
        glide.element = winrt::make_weak(element);
        glide.capMs = ms;
        glide.token = element.RegisterPropertyChangedCallback(
            xaml::FrameworkElement::WidthProperty(),
            [this, id](const xaml::DependencyObject&, const xaml::DependencyProperty&) { OnWidth(id); });
        OnWidth(id);
    } catch (...) {
        m_glides.erase(id);
    }
}

void WidthGlides::OnWidth(theme::NodeId id) noexcept {
    if (m_writing) return;
    auto found = m_glides.find(id);
    if (found == m_glides.end()) return;
    Glide& glide = found->second;
    xaml::FrameworkElement element = glide.element.get();
    if (!element) return;
    double width = 0;
    double room = 0;
    try {
        width = element.Width();
        if (auto parent = element.Parent().try_as<xaml::FrameworkElement>()) room = parent.ActualWidth();
    } catch (...) {
        return;  // an element on its way out: Apple's width stays as it is
    }
    if (!Usable(width)) return;  // Auto: nothing to glide from or to
    Clock::time_point now = Clock::now();
    double since = std::chrono::duration<double, std::milli>(now - glide.lastWrite).count();
    glide.lastWrite = now;
    glide.target = width;

    bool jump = !glide.known || width < glide.shown || width - glide.shown > room * kJumpShare;
    glide.known = true;
    if (jump) {
        glide.moving = false;
        Show(glide, width);  // already Apple's value: nothing to write back
        return;
    }
    // Over the time this write took to come, so the line reaches it as the next one comes; Apple's own width stays
    // under it until then, Ampify's shown in its place.
    glide.from = glide.shown;
    glide.durationMs = std::clamp(since, 1.0, glide.capMs);
    glide.start = now;
    glide.moving = true;
    Show(glide, glide.from);
    Listen();
}

void WidthGlides::Listen() noexcept {
    if (m_frames) return;
    try {
        m_frames = xaml::Media::CompositionTarget::Rendering(winrt::auto_revoke,
                                                             [this](auto&&, auto&&) { Step(); });
    } catch (...) {
        // No frame events: the widths jump as before.
        for (auto& [id, glide] : m_glides) {
            if (glide.moving) Show(glide, glide.target);
            glide.moving = false;
        }
    }
}

// One frame: each glide moves on, linearly, as a progress line does; the frame events stop once none moves.
void WidthGlides::Step() noexcept {
    Clock::time_point now = Clock::now();
    bool any = false;
    for (auto& [id, glide] : m_glides) {
        if (!glide.moving) continue;
        double t = std::chrono::duration<double, std::milli>(now - glide.start).count() / glide.durationMs;
        if (t >= 1) {
            glide.moving = false;
            Show(glide, glide.target);
            continue;
        }
        any = true;
        Show(glide, glide.from + (glide.target - glide.from) * t);
    }
    if (!any) m_frames.revoke();
}

void WidthGlides::Show(Glide& glide, double width) noexcept {
    glide.shown = width;
    xaml::FrameworkElement element = glide.element.get();
    if (!element) return;
    try {
        if (element.Width() == width) return;
        m_writing = true;
        element.Width(width);
    } catch (...) {
    }
    m_writing = false;
}

// Apple's last width back in place, and the callback gone.
void WidthGlides::Release(Glide& glide) noexcept {
    xaml::FrameworkElement element = glide.element.get();
    if (!element) return;
    try {
        element.UnregisterPropertyChangedCallback(xaml::FrameworkElement::WidthProperty(), glide.token);
        if (glide.known) element.Width(glide.target);
    } catch (...) {
    }
}

void WidthGlides::Sweep() noexcept {
    std::vector<theme::NodeId> gone;
    for (const auto& [id, glide] : m_glides) {
        if (!glide.element.get()) gone.push_back(id);
    }
    for (theme::NodeId id : gone) m_glides.erase(id);
    if (m_glides.empty()) m_frames.revoke();
}

}  // namespace ampify::win

#endif
