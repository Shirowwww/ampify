#pragma once

// `ampify theme scrub`: a copy of an inspector.json that holds nothing about the person who recorded it, for the
// offline bench's corpus and its test fixtures (docs/development/offline-bench.md).

#include <optional>
#include <string>
#include <string_view>

#include "anonymize.hpp"

namespace ampify::cli {

struct ScrubCounts {
    int names = 0;      // element names that were not identifiers (a playlist's or a person's name...)
    int texts = 0;      // texts, labels, tooltips, descriptions
    int addresses = 0;  // image and web addresses, file paths
    int masked = 0;     // what Anonymize masked besides: profile folders, account and computer names, e-mail addresses
};

// Keeps the structure, the types, the element names that are identifiers (x:Name, such as "TransportBar"), sizes,
// boxes, flags, colours, numbers, keywords and visual states. Replaces every text and every free name by a placeholder,
// every address or path (but the app's own ms-appx resources) by "<address>" (a font keeps its family after '#'), the
// ids by the element's index, and rebuilds each selector from the scrubbed names. Nothing, with `problem` set, for a
// file that is not an inspector.json.
std::optional<std::string> ScrubDump(std::string_view json, const Identity& who, ScrubCounts& counts,
                                     std::string* problem = nullptr);

}  // namespace ampify::cli
