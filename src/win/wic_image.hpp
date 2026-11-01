#pragma once

// Image decoding with the Windows Imaging Component, behind ImageHost (src/core/image_host.hpp).

#include <cstdint>
#include <optional>
#include <string>

#include "../core/image_host.hpp"

namespace ampify::win {

// Any thread: it joins the multithreaded COM apartment for the call when the thread has none.
std::optional<RgbaImage> DecodeWithWic(const std::string& bytes, uint32_t maxSide);

}  // namespace ampify::win
