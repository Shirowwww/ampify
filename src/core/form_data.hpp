#pragma once

// multipart/form-data bodies: Ampify.http.postForm sends files to an upload service (src/core/natives/http.cpp).

#include <string>
#include <string_view>
#include <vector>

namespace ampify {

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
