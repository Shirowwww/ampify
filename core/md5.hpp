#pragma once

// MD5 (RFC 1321), for AMK.crypto.md5. Last.fm signs its API calls with it (api_sig). MD5 is broken as a security
// hash: it is offered for such protocols only, never to protect anything in the kit.

#include <string>
#include <string_view>

namespace amk {

// The digest of `data` as 32 lowercase hexadecimal characters.
std::string Md5Hex(std::string_view data);

}  // namespace amk
