#pragma once
// How much glass the themes may ask of the graphics card (theme.glass in settings.txt), for every theme and snippet:
// a laptop on battery or an older card keeps the look, without the costliest effects.
//   full      the theme as written (default)
//   balanced  no animation that never ends (a turning artwork) and no depth shadows; the blur of acrylic stays
//   eco       the same, and acrylic panes are painted as plain colours, without the blur

#include <string_view>

#include "theme.hpp"

namespace ampify::theme {

enum class GlassLevel { Full, Balanced, Eco };

inline constexpr char kGlassKey[] = "theme.glass";

// The level of a value of theme.glass, ignoring case and spaces. Full for an empty or unknown value.
GlassLevel GlassLevelOf(std::string_view value);

// Lightens the theme's values for that level (see above). Full leaves them as they are.
void ApplyGlassLevel(Theme& theme, GlassLevel level);

}  // namespace ampify::theme
