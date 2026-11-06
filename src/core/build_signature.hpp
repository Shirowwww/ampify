#pragma once

// Checks that the Apple Music build on disk exports the functions the kit depends on. The check reads the export
// table of the DLL file, and the kit only ever looks functions up by name: no address is stored, so a rebuilt DLL
// with the same API keeps working, and a build that drops one of these names is refused.

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "apple_facts.hpp"

namespace ampify {

struct BuildSignature {
    bool readable = false;             // the bytes are a PE image with a readable export table
    std::vector<std::string> missing;  // required exports that the image does not have
};

// Names in the export directory of a PE image. Empty when the bytes are not a readable PE image; an image
// without exports gives an empty list.
std::optional<std::vector<std::string>> PeExportNames(std::span<const std::byte> image);

// Checks `image` against the required names: by default the exports of kPlayerDllName that the audio hooks and the
// Atmos leveler resolve (src/win/audio/, extensions/atmos-leveler/atmos_windows.cpp). Bytes that cannot be read are
// never a match.
BuildSignature CheckBuildSignature(std::span<const std::byte> image,
                                   std::span<const std::string_view> required = kRequiredPlayerExports);

inline bool SignatureMatches(const BuildSignature& signature) {
    return signature.readable && signature.missing.empty();
}

// One line for the log, in English: what was found in the build on disk, or why it is not recognised.
std::string DescribeSignature(const BuildSignature& signature);

}  // namespace ampify
