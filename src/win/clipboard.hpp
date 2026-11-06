#pragma once

// The Windows clipboard, text only, behind Ampify.clipboard (src/core/natives/clipboard.hpp).

#include <optional>
#include <string>

namespace ampify::win {

// `owner` is a window of the calling thread: Windows needs one to take the clipboard after emptying it.
bool CopyTextToClipboard(void* owner, const std::string& utf8);
std::optional<std::string> ReadClipboardText();

}  // namespace ampify::win
