#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "leveler.hpp"

namespace atmos {

// What the leveler remembers from one run to the next: the level of each Atmos variant it measured, and the
// recent stereo tracks the target is the median of. Kept as plain text, one value per line:
//
//   # Apple Music Mod Kit, atmos-leveler
//   atmos <stable variant id> <lufs> <seconds>
//   stereo <lufs>
//
// The stereo lines are oldest first. Numbers use a dot, whatever the locale.
struct SavedLevels {
    VariantStore atmos;
    std::vector<double> stereo;
};

std::string FormatLevels(const VariantStore& atmos, const TargetHistory& stereo);

// Lines that do not parse are skipped, so a damaged file only loses those lines. When a variant comes twice,
// the longer measurement wins, as in VariantStore.
SavedLevels ParseLevels(std::string_view text);

}  // namespace atmos
