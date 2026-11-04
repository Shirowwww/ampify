#pragma once

// zlib (RFC 1950) decoding of DEFLATE (RFC 1951) data, for Apple Music's library file. Decoding only: Ampify never
// writes that file, so it needs no compressor, and the repository carries no zlib.

#include <cstddef>
#include <string>
#include <string_view>

namespace ampify {

// The bytes of the zlib stream `input` in `output`, or false with `error` set: a header that is not zlib, a damaged
// or truncated stream, a wrong Adler-32 checksum, or more than `maxOutput` bytes once decoded.
bool InflateZlib(std::string_view input, size_t maxOutput, std::string* output, std::string* error);

}  // namespace ampify
