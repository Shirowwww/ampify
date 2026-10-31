#pragma once

#include <span>
#include <string_view>

namespace ampify::theme {

// The colour and brush resource keys that Apple Music's and WinUI's theme dictionaries name, for the inspector. The
// application's dictionaries are never enumerated: that realises every deferred resource, styles and templates out of
// their context, and Apple Music then crashed at the next theme change.
std::span<const std::string_view> ColourResourceKeys();

}  // namespace ampify::theme
