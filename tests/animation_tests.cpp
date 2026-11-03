// Tests of theme animations: the @keyframes blocks of user.css, the `animation` property, and the text a backend
// decodes (src/core/theme/animation.hpp).

#include <string>
#include <vector>

#include "../src/core/theme/animation.hpp"
#include "../src/core/theme/css.hpp"
#include "test.hpp"

namespace th = ampify::theme;

namespace {

using Channel = th::AnimationTrack::Channel;

th::KeyframeSets Sets() {
    th::KeyframeSets sets;
    sets["spin"] = {{Channel::Rotation, {{1, 360, 0}}}};
    sets["fade"] = {{Channel::Opacity, {{0, 0, 0}, {1, 1, 0}}}};
    sets["still"] = {};
    return sets;
}

// The animations of a value, or none when it does not convert (the problem then goes to `problem`).
std::vector<th::Animation> Animations(std::string_view raw, std::string* problem = nullptr) {
    std::string reason;
    std::optional<std::string> text = th::AnimationValue(raw, Sets(), &reason);
    if (problem) *problem = reason;
    if (!text) return {};
    std::optional<std::vector<th::Animation>> decoded = th::DecodeAnimations(*text);
    CHECK(decoded.has_value());
    return decoded.value_or(std::vector<th::Animation>{});
}

std::string Problem(std::string_view raw) {
    std::string problem;
    std::optional<std::string> text = th::AnimationValue(raw, Sets(), &problem);
    CHECK(!text.has_value());
    return problem;
}

}  // namespace

TEST_CASE(Keyframes_StylesheetReadsTheStopsAndTheirOffsets) {
    th::Stylesheet sheet = th::ParseStylesheet(
        "@keyframes spin { from { transform: rotate(0deg) } to { transform: rotate(360deg) } }\n"
        "@-webkit-keyframes pulse {\n"
        "    0%, 100% { opacity: 1 }\n"
        "    50% { opacity: .4 }\n"
        "}\n"
        "Grid { animation: spin 2s }\n");

    CHECK(sheet.problems.empty());
    CHECK(sheet.rules.size() == 1);
    CHECK(sheet.keyframes.size() == 2);
    if (sheet.keyframes.size() != 2) return;
    const th::Keyframes& spin = sheet.keyframes[0];
    CHECK(spin.name == "spin" && spin.line == 1 && spin.stops.size() == 2);
    if (spin.stops.size() == 2) {
        CHECK(spin.stops[0].offsets == std::vector<double>{0});
        CHECK(spin.stops[1].offsets == std::vector<double>{1});
        CHECK(spin.stops[1].declarations.size() == 1 && spin.stops[1].declarations[0].value == "rotate(360deg)");
    }
    const th::Keyframes& pulse = sheet.keyframes[1];
    CHECK(pulse.name == "pulse" && pulse.stops.size() == 2);
    if (pulse.stops.size() == 2) {
        CHECK(pulse.stops[0].offsets == (std::vector<double>{0, 1}));
        CHECK(pulse.stops[1].offsets == std::vector<double>{0.5});
        CHECK(pulse.stops[1].line == 4);
        CHECK(pulse.stops[1].declarations.size() == 1 && pulse.stops[1].declarations[0].property == "opacity");
    }
}

TEST_CASE(Keyframes_BadStopsAndPlacesAreReportedAndSkipped) {
    th::Stylesheet sheet = th::ParseStylesheet(
        "@keyframes a { middle { opacity: 1 } 120% { opacity: 0 } 50% { opacity: .5 } }\n"
        "@keyframes { from { opacity: 0 } }\n"
        "@media (max-width: 900px) { @keyframes b { to { opacity: 1 } } }\n"
        "Grid { width: 1 }\n");

    CHECK(sheet.keyframes.size() == 1);  // a, with its one good stop
    if (!sheet.keyframes.empty()) CHECK(sheet.keyframes[0].stops.size() == 1);
    CHECK(sheet.problems.size() == 4);  // middle, 120%, a block without a name, one inside @media
    CHECK(sheet.rules.size() == 1);
}

TEST_CASE(Animation_ReadsTheShorthandInAnyOrder) {
    std::vector<th::Animation> spin = Animations("spin 2s linear infinite");
    CHECK(spin.size() == 1);
    if (spin.size() == 1) {
        CHECK(spin[0].durationMs == 2000 && spin[0].delayMs == 0);
        CHECK(spin[0].iterations == 0);
        CHECK(spin[0].easing == (std::array<double, 4>{0, 0, 1, 1}));
        CHECK(spin[0].tracks == Sets()["spin"]);
    }
    CHECK(Animations("infinite linear 2s spin") == spin);

    std::vector<th::Animation> plain = Animations("fade 500ms");
    CHECK(plain.size() == 1);
    if (plain.size() == 1) {
        CHECK(plain[0].iterations == 1 && plain[0].direction == th::Animation::Direction::Normal);
        CHECK(!plain[0].fillBackwards && !plain[0].fillForwards);
        CHECK(plain[0].easing == (std::array<double, 4>{0.25, 0.1, 0.25, 1}));  // ease
    }

    std::vector<th::Animation> full = Animations("fade 1.5s ease-in-out .2s 3 alternate both");
    CHECK(full.size() == 1);
    if (full.size() == 1) {
        CHECK(full[0].durationMs == 1500 && full[0].delayMs == 200 && full[0].iterations == 3);
        CHECK(full[0].direction == th::Animation::Direction::Alternate);
        CHECK(full[0].fillBackwards && full[0].fillForwards);
        CHECK(full[0].easing == (std::array<double, 4>{0.42, 0, 0.58, 1}));
    }
    std::vector<th::Animation> curve = Animations("fade 1s cubic-bezier(0.2, 0.8, 0.2, 1) forwards running");
    CHECK(curve.size() == 1 && curve[0].easing == (std::array<double, 4>{0.2, 0.8, 0.2, 1}));
    CHECK(curve.size() == 1 && curve[0].fillForwards && !curve[0].fillBackwards);

    std::vector<th::Animation> both = Animations("spin 20s linear infinite, fade 300ms ease-out");
    CHECK(both.size() == 2);
    if (both.size() == 2) CHECK(both[1].tracks == Sets()["fade"] && both[1].durationMs == 300);

    std::string problem;
    std::optional<std::string> none = th::AnimationValue("none", Sets(), &problem);
    CHECK(none && none->empty());
}

TEST_CASE(Animation_RefusesWhatItCannotRunWithTheReason) {
    CHECK(Problem("spin").find("duration") != std::string::npos);
    CHECK(Problem("2s linear").find("name") != std::string::npos);
    CHECK(Problem("ghost 2s").find("ghost") != std::string::npos);
    CHECK(Problem("still 2s").find("animates nothing") != std::string::npos);
    CHECK(Problem("spin 2s steps(4)").find("steps") != std::string::npos);
    CHECK(Problem("spin 0s").find("above 0") != std::string::npos);
    CHECK(Problem("spin 200s").find("120 s") != std::string::npos);
    CHECK(Problem("spin 2s 1.5").find("whole number") != std::string::npos);
    CHECK(Problem("spin 2s paused").find("paused") != std::string::npos);
    CHECK(Problem("spin 2s -1s").find("delay") != std::string::npos);
    CHECK(Problem("spin 2s cubic-bezier(2, 0, 1, 1)").find("cubic-bezier") != std::string::npos);
    CHECK(Problem("spin 2s fade").find("two names") != std::string::npos);
    CHECK(Problem("spin 2s 1s 3s").find("third") != std::string::npos);
    CHECK(Problem("spin 1s, spin 1s, spin 1s, spin 1s, spin 1s").find("4") != std::string::npos);
}

TEST_CASE(Animation_TheEncodedTextReadsBack) {
    th::Animation first;
    first.durationMs = 1250;
    first.delayMs = 40;
    first.iterations = 0;
    first.direction = th::Animation::Direction::AlternateReverse;
    first.fillBackwards = true;
    first.easing = {0.1, 0.7, 0.3, 1};
    first.tracks = {{Channel::Opacity, {{0, 1, 0}, {0.5, 0.25, 0}}},
                    {Channel::Scale, {{0, 1, 1}, {1, 1.05, 0.95}}},
                    {Channel::Rotation, {{1, -90, 0}}},
                    {Channel::Translation, {{0.3, 4, -2.5}}}};
    th::Animation second;
    second.durationMs = 300;
    second.fillForwards = true;
    second.tracks = {{Channel::Opacity, {{1, 0, 0}}}};

    std::string text = th::EncodeAnimations({first, second});
    std::optional<std::vector<th::Animation>> decoded = th::DecodeAnimations(text);
    CHECK(decoded && *decoded == (std::vector<th::Animation>{first, second}));
    CHECK(th::EncodeAnimations({}).empty());
    CHECK(th::DecodeAnimations("") && th::DecodeAnimations("")->empty());
    CHECK(!th::DecodeAnimations("not an animation").has_value());
    CHECK(!th::DecodeAnimations("100;0;1;normal;none;0,0,1,1;wobble=0:1").has_value());
}

namespace {

// The value of the one track of `animation` at `ms`, with the element's own values at 0.
double At(const th::Animation& animation, double ms) {
    std::vector<th::ChannelValue> values = th::SampleAnimation(animation, ms, {});
    CHECK(values.size() == 1);
    return values.empty() ? -1 : values.front().x;
}

bool Near(double a, double b) { return a > b - 0.01 && a < b + 0.01; }

}  // namespace

TEST_CASE(SampleAnimation_AnEndlessTurnGoesRoundAndRoundFromTheElementsOwnAngle) {
    std::vector<th::Animation> spin = Animations("spin 24s linear infinite");
    CHECK(spin.size() == 1);
    if (spin.empty()) return;
    CHECK(Near(At(spin[0], 0), 0));
    CHECK(Near(At(spin[0], 6000), 90));
    CHECK(Near(At(spin[0], 30000), 90));
    std::vector<th::ChannelValue> tilted = th::SampleAnimation(spin[0], 0, {{Channel::Rotation, 45, 0}});
    CHECK(tilted.size() == 1 && Near(tilted[0].x, 45));
}

TEST_CASE(SampleAnimation_AlternateGoesBackOnEveryOtherRound) {
    std::vector<th::Animation> fade = Animations("fade 1s linear infinite alternate");
    CHECK(fade.size() == 1);
    if (fade.empty()) return;
    CHECK(Near(At(fade[0], 250), 0.25));
    CHECK(Near(At(fade[0], 1250), 0.75));
    std::vector<th::Animation> reverse = Animations("fade 1s linear infinite reverse");
    CHECK(!reverse.empty() && Near(At(reverse[0], 250), 0.75));
}

TEST_CASE(SampleAnimation_EasingAndDelayFollowCss) {
    std::vector<th::Animation> eased = Animations("fade 1s ease infinite");
    CHECK(!eased.empty() && Near(At(eased[0], 500), 0.8024));
    std::vector<th::Animation> late = Animations("fade 1s linear 2s infinite");
    CHECK(!late.empty());
    if (late.empty()) return;
    std::vector<th::ChannelValue> waiting = th::SampleAnimation(late[0], 1000, {{Channel::Opacity, 0.6, 0}});
    CHECK(waiting.size() == 1 && Near(waiting[0].x, 0.6));
    CHECK(Near(At(late[0], 2500), 0.5));
}
