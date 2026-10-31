#pragma once

// The Atmos leveler as an extension of the kernel. Registered in AMPLibraryAgent.exe only: that is where
// the audio is decoded and rendered. Not compiled on Linux.

#include <windows.h>
#include <propidl.h>

#include <filesystem>

#include "../../core/extension.hpp"
#include "../../core/journal.hpp"
#include "../../core/settings.hpp"

namespace atmos::hooks {

// Reads extensions.atmos-leveler.auto_level, fallback_db and max_db, with the reference mod's defaults. The two
// gains are brought into 0..15 dB.
ampify::ExtensionInfo MakeAtmosLevelerExtension(ampify::Journal& journal, const ampify::Settings& settings);

// For the Wine harness only, which has no spatial audio engine to activate Apple Music's stream with.
namespace testing {

// Hands a stream to the leveler as if ISpatialAudioClient::ActivateSpatialAudioStream had just returned it with
// these arguments: a metadata stream gets its hooks. The leveler must have started.
void SpatialStreamActivated(const PROPVARIANT* params, REFIID riid, void* stream);

// %USERPROFILE%\Ampify\atmos-levels.txt: the levels kept across runs.
std::filesystem::path LevelsFile();

}  // namespace testing

}  // namespace atmos::hooks
