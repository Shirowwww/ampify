#include "animation.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>

#include "../text_util.hpp"
#include "color.hpp"
#include "properties.hpp"
#include "theme.hpp"

namespace ampify::theme {

namespace {

using Channel = AnimationTrack::Channel;

constexpr size_t kMaxAnimations = 4;
constexpr double kMaxMs = 120000;
constexpr int kMaxCount = 1000;
constexpr std::array<std::string_view, 5> kChannels{"opacity", "scale", "rotation", "translation", "background"};
constexpr std::array<std::string_view, 4> kDirections{"normal", "reverse", "alternate", "alternate-reverse"};

struct NamedEasing {
    std::string_view name;
    std::array<double, 4> curve;
};
constexpr std::array<NamedEasing, 5> kEasings{{
    {"linear", {0, 0, 1, 1}},
    {"ease", {0.25, 0.1, 0.25, 1}},
    {"ease-in", {0.42, 0, 1, 1}},
    {"ease-out", {0, 0, 0.58, 1}},
    {"ease-in-out", {0.42, 0, 0.58, 1}},
}};

// The pieces of `text` between each `separator` outside brackets, trimmed. With `skipEmpty`, runs of separators make
// one break, as spaces do between words.
std::vector<std::string_view> Split(std::string_view text, char separator, bool skipEmpty = false) {
    std::vector<std::string_view> pieces;
    int depth = 0;
    size_t start = 0;
    for (size_t i = 0; i <= text.size(); i++) {
        bool end = i == text.size();
        if (!end && text[i] == '(') depth++;
        if (!end && text[i] == ')') depth--;
        bool cut = end || (depth == 0 && (separator == ' ' ? std::isspace(static_cast<unsigned char>(text[i])) != 0
                                                           : text[i] == separator));
        if (!cut) continue;
        std::string_view piece = TrimSpace(text.substr(start, i - start));
        if (!piece.empty() || !skipEmpty) pieces.push_back(piece);
        start = i + 1;
    }
    return pieces;
}

std::optional<double> Number(std::string_view text) {
    double value = 0;
    const char* last = text.data() + text.size();
    auto [end, error] = std::from_chars(text.data(), last, value);
    if (text.empty() || error != std::errc() || end != last || !std::isfinite(value)) return std::nullopt;
    return value;
}

// `2s` or `150ms`, in milliseconds. Nothing for another word.
std::optional<double> Milliseconds(std::string_view word) {
    if (word.ends_with("ms")) return Number(word.substr(0, word.size() - 2));
    if (!word.ends_with('s')) return std::nullopt;
    std::optional<double> seconds = Number(word.substr(0, word.size() - 1));
    if (!seconds) return std::nullopt;
    return *seconds * 1000;
}

// `cubic-bezier(x1, y1, x2, y2)`: the x of both points from 0 to 1, as in CSS.
std::optional<std::array<double, 4>> CubicBezier(std::string_view lowered) {
    constexpr std::string_view kStart = "cubic-bezier(";
    if (!lowered.starts_with(kStart) || !lowered.ends_with(')')) return std::nullopt;
    std::vector<std::string_view> numbers = Split(lowered.substr(kStart.size(), lowered.size() - kStart.size() - 1), ',');
    if (numbers.size() != 4) return std::nullopt;
    std::array<double, 4> curve{};
    for (size_t i = 0; i < 4; i++) {
        std::optional<double> number = Number(numbers[i]);
        if (!number || (i % 2 == 0 && (*number < 0 || *number > 1))) return std::nullopt;
        curve[i] = *number;
    }
    return curve;
}

// One item of `animation` as it is read, word by word.
struct Item {
    std::string name;
    std::vector<double> times;  // the duration, then the delay
    Animation animation;
    std::vector<std::string_view> seen;  // the kinds of parts already read, each allowed once
};

// Records that `item` has a part of this kind. False, with the reason, for a second one.
bool Once(Item& item, std::string_view kind, std::string_view text, std::string& problem) {
    if (std::find(item.seen.begin(), item.seen.end(), kind) != item.seen.end()) {
        problem = "two " + std::string(kind) + "s in \"" + std::string(text) + "\"";
        return false;
    }
    item.seen.push_back(kind);
    return true;
}

// The keywords of the shorthand: easing, count, direction, fill mode, play state. Nothing for another word.
std::optional<bool> ReadKeyword(std::string_view lowered, std::string_view text, Item& item, std::string& problem) {
    Animation& animation = item.animation;
    for (const NamedEasing& easing : kEasings) {
        if (lowered != easing.name) continue;
        animation.easing = easing.curve;
        return Once(item, "easing", text, problem);
    }
    if (lowered.starts_with("cubic-bezier(")) {
        std::optional<std::array<double, 4>> curve = CubicBezier(lowered);
        if (!curve) {
            problem = "unreadable cubic-bezier(x1, y1, x2, y2), x1 and x2 from 0 to 1: \"" + std::string(text) + "\"";
            return false;
        }
        animation.easing = *curve;
        return Once(item, "easing", text, problem);
    }
    if (lowered.starts_with("steps(") || lowered == "step-start" || lowered == "step-end") {
        problem = "steps() is not supported, an animation moves smoothly: \"" + std::string(text) + "\"";
        return false;
    }
    for (size_t i = 0; i < kDirections.size(); i++) {
        if (lowered != kDirections[i]) continue;
        animation.direction = static_cast<Animation::Direction>(i);
        return Once(item, "direction", text, problem);
    }
    if (lowered == "none" || lowered == "forwards" || lowered == "backwards" || lowered == "both") {
        animation.fillBackwards = lowered == "backwards" || lowered == "both";
        animation.fillForwards = lowered == "forwards" || lowered == "both";
        return Once(item, "fill mode", text, problem);
    }
    if (lowered == "running") return true;
    if (lowered == "playing") {
        animation.whilePlaying = true;
        return Once(item, "play state", text, problem);
    }
    if (lowered == "paused") {
        problem = "a paused animation is not supported: \"" + std::string(text) + "\"";
        return false;
    }
    return std::nullopt;
}

// Reads one word of an item into it. False, with the reason, for a word it cannot take.
bool ReadWord(std::string_view word, std::string_view text, Item& item, std::string& problem) {
    std::string lowered = ToLowerAscii(word);
    if (std::optional<double> ms = Milliseconds(lowered)) {
        if (item.times.size() == 2) {
            problem = "a third time in \"" + std::string(text) + "\": the first is the duration, the second the delay";
            return false;
        }
        item.times.push_back(*ms);
        return true;
    }
    if (lowered == "infinite" || Number(lowered)) {
        std::optional<double> count = lowered == "infinite" ? 0.0 : Number(lowered);
        if (lowered != "infinite" && (*count < 1 || *count > kMaxCount || *count != std::floor(*count))) {
            problem = "the count is a whole number from 1 to 1000, or infinite: \"" + std::string(word) + "\"";
            return false;
        }
        item.animation.iterations = static_cast<int>(*count);
        return Once(item, "count", text, problem);
    }
    if (std::optional<bool> keyword = ReadKeyword(lowered, text, item, problem)) return *keyword;
    if (!item.name.empty()) {
        problem = "two names, " + item.name + " and " + std::string(word) + ", in \"" + std::string(text) +
                  "\": one @keyframes per animation, and commas between animations";
        return false;
    }
    item.name = std::string(word);
    return true;
}

// The duration, the delay and the @keyframes block of a read item. False, with the reason, when one is missing or
// cannot run.
bool Complete(Item& item, std::string_view text, const KeyframeSets& sets, std::string& problem) {
    const std::string quoted = "\"" + std::string(text) + "\"";
    if (item.name.empty()) {
        problem = "animation needs the name of a @keyframes block: " + quoted;
        return false;
    }
    if (item.times.empty()) {
        problem = quoted + " needs a duration, such as " + item.name + " 2s";
        return false;
    }
    Animation& animation = item.animation;
    animation.durationMs = item.times[0];
    animation.delayMs = item.times.size() > 1 ? item.times[1] : 0;
    if (animation.durationMs <= 0) problem = "the duration of " + quoted + " must be above 0";
    else if (animation.durationMs > kMaxMs) problem = "durations above 120 s are not supported: " + quoted;
    else if (animation.delayMs < 0) problem = "a negative delay is not supported: " + quoted;
    else if (animation.delayMs > kMaxMs) problem = "delays above 120 s are not supported: " + quoted;
    else if (animation.whilePlaying && animation.iterations != 0) {
        problem = "playing holds an endless animation while no song plays: add infinite to " + quoted;
    }
    if (!problem.empty()) return false;

    auto block = sets.find(item.name);
    if (block == sets.end()) {
        problem = "no @keyframes named " + item.name;
        return false;
    }
    if (block->second.empty()) {
        problem = "@keyframes " + item.name + " animates nothing: it changes neither opacity, transform nor background";
        return false;
    }
    animation.tracks = block->second;
    return true;
}

bool IsNeutral(Channel channel, const AnimationTrack::Point& point) {
    if (channel == Channel::Scale) return point.x == 1 && point.y == 1;
    return point.x == 0 && point.y == 0;
}

// Per channel, the value at each offset: a later stop or declaration at the same offset wins, as in CSS.
using StopPoints = std::array<std::map<double, AnimationTrack::Point>, 5>;

// The colour as one number, 0xAARRGGBB, and back.
double PackColor(const Argb& color) {
    return static_cast<double>((static_cast<std::uint32_t>(color.a) << 24) | (static_cast<std::uint32_t>(color.r) << 16) |
                               (static_cast<std::uint32_t>(color.g) << 8) | static_cast<std::uint32_t>(color.b));
}

// A colour between two packed ones, channel by channel (a number between 0xFF000000 and 0xFFFFFFFF is not a colour
// between black and white). As in CSS the red, green and blue mix with their alpha applied: `transparent` (0x00FFFFFF)
// fading to red passes through red at half opacity, not through a pink.
double MixColor(double from, double to, double t) {
    if (t <= 0) return from;
    if (t >= 1) return to;
    const auto a = static_cast<std::uint32_t>(from);
    const auto b = static_cast<std::uint32_t>(to);
    const double alphaA = (a >> 24) & 0xFF;
    const double alphaB = (b >> 24) & 0xFF;
    const double alpha = alphaA + (alphaB - alphaA) * t;
    auto byte = [](double value) { return static_cast<std::uint32_t>(std::clamp(std::round(value), 0.0, 255.0)); };
    std::uint32_t mixed = byte(alpha) << 24;
    for (int shift = 0; shift <= 16; shift += 8) {
        const double x = static_cast<double>((a >> shift) & 0xFF) * alphaA;
        const double y = static_cast<double>((b >> shift) & 0xFF) * alphaB;
        const double applied = x + (y - x) * t;
        mixed |= byte(alpha > 0 ? applied / alpha : 0) << shift;
    }
    return static_cast<double>(mixed);
}

// Adds one declaration of a stop to `points`. False, with the reason, for one that cannot animate.
bool ReadDeclaration(const Declaration& declaration, const KeyframeStop& stop, const ValueContext& context,
                     const std::map<std::string, std::string>& variables, StopPoints& points, std::string& problem) {
    std::string property = ToLowerAscii(declaration.property);
    if (property == "background-color") property = "background";
    if (property != "opacity" && property != "transform" && property != "background") {
        problem = "\"" + declaration.property + "\" does not animate: opacity, transform and background do";
        return false;
    }
    if (declaration.important) {
        problem = "!important does not apply inside @keyframes";
        return false;
    }
    std::optional<std::string> raw = SubstituteVariables(declaration.value, variables, &problem);
    if (raw) raw = EvaluateCalc(*raw, &problem);
    if (!raw) return false;
    Conversion conversion = ConvertValue(*FindProperty(property), *raw, context);
    if (!conversion.value) {
        problem = conversion.problem;
        return false;
    }
    if (property == "background") {
        std::optional<Argb> color =
            conversion.value->kind == ValueKind::Color ? ParseColor(conversion.value->text) : std::nullopt;
        if (!color) {
            problem = "background animates plain colours only, not images or gradients";
            return false;
        }
        for (double offset : stop.offsets) {
            points[static_cast<size_t>(Channel::Background)][offset] = {offset, PackColor(*color), 0};
        }
        return true;
    }
    std::vector<double> numbers;
    for (std::string_view part : Split(conversion.value->text, ',')) numbers.push_back(Number(part).value_or(0));
    for (double offset : stop.offsets) {
        if (property == "opacity") {
            points[static_cast<size_t>(Channel::Opacity)][offset] = {offset, numbers.at(0), 0};
            continue;
        }
        points[static_cast<size_t>(Channel::Scale)][offset] = {offset, numbers.at(0), numbers.at(1)};
        points[static_cast<size_t>(Channel::Rotation)][offset] = {offset, numbers.at(2), 0};
        points[static_cast<size_t>(Channel::Translation)][offset] = {offset, numbers.at(3), numbers.at(4)};
    }
    return true;
}

std::string FillName(const Animation& animation) {
    if (animation.fillBackwards) return animation.fillForwards ? "both" : "backwards";
    return animation.fillForwards ? "forwards" : "none";
}

std::string EncodeTrack(const AnimationTrack& track) {
    std::string text(kChannels[static_cast<size_t>(track.channel)]);
    const bool pair = track.channel == Channel::Scale || track.channel == Channel::Translation;
    for (size_t i = 0; i < track.points.size(); i++) {
        const AnimationTrack::Point& point = track.points[i];
        text += (i == 0 ? "=" : "/") + FormatNumber(point.offset) + ":" + FormatNumber(point.x);
        if (pair) text += ":" + FormatNumber(point.y);
    }
    return text;
}

std::optional<AnimationTrack> DecodeTrack(std::string_view text) {
    size_t equals = text.find('=');
    if (equals == std::string_view::npos) return std::nullopt;
    auto channel = std::find(kChannels.begin(), kChannels.end(), text.substr(0, equals));
    if (channel == kChannels.end()) return std::nullopt;
    AnimationTrack track;
    track.channel = static_cast<Channel>(channel - kChannels.begin());
    const size_t parts = track.channel == Channel::Scale || track.channel == Channel::Translation ? 3 : 2;
    for (std::string_view point : Split(text.substr(equals + 1), '/')) {
        std::vector<std::string_view> numbers = Split(point, ':');
        if (numbers.size() != parts) return std::nullopt;
        std::optional<double> offset = Number(numbers[0]), x = Number(numbers[1]);
        std::optional<double> y = parts == 3 ? Number(numbers[2]) : std::optional<double>(0);
        if (!offset || !x || !y) return std::nullopt;
        track.points.push_back({*offset, *x, *y});
    }
    return track;
}

std::optional<Animation> DecodeAnimation(std::string_view text) {
    std::vector<std::string_view> fields = Split(text, ';');
    if (fields.size() != 7 && !(fields.size() == 8 && fields[7] == "playing")) return std::nullopt;
    Animation animation;
    std::optional<double> duration = Number(fields[0]), delay = Number(fields[1]), count = Number(fields[2]);
    auto direction = std::find(kDirections.begin(), kDirections.end(), fields[3]);
    if (!duration || !delay || !count || direction == kDirections.end()) return std::nullopt;
    animation.durationMs = *duration;
    animation.delayMs = *delay;
    animation.iterations = static_cast<int>(*count);
    animation.direction = static_cast<Animation::Direction>(direction - kDirections.begin());
    if (fields[4] != "none" && fields[4] != "forwards" && fields[4] != "backwards" && fields[4] != "both") {
        return std::nullopt;
    }
    animation.fillBackwards = fields[4] == "backwards" || fields[4] == "both";
    animation.fillForwards = fields[4] == "forwards" || fields[4] == "both";
    std::vector<std::string_view> curve = Split(fields[5], ',');
    if (curve.size() != 4) return std::nullopt;
    for (size_t i = 0; i < 4; i++) {
        std::optional<double> number = Number(curve[i]);
        if (!number) return std::nullopt;
        animation.easing[i] = *number;
    }
    for (std::string_view piece : Split(fields[6], ' ', true)) {
        std::optional<AnimationTrack> track = DecodeTrack(piece);
        if (!track) return std::nullopt;
        animation.tracks.push_back(std::move(*track));
    }
    animation.whilePlaying = fields.size() == 8;
    return animation;
}

}  // namespace

KeyframeTracks ReadKeyframes(const Keyframes& block, const ValueContext& context,
                             const std::map<std::string, std::string>& variables,
                             std::vector<std::pair<int, std::string>>& problems) {
    StopPoints points;
    for (const KeyframeStop& stop : block.stops) {
        for (const Declaration& declaration : stop.declarations) {
            std::string problem;
            if (!ReadDeclaration(declaration, stop, context, variables, points, problem)) {
                problems.emplace_back(declaration.line, "@keyframes " + block.name + ": " + problem);
            }
        }
    }
    KeyframeTracks tracks;
    for (size_t channel = 0; channel < points.size(); channel++) {
        if (points[channel].empty()) continue;
        AnimationTrack track{static_cast<Channel>(channel), {}};
        for (const auto& [offset, point] : points[channel]) track.points.push_back(point);
        // The parts of a transform that stay at none in every stop are left to the element.
        bool neutral = std::all_of(track.points.begin(), track.points.end(),
                                   [&](const AnimationTrack::Point& point) { return IsNeutral(track.channel, point); });
        if (track.channel != Channel::Opacity && track.channel != Channel::Background && neutral) continue;
        tracks.push_back(std::move(track));
    }
    return tracks;
}

std::optional<std::string> AnimationValue(std::string_view raw, const KeyframeSets& sets, std::string* problem) {
    std::string reason;
    std::string_view text = TrimSpace(raw);
    if (ToLowerAscii(text) == "none") return std::string();
    std::vector<std::string_view> items = Split(text, ',');
    std::vector<Animation> animations;
    if (items.size() > kMaxAnimations) reason = "animation runs 4 animations at most: \"" + std::string(text) + "\"";
    for (size_t i = 0; reason.empty() && i < items.size(); i++) {
        Item item;
        for (std::string_view word : Split(items[i], ' ', true)) {
            if (!ReadWord(word, items[i], item, reason)) break;
        }
        if (reason.empty() && Complete(item, items[i], sets, reason)) animations.push_back(std::move(item.animation));
    }
    if (!reason.empty()) {
        if (problem) *problem = reason;
        return std::nullopt;
    }
    return EncodeAnimations(animations);
}

std::string EncodeAnimations(const std::vector<Animation>& animations) {
    std::string text;
    for (const Animation& animation : animations) {
        if (!text.empty()) text += '|';
        text += FormatNumber(animation.durationMs) + ";" + FormatNumber(animation.delayMs) + ";" +
                std::to_string(animation.iterations) + ";" + std::string(kDirections[static_cast<size_t>(animation.direction)]) +
                ";" + FillName(animation) + ";";
        for (size_t i = 0; i < animation.easing.size(); i++) text += (i ? "," : "") + FormatNumber(animation.easing[i]);
        text += ';';
        for (size_t i = 0; i < animation.tracks.size(); i++) text += (i ? " " : "") + EncodeTrack(animation.tracks[i]);
        if (animation.whilePlaying) text += ";playing";
    }
    return text;
}

std::optional<std::vector<Animation>> DecodeAnimations(std::string_view text) {
    std::vector<Animation> animations;
    if (text.empty()) return animations;
    for (std::string_view piece : Split(text, '|')) {
        std::optional<Animation> animation = DecodeAnimation(piece);
        if (!animation) return std::nullopt;
        animations.push_back(std::move(*animation));
    }
    return animations;
}

namespace {

// y of cubic-bezier(x1, y1, x2, y2) at x, as CSS eases: t is found from x by halving, close enough for a screen.
double Ease(const std::array<double, 4>& curve, double x) {
    auto at = [](double a, double b, double t) { return 3 * a * t * (1 - t) * (1 - t) + 3 * b * t * t * (1 - t) + t * t * t; };
    double low = 0, high = 1, t = x;
    for (int i = 0; i < 40; i++) {
        t = (low + high) / 2;
        (at(curve[0], curve[2], t) < x ? low : high) = t;
    }
    return at(curve[1], curve[3], t);
}

ChannelValue OwnValue(AnimationTrack::Channel channel, const std::vector<ChannelValue>& own) {
    for (const ChannelValue& value : own) {
        if (value.channel == channel) return value;
    }
    const double neutral = channel == AnimationTrack::Channel::Scale ? 1 : 0;
    return {channel, neutral, neutral};
}

// The track at `progress` (0 to 1) of one round, eased between the two points around it.
ChannelValue TrackAt(const AnimationTrack& track, double progress, const std::array<double, 4>& easing,
                     const std::vector<ChannelValue>& own) {
    ChannelValue start = OwnValue(track.channel, own);
    std::vector<AnimationTrack::Point> points;
    if (track.points.front().offset > 0) points.push_back({0, start.x, start.y});
    points.insert(points.end(), track.points.begin(), track.points.end());
    if (track.points.back().offset < 1) points.push_back({1, start.x, start.y});
    size_t next = 1;
    while (next + 1 < points.size() && points[next].offset < progress) next++;
    const AnimationTrack::Point& a = points[next - 1];
    const AnimationTrack::Point& b = points[next];
    const double span = b.offset - a.offset;
    const double eased = span > 0 ? Ease(easing, std::clamp((progress - a.offset) / span, 0.0, 1.0)) : 1;
    if (track.channel == Channel::Background) return {track.channel, MixColor(a.x, b.x, eased), 0};
    return {track.channel, a.x + (b.x - a.x) * eased, a.y + (b.y - a.y) * eased};
}

}  // namespace

std::vector<ChannelValue> SampleAnimation(const Animation& animation, double elapsedMs,
                                          const std::vector<ChannelValue>& own) {
    std::vector<ChannelValue> values;
    const double running = elapsedMs - animation.delayMs;
    const bool waiting = running < 0;
    double rounds = animation.durationMs > 0 && !waiting ? running / animation.durationMs : 0;
    if (animation.iterations > 0) rounds = std::min(rounds, static_cast<double>(animation.iterations));
    const double round = std::floor(rounds);
    double progress = rounds - round;
    if (animation.iterations > 0 && rounds >= animation.iterations) progress = 1;
    const bool odd = static_cast<long long>(round) % 2 == 1;
    using Direction = Animation::Direction;
    const bool backwards = animation.direction == Direction::Reverse ||
                           (animation.direction == Direction::Alternate && odd) ||
                           (animation.direction == Direction::AlternateReverse && !odd);
    if (backwards) progress = 1 - progress;
    for (const AnimationTrack& track : animation.tracks) {
        if (track.points.empty()) continue;
        if (waiting && !animation.fillBackwards) {
            values.push_back(OwnValue(track.channel, own));
        } else {
            values.push_back(TrackAt(track, progress, animation.easing, own));
        }
    }
    return values;
}

}  // namespace ampify::theme
