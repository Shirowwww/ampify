#pragma once

#include "../host.hpp"
#include "../json.hpp"

namespace amk::natives {

// The player state as AMK.player.state() returns it and as the player events carry it.
Json PlayerJson(const PlayerSnapshot& snapshot);

}  // namespace amk::natives
