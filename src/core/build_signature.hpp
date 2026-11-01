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

namespace ampify {

// The player DLL that holds the playback calls the kit needs. M3 checks them; M4 hooks them.
inline constexpr std::string_view kPlayerDllName = "AVFoundationCF.dll";

// Exports of kPlayerDllName that the kit depends on. Names from knowledge/exports/AVFoundationCF.txt.
inline constexpr std::string_view kRequiredPlayerExports[] = {
    "AVCFPlayerCopyCurrentItem",
    "AVCFPlayerItemCopyCurrentStableVariantID",
    "AVCFPlayerItemCopyTracks",
    "AVCFPlayerItemTrackCopyAssetTrack",
    "AVCFPlayerSetVolume",
};

struct BuildSignature {
    bool readable = false;             // the bytes are a PE image with a readable export table
    std::vector<std::string> missing;  // required exports that the image does not have
};

// Names in the export directory of a PE image. Empty when the bytes are not a readable PE image; an image
// without exports gives an empty list.
std::optional<std::vector<std::string>> PeExportNames(std::span<const std::byte> image);

// Checks `image` against the required names. Bytes that cannot be read are never a match.
BuildSignature CheckBuildSignature(std::span<const std::byte> image,
                                   std::span<const std::string_view> required = kRequiredPlayerExports);

inline bool SignatureMatches(const BuildSignature& signature) {
    return signature.readable && signature.missing.empty();
}

// One line for the log, in English: what was found in the build on disk, or why it is not recognised.
std::string DescribeSignature(const BuildSignature& signature);

}  // namespace ampify
