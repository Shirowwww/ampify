#include "bar_shapes.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace ampify::audio {

namespace {

constexpr float kRingFoot = 0.45f;      // of the half side: the hollow middle, where the bars stand
constexpr float kRingFill = 0.5f;       // of the room each bar has on the ring
constexpr float kThinnest = 1.5f;       // pixels
constexpr float kThickest = 6.0f;
constexpr size_t kCurveSteps = 4;       // points between two bars: enough for a smooth curve at 32 bars

float CatmullRom(float p0, float p1, float p2, float p3, float t) {
    float t2 = t * t;
    return 0.5f * (2 * p1 + (p2 - p0) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t2 + (3 * p1 - p0 - 3 * p2 + p3) * t2 * t);
}

}  // namespace

size_t MirrorBands(size_t count) { return (count + 1) / 2; }

size_t MirrorSource(size_t index, size_t count) {
    if (count == 0) return 0;
    long long distance = 2 * static_cast<long long>(index) - static_cast<long long>(count - 1);
    return static_cast<size_t>((distance < 0 ? -distance : distance) / 2);
}

Ring LayRing(float width, float height, size_t count) {
    Ring ring;
    float half = std::max(0.0f, std::min(width, height) / 2);
    ring.centreX = std::max(0.0f, width) / 2;
    ring.centreY = std::max(0.0f, height) / 2;
    ring.radius = half * kRingFoot;
    ring.length = half - ring.radius;
    float room = count ? 2 * std::numbers::pi_v<float> * ring.radius / static_cast<float>(count) : 0.0f;
    ring.thickness = std::clamp(room * kRingFill, kThinnest, kThickest);
    return ring;
}

// Scaled from the foot (thickness / 2, length), moved so the foot sits on the ring above the centre, then turned
// around the centre: the three folded into one matrix.
Affine RingBar(const Ring& ring, size_t index, size_t count, float level) {
    float angle = count ? 2 * std::numbers::pi_v<float> * static_cast<float>(index) / static_cast<float>(count) : 0.0f;
    float c = std::cos(angle);
    float s = std::sin(angle);
    float reach = ring.length * level + ring.radius;  // from the centre to the bar's tip
    float half = ring.thickness / 2;
    Affine m;
    m.m11 = c;
    m.m12 = s;
    m.m21 = -level * s;
    m.m22 = level * c;
    m.dx = ring.centreX - half * c + reach * s;
    m.dy = ring.centreY - half * s - reach * c;
    return m;
}

void TraceLine(const std::vector<float>& levels, float width, float height, float inset, std::vector<Point>& out) {
    out.clear();
    if (levels.empty() || width <= 0 || height <= 0) return;
    const size_t bars = levels.size();
    const float top = inset;
    const float bottom = std::max(top, height - inset);
    const auto at = [&](size_t anchor) {
        // Anchors: the left edge, the middle of each bar, the right edge.
        if (anchor == 0) return Point{0.0f, levels.front()};
        if (anchor > bars) return Point{width, levels.back()};
        return Point{width * (static_cast<float>(anchor) - 0.5f) / static_cast<float>(bars), levels[anchor - 1]};
    };
    const size_t anchors = bars + 2;
    const auto toY = [&](float level) { return std::clamp(bottom - (bottom - top) * level, top, bottom); };
    out.reserve((anchors - 1) * kCurveSteps + 1);
    for (size_t j = 0; j + 1 < anchors; j++) {
        Point p0 = at(j == 0 ? 0 : j - 1), p1 = at(j), p2 = at(j + 1), p3 = at(std::min(j + 2, anchors - 1));
        for (size_t k = 0; k < kCurveSteps; k++) {
            float t = static_cast<float>(k) / kCurveSteps;
            out.push_back({p1.x + (p2.x - p1.x) * t, toY(CatmullRom(p0.y, p1.y, p2.y, p3.y, t))});
        }
    }
    Point last = at(anchors - 1);
    out.push_back({last.x, toY(last.y)});
}

}  // namespace ampify::audio
