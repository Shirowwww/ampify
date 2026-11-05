#pragma once

// Theme animations: the @keyframes blocks of user.css and the `animation` property that runs them (docs/themes/format.md,
// "Animations"). What the compositor animates without the interface thread is offered: opacity, and the parts of
// transform (scale, rotation, translation). A plain background colour is offered too: the backend steps it itself, a few
// times a second, as it does an endless animation. The engine writes an `animation` like any value; the backend decodes it.

#include <array>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "css.hpp"

namespace ampify::theme {

struct ValueContext;

// One property a @keyframes block animates, with its values at offsets from 0 to 1, in order. A track without a point
// at 0 or at 1 starts or ends at the element's own value there, as in CSS.
struct AnimationTrack {
    enum class Channel { Opacity, Scale, Rotation, Translation, Background };
    struct Point {
        double offset = 0;
        double x = 0;  // the value; the horizontal one for Scale and Translation (pixels), degrees for Rotation, the
                       // colour for Background as one number 0xAARRGGBB (a mix goes channel by channel, not on the number)
        double y = 0;  // the vertical one for Scale and Translation, 0 otherwise
        bool operator==(const Point&) const = default;
    };
    Channel channel = Channel::Opacity;
    std::vector<Point> points;
    bool operator==(const AnimationTrack&) const = default;
};

// The tracks of one @keyframes block, in the order of Channel, one per channel at most.
using KeyframeTracks = std::vector<AnimationTrack>;
// The @keyframes blocks of a theme by name. A later block of the same name replaces the earlier one, as in CSS.
using KeyframeSets = std::map<std::string, KeyframeTracks, std::less<>>;

// One animation of the `animation` property, as the backend runs it.
struct Animation {
    enum class Direction { Normal, Reverse, Alternate, AlternateReverse };
    double durationMs = 0;
    double delayMs = 0;
    int iterations = 1;  // 0 repeats forever
    Direction direction = Direction::Normal;
    bool fillBackwards = false;  // the first values show during the delay
    bool fillForwards = false;   // the last values stay once it ends
    // `playing`, endless animations only: it holds still while no song plays and goes on from there (a record that
    // stops turning in pause), and the backend steps nothing for it meanwhile.
    bool whilePlaying = false;
    std::array<double, 4> easing{0.25, 0.1, 0.25, 1};  // cubic-bezier(x1, y1, x2, y2) between two points: `ease`
    KeyframeTracks tracks;
    bool operator==(const Animation&) const = default;
};

// The tracks of one @keyframes block. Each stop's opacity and transform are read with `variables` and calc(), like a
// rule's values. What cannot animate goes to `problems`, with its line, and the rest of the block still counts.
KeyframeTracks ReadKeyframes(const Keyframes& block, const ValueContext& context,
                             const std::map<std::string, std::string>& variables,
                             std::vector<std::pair<int, std::string>>& problems);

// The value of `animation`: up to 4 `<name> <duration> [easing] [delay] [count] [direction] [fill]`, separated by
// commas, the parts in any order, as the text EncodeAnimations writes; empty for `none`. Nothing, with the reason in
// `problem`, for a part it cannot read or run, or a name without a @keyframes block in `sets`.
std::optional<std::string> AnimationValue(std::string_view raw, const KeyframeSets& sets, std::string* problem);

std::string EncodeAnimations(const std::vector<Animation>& animations);
// Nothing for a text that EncodeAnimations did not write.
std::optional<std::vector<Animation>> DecodeAnimations(std::string_view text);

// One channel's value: x and y as in AnimationTrack::Point.
struct ChannelValue {
    AnimationTrack::Channel channel = AnimationTrack::Channel::Opacity;
    double x = 0;
    double y = 0;
};

// The values `animation` gives `elapsedMs` after it started, one per track, for a backend that draws an endless
// animation itself a few times a second: the compositor would redraw the whole window at every frame of the screen.
// `own` holds the element's own values, for a track without a point at 0 or 1 and for the delay; a channel missing
// there counts as 0 (1 for Scale, as `own` of an element that was never scaled).
std::vector<ChannelValue> SampleAnimation(const Animation& animation, double elapsedMs,
                                          const std::vector<ChannelValue>& own);

}  // namespace ampify::theme
