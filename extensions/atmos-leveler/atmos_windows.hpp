#pragma once

// The Atmos leveler as an extension of the kernel. Registered in AMPLibraryAgent.exe only: that is where
// the audio is decoded and rendered. Not compiled on Linux.

#include "../../core/extension.hpp"
#include "../../core/journal.hpp"
#include "../../core/settings.hpp"

namespace atmos::hooks {

// Reads extensions.atmos-leveler.auto_level, fallback_db and max_db, with the reference mod's defaults.
amk::ExtensionInfo MakeAtmosLevelerExtension(amk::Journal& journal, const amk::Settings& settings);

}  // namespace atmos::hooks
