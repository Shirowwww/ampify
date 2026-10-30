#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace atmos {

// What a track is, from the format descriptions CoreFoundation gives for its audio tracks.
enum class TrackKind {
    Unknown,  // not loaded yet, or no audio description
    Stereo,   // any other audio: AAC ('qaac') or ALAC ('qlac')
    Atmos,    // E-AC-3 ('qec3' or 'ec-3') with its 16-channel layer ('qc+3' or 'ec+3')
};

// Classifies one description, such as "mediaType:'soun' mediaSubType:'qec3' ...".
TrackKind ClassifyFormatDescription(std::string_view description);

// Classifies a whole item: Atmos when any of its descriptions is Atmos, Stereo when any is audio.
TrackKind ClassifyDescriptions(const std::vector<std::string>& descriptions);

// The stable variant id inside the description of AVCFPlayerItemCopyCurrentStableVariantID: the text
// between `contents = "` and the next quote. Empty when the description does not have that shape.
std::string VariantIdFromDescription(std::string_view description);

}  // namespace atmos
