#pragma once

// MD5 (RFC 1321), for Ampify.crypto.md5. Last.fm signs its API calls with it (api_sig). MD5 is broken as a security
// hash: it is offered for such protocols only, never to protect anything in the kit. It also names tracks ("t-" ids,
// TrackIdOf in src/core/player_model.hpp), which elements store: changing the hash changes every stored id.

#include <string>
#include <string_view>

namespace ampify {

// The digest of `data` as 32 lowercase hexadecimal characters.
std::string Md5Hex(std::string_view data);

}  // namespace ampify
