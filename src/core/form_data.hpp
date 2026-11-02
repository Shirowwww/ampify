#pragma once

// Base64 and multipart/form-data bodies: Ampify.player.artwork hands the cover's bytes to the script as Base64, and
// Ampify.http.postForm sends files to an upload service (src/core/natives/http.cpp).

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ampify {

std::string Base64Encode(std::string_view bytes);

// nullopt when `text` is not standard Base64 (alphabet A-Z a-z 0-9 + /, "=" padding, no spaces).
std::optional<std::string> Base64Decode(std::string_view text);

// One part of a form. A part with a filename is a file; its contentType defaults to application/octet-stream.
struct FormField {
    std::string name;
    std::string value;
    std::string filename;
    std::string contentType;
};

// The body of a multipart/form-data request. Names and file names must not hold quotes or line breaks (the caller
// checks them); `boundary` must not appear in any value.
std::string MultipartBody(const std::vector<FormField>& fields, std::string_view boundary);

}  // namespace ampify
