#pragma once
// How much glass the themes may ask of the graphics card (theme.glass in settings.txt), for every theme and snippet:
// a laptop on battery or an older card keeps the look, without the costliest effects.
//   full      the theme as written (default)
//   balanced  no animation that never ends (a turning artwork) and no depth shadows; the blur of acrylic stays
//   eco       the same, and acrylic panes are painted as plain colours, and an acrylic window as Mica, without the blur

#include <string_view>

#include "theme.hpp"

namespace ampify::theme {

enum class GlassLevel { Full, Balanced, Eco };

inline constexpr char kGlassKey[] = "theme.glass";

// The level of a value of theme.glass, ignoring case and spaces. Full for an empty or unknown value.
GlassLevel GlassLevelOf(std::string_view value);

// Lightens the theme's values for that level (see above). Full leaves them as they are.
void ApplyGlassLevel(Theme& theme, GlassLevel level);

// The level the themes were last worked out with (the theme worker sets it), for what is drawn outside the theme's
// values, such as the artwork's cross-fade. Full until then. Any thread.
void SetCurrentGlassLevel(GlassLevel level);
GlassLevel CurrentGlassLevel();

// How long a new song's artwork fades in over the previous one (`content: artwork`, an image node with src
// "artwork"): none in eco or with Windows' animation effects off, where it changes at once.
inline constexpr int kArtworkFadeMs = 500;
int ArtworkFadeMs(GlassLevel level, bool animationsOn);

}  // namespace ampify::theme
