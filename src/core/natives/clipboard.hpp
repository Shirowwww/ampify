#pragma once

// The optional host service behind Ampify.clipboard (src/core/host.hpp says how a capability works). Text only, in
// UTF-8. A host without it makes copy answer false and paste answer null.

#include <optional>
#include <string>
#include <string_view>

namespace ampify {

struct ClipboardHost {
    static constexpr std::string_view kCapabilityName = "clipboard";
    virtual ~ClipboardHost() = default;

    // Puts the text on the clipboard, in place of what was there. False when the clipboard could not be opened.
    virtual bool CopyText(const std::string& text) = 0;
    // The text on the clipboard, or nullopt when it holds none (an image, nothing) or could not be opened.
    virtual std::optional<std::string> PasteText() = 0;
};

namespace natives {

// The most text an element copies or reads at once, in bytes of UTF-8.
constexpr size_t kMaxClipboardText = 1 << 20;

}  // namespace natives

}  // namespace ampify
