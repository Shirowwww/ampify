#pragma once

#include "../host.hpp"
#include "../json.hpp"

namespace ampify::natives {

// The player state as Ampify.player.state() returns it and as the player events carry it.
Json PlayerJson(const PlayerSnapshot& snapshot);

}  // namespace ampify::natives
