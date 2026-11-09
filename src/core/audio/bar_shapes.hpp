#pragma once

// Where the bars of `content: bars(n, shape)` stand for each shape (src/win/audio_bars.*), from their levels (0 to 1,
// BarMeter) and the decor's size. Portable arithmetic only: the interface writes the results as composition
// transforms and points, which move nothing in the layout.

#include <cstddef>
#include <vector>

namespace ampify::audio {

// Mirror: the meter has MirrorBands(count) levels, the lowest notes first, and bar `index` shows level
// MirrorSource(index, count), so the low notes stand in the centre and the high ones at both ends.
size_t MirrorBands(size_t count);
size_t MirrorSource(size_t index, size_t count);

// A 2D transform for row vectors, as XAML's matrices: x' = x * m11 + y * m21 + dx, y' = x * m12 + y * m22 + dy.
struct Affine {
    float m11 = 1, m12 = 0, m21 = 0, m22 = 1, dx = 0, dy = 0;
};

// Circle: a ring in the middle of the decor, its bars `thickness` wide and at most `length` long, pointing outwards.
struct Ring {
    float centreX = 0, centreY = 0;
    float radius = 0;     // from the centre to the foot of a bar
    float length = 0;     // of a full bar
    float thickness = 0;  // of every bar
};
Ring LayRing(float width, float height, size_t count);
// The transform that takes a bar laid at the top left (thickness by length) to its place on the ring, `level` of its
// length, scaled from its foot. Bar 0 points up; the others follow clockwise.
Affine RingBar(const Ring& ring, size_t index, size_t count, float level);

// Line: a smooth curve through the tops of the bars, from the left edge to the right one, kept `inset` inside the
// decor so a thick stroke is not cut. `out` is overwritten (reused from frame to frame: no allocation once sized).
struct Point {
    float x = 0, y = 0;
};
void TraceLine(const std::vector<float>& levels, float width, float height, float inset, std::vector<Point>& out);

}  // namespace ampify::audio
