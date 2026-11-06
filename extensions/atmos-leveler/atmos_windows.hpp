#pragma once

// The Atmos leveler as an extension of the kernel, in AMPLibraryAgent.exe only: that is where the audio is decoded
// and rendered. It is the gain policy of the audio platform (src/win/audio/audio_hooks.hpp): it follows the playing
// item, measures it from the samples the platform hands it, and decides the gain the platform applies. Not compiled
// on Linux.

#include "core/extension.hpp"
#include "core/journal.hpp"
#include "core/settings.hpp"

namespace atmos::hooks {

// Reads extensions.atmos-leveler.auto_level, fallback_db and max_db, with the reference mod's defaults. The two
// gains are brought into 0..kGainLimitDb. Installs the leveler as the platform's policy, neutral until it starts.
ampify::ExtensionInfo MakeAtmosLevelerExtension(ampify::Journal& journal, const ampify::Settings& settings);

}  // namespace atmos::hooks
