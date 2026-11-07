#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>

#include "color.hpp"

namespace ampify::theme {

// The accent the user chooses over the theme's (theme.accent): a colour, or the artwork's.

// The colour of theme.accent: six hexadecimal digits, with or without `#`. Nothing for an empty or unfit value.
std::optional<Argb> ParseAccent(std::string_view text);

// theme.accent set to `artwork`: the accent follows the song playing (its vibrant colour, see ArtworkAccent).
inline constexpr std::string_view kArtworkAccent = "artwork";
bool IsArtworkAccent(std::string_view text);

// The accent of the artwork playing: its art-2 colour, kept within lightness limits so it reads on either page mode.
// Nothing before the first artwork, so the theme keeps its own accent until then.
std::optional<Argb> ArtworkAccent(const std::map<std::string, std::string>& artwork);
Argb LegibleAccent(Argb color);

}  // namespace ampify::theme
