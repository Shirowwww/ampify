// The Background channel of theme animations: a colour mixes channel by channel, a track keeps its own start and end,
// and the text that goes from the engine to the backend carries it. Reading it from user.css is in theme_tests.cpp.

#include <array>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "../src/core/theme/animation.hpp"
#include "test.hpp"

namespace {

namespace th = ampify::theme;
using Channel = th::AnimationTrack::Channel;

// A straight line: the mixes below land on whole channel values (a halfway mix of 255 would sit on a rounding edge).
constexpr std::array<double, 4> kLinear{0, 0, 1, 1};
constexpr double kAt40Percent = 400;

double Packed(std::uint32_t argb) { return static_cast<double>(argb); }

th::Animation Fade(std::vector<th::AnimationTrack::Point> points) {
    th::Animation animation;
    animation.durationMs = 1000;
    animation.easing = kLinear;
    animation.tracks = {th::AnimationTrack{Channel::Background, std::move(points)}};
    return animation;
}

double ColourAt(const th::Animation& animation, double ms, double own = 0) {
    std::vector<th::ChannelValue> values = th::SampleAnimation(animation, ms, {{Channel::Background, own, 0}});
    return values.size() == 1 ? values[0].x : -1;
}

}  // namespace

TEST_CASE(ColourAnimation_MixesEachChannelOfTheColour) {
    th::Animation fade = Fade({{0, Packed(0xFF000000), 0}, {1, Packed(0xFFFFFFFF), 0}});
    CHECK(ColourAt(fade, 0) == Packed(0xFF000000));
    CHECK(ColourAt(fade, kAt40Percent) == Packed(0xFF666666));
    CHECK(ColourAt(fade, 1000) == Packed(0xFFFFFFFF));
}

TEST_CASE(ColourAnimation_MixesTheAlphaToo) {
    th::Animation fade = Fade({{0, Packed(0x00FF0000), 0}, {1, Packed(0xFFFF0000), 0}});
    CHECK(ColourAt(fade, kAt40Percent) == Packed(0x66FF0000));
}

TEST_CASE(ColourAnimation_AFadeFromTransparentKeepsTheColourItFadesTo) {
    // As in CSS, colours mix with their alpha applied: `transparent` is 0x00FFFFFF here, and must not tint the fade white.
    th::Animation fade = Fade({{0, Packed(0x00FFFFFF), 0}, {1, Packed(0xFFFF0000), 0}});
    CHECK(ColourAt(fade, kAt40Percent) == Packed(0x66FF0000));
    CHECK(ColourAt(fade, 1000) == Packed(0xFFFF0000));
    CHECK((static_cast<std::uint32_t>(ColourAt(fade, 0)) >> 24) == 0);  // nothing shows at the start, whatever its RGB
}

TEST_CASE(ColourAnimation_AFadeToTransparentKeepsTheColourItFadesFrom) {
    th::Animation fade = Fade({{0, Packed(0xFF00FF00), 0}, {1, Packed(0x00000000), 0}});
    CHECK(ColourAt(fade, kAt40Percent) == Packed(0x9900FF00));
}

TEST_CASE(ColourAnimation_DoesNotMixThePackedNumber) {
    // Blue to green: a mix of the two numbers would land on a colour with red in it, not on a blue-green one.
    th::Animation fade = Fade({{0, Packed(0xFF0000FF), 0}, {1, Packed(0xFF00FF00), 0}});
    CHECK(ColourAt(fade, kAt40Percent) == Packed(0xFF006699));
}

TEST_CASE(ColourAnimation_AMissingStartPointIsTheElementsOwnColour) {
    th::Animation fade = Fade({{1, Packed(0xFFFFFFFF), 0}});
    CHECK(ColourAt(fade, 0, Packed(0xFF000000)) == Packed(0xFF000000));
    CHECK(ColourAt(fade, kAt40Percent, Packed(0xFF000000)) == Packed(0xFF666666));
    CHECK(ColourAt(fade, 1000, Packed(0xFF000000)) == Packed(0xFFFFFFFF));
}

TEST_CASE(ColourAnimation_TheDelayShowsTheElementsOwnColourUnlessItFillsBackwards) {
    th::Animation fade = Fade({{0, Packed(0xFF102030), 0}, {1, Packed(0xFFFFFFFF), 0}});
    fade.delayMs = 400;
    CHECK(ColourAt(fade, 100, Packed(0xFF000000)) == Packed(0xFF000000));
    fade.fillBackwards = true;
    CHECK(ColourAt(fade, 100, Packed(0xFF000000)) == Packed(0xFF102030));
}

TEST_CASE(ColourAnimation_TheTextTheEngineWritesKeepsTheColours) {
    th::Animation fade = Fade({{0, Packed(0xFF112233), 0}, {0.5, Packed(0xEE6B2800), 0}, {1, Packed(0xFF112233), 0}});
    fade.iterations = 0;
    fade.direction = th::Animation::Direction::Alternate;
    std::optional<std::vector<th::Animation>> decoded = th::DecodeAnimations(th::EncodeAnimations({fade}));
    CHECK(decoded && decoded->size() == 1);
    if (!decoded || decoded->size() != 1) return;
    CHECK((*decoded)[0] == fade);
    CHECK((*decoded)[0].tracks[0].points[1].x == Packed(0xEE6B2800));
}
