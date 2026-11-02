#pragma once

// The optional host service behind Ampify.color.fromArtwork: an image turned into pixels. See src/core/host.hpp for how a
// capability works. The Windows host decodes with WIC (src/win/wic_image.*); without it, fromArtwork rejects with the
// reason.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ampify {

struct RgbaImage {
    std::vector<uint8_t> pixels;  // RGBA, row by row
    uint32_t width = 0;
    uint32_t height = 0;
};

struct ImageHost {
    static constexpr std::string_view kCapabilityName = "image.decode";
    virtual ~ImageHost() = default;

    // The image in `bytes` (JPEG, PNG...), scaled down to fit in maxSide x maxSide, or nullopt when it cannot be read.
    // Called from a worker thread of the element, possibly with other calls at the same time.
    virtual std::optional<RgbaImage> DecodeImage(const std::string& bytes, uint32_t maxSide) = 0;
};

// The optional host service behind Ampify.player.artwork: the cover of what plays now, as the system's media
// controls hold it (on Windows, the thumbnail of Apple Music's media session), files of one's own included.
struct ArtworkHost {
    static constexpr std::string_view kCapabilityName = "player.artwork";
    virtual ~ArtworkHost() = default;

    struct File {
        std::string contentType;  // "image/jpeg", "image/png"...
        std::string bytes;
    };

    // nullopt when nothing plays or the player gave no cover. Called from a worker thread of the element and may
    // wait a few seconds.
    virtual std::optional<File> CurrentArtwork() = 0;
};

}  // namespace ampify
