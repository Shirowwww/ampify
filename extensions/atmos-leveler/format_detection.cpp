#include "format_detection.hpp"

#include <array>

namespace atmos {

namespace {

constexpr std::string_view kAudioMarker = "mediaType:'soun'";
constexpr std::string_view kVariantMarker = "contents = \"";
constexpr std::array<std::string_view, 4> kAtmosSubtypes = {"'qec3'", "'ec-3'", "'qc+3'", "'ec+3'"};

}  // namespace

TrackKind ClassifyFormatDescription(std::string_view description) {
    if (description.find(kAudioMarker) == std::string_view::npos) return TrackKind::Unknown;

    for (std::string_view subtype : kAtmosSubtypes) {
        if (description.find(subtype) != std::string_view::npos) return TrackKind::Atmos;
    }
    return TrackKind::Stereo;
}

TrackKind ClassifyDescriptions(const std::vector<std::string>& descriptions) {
    TrackKind result = TrackKind::Unknown;
    for (const std::string& description : descriptions) {
        TrackKind kind = ClassifyFormatDescription(description);
        if (kind == TrackKind::Atmos) return TrackKind::Atmos;
        if (kind == TrackKind::Stereo) result = TrackKind::Stereo;
    }
    return result;
}

std::string VariantIdFromDescription(std::string_view description) {
    size_t open = description.find(kVariantMarker);
    if (open == std::string_view::npos) return {};

    size_t start = open + kVariantMarker.size();
    size_t close = description.find('"', start);
    if (close == std::string_view::npos) return {};
    return std::string(description.substr(start, close - start));
}

}  // namespace atmos
