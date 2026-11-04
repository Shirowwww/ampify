#pragma once

// What Apple Music's own data says about an item of a list or a menu (a song, an album, a playlist...): the content
// descriptor behind its data context. Plain WinRT at the ABI level (src/win/winrt_abi.hpp), like the player.

#include <optional>
#include <string>

#include "winrt_abi.hpp"

namespace ampify::win {

struct AmpItemInfo {
    std::string id;           // the catalog id (AdamId); empty for an item that is not in the catalog
    std::string canonicalId;  // Apple's own identifier for the item, empty when it gives none
    std::string kind;         // song, album, artist, composer, genre, playlist, station, video, concert or container
};

// IContentDescriptorDriven.get_ContentDescriptor of `model`, as an IContentDescriptor; empty when `model` is not driven by one.
rt::Ref ContentDescriptorOf(const rt::Ref& model);

// The item `data` (a data context) stands for. Empty when it carries no content descriptor, or when `title` is not
// empty and is not the name of `data`: the page a row sits in is driven by a descriptor too (the playlist's), and must
// not pass for the row.
std::optional<AmpItemInfo> ReadItemInfo(const rt::Ref& data, const std::string& title);

}  // namespace ampify::win
