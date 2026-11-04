#pragma once

// A short video turned into a GIF that loops, behind GifHost (src/core/image_host.hpp): Media Foundation reads the
// video and WIC writes the GIF, a frame at a time.

#include <optional>
#include <string>

#include "../core/image_host.hpp"

namespace ampify::win {

// Any thread: it joins the multithreaded COM apartment for the call when the thread has none. Media Foundation is
// loaded at the first call, so that a Windows edition without it ("N") only loses the GIFs, with the reason.
std::optional<std::string> GifFromVideoWithMf(const std::string& video, const GifHost::Options& options,
                                              std::string* error);

}  // namespace ampify::win
