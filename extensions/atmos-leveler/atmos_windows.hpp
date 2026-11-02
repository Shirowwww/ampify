#pragma once

// The Atmos leveler as an extension of the kernel. Registered in AMPLibraryAgent.exe only: that is where
// the audio is decoded and rendered. Not compiled on Linux.

#include <windows.h>
#include <propidl.h>

#include <filesystem>

#include "../../src/core/extension.hpp"
#include "../../src/core/journal.hpp"
#include "../../src/core/settings.hpp"

namespace atmos::hooks {

// Reads extensions.atmos-leveler.auto_level, fallback_db and max_db, with the reference mod's defaults. The two
// gains are brought into 0..15 dB.
ampify::ExtensionInfo MakeAtmosLevelerExtension(ampify::Journal& journal, const ampify::Settings& settings);

// Puts the sample hooks in place with no gain, so the sound can be analysed (a theme's bars(n)) while the leveler
// is off. Once only, and nothing when the leveler already placed them; the leveler starting later takes them over
// with its own settings. May wait up to 60 s for Apple's audio modules: call it off the interface's paths.
bool StartSoundHooks(ampify::Journal& journal);


}  // namespace atmos::hooks
