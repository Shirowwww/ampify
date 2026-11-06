#pragma once

// Standard Base64 (RFC 4648): Ampify.player.artwork hands the cover's bytes to the script as Base64, Ampify.http.postForm
// takes file bytes as Base64, and the GitHub upload of `ampify publish` sends its content that way.

#include <optional>
#include <string>
#include <string_view>

namespace ampify {

std::string Base64Encode(std::string_view bytes);

// nullopt when `text` is not standard Base64 (alphabet A-Z a-z 0-9 + /, "=" padding, no spaces).
std::optional<std::string> Base64Decode(std::string_view text);

}  // namespace ampify
