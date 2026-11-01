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

}  // namespace ampify
