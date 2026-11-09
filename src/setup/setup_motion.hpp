#pragma once

// The timing of the installer window's motion (src/setup/setup_window.cpp): the curves of Ampify's interface, and a
// value that eases toward a target. Plain arithmetic, so the tests run it on every system.

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ampify::setup {

// A CSS cubic-bezier(x1, y1, x2, y2) timing function: At(progress) gives the eased value, both from 0 to 1.
struct Curve {
    float x1, y1, x2, y2;

    float At(float t) const {
        if (t <= 0) return 0;
        if (t >= 1) return 1;
        // Find the curve's parameter whose x is `t` (Newton's method, then halving if it strays), and return its y.
        float u = t;
        for (int i = 0; i < 8; i++) {
            float error = Coordinate(x1, x2, u) - t;
            if (std::fabs(error) < 1e-5f) return Coordinate(y1, y2, u);
            float slope = Slope(x1, x2, u);
            if (std::fabs(slope) < 1e-6f) break;
            u = std::clamp(u - error / slope, 0.0f, 1.0f);
        }
        float low = 0;
        float high = 1;
        u = t;
        for (int i = 0; i < 40; i++) {
            float x = Coordinate(x1, x2, u);
            if (std::fabs(x - t) < 1e-5f) break;
            (x < t ? low : high) = u;
            u = (low + high) / 2;
        }
        return Coordinate(y1, y2, u);
    }

private:
    static float Coordinate(float a, float b, float u) {
        float v = 1 - u;
        return 3 * v * v * u * a + 3 * v * u * u * b + u * u * u;
    }
    static float Slope(float a, float b, float u) {
        float v = 1 - u;
        return 3 * v * v * a + 6 * v * u * (b - a) + 3 * u * u * (1 - b);
    }
};

inline constexpr Curve kEaseOut{0.23f, 1.0f, 0.32f, 1.0f};  // what appears or settles: fast start, soft landing
inline constexpr Curve kEase{0.25f, 0.1f, 0.25f, 1.0f};     // a colour that follows the pointer

// A value on its way to a target. Sent elsewhere midway, it leaves from where it is, so nothing jumps.
class Tween {
public:
    Tween() : Tween(0) {}
    explicit Tween(float value) : m_from(value), m_to(value) {}

    void To(float target, std::uint64_t now, std::uint64_t durationMs, Curve curve = kEaseOut) {
        m_from = At(now);
        m_to = target;
        m_start = now;
        m_duration = durationMs;
        m_curve = curve;
    }

    void Jump(float value) {
        m_from = m_to = value;
        m_duration = 0;
    }

    float At(std::uint64_t now) const {
        if (!Moving(now)) return m_to;
        float progress = static_cast<float>(now - m_start) / static_cast<float>(m_duration);
        return m_from + (m_to - m_from) * m_curve.At(progress);
    }

    bool Moving(std::uint64_t now) const { return m_duration != 0 && now >= m_start && now < m_start + m_duration; }

    // When the last move ends; 0 for a value that never moved.
    std::uint64_t EndsAt() const { return m_duration != 0 ? m_start + m_duration : 0; }

private:
    float m_from;
    float m_to;
    std::uint64_t m_start = 0;
    std::uint64_t m_duration = 0;
    Curve m_curve = kEaseOut;
};

}  // namespace ampify::setup
