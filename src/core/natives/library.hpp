#pragma once

// Capability of the module library (src/core/natives/library.cpp): where Apple Music keeps the user's library file.

#include <filesystem>
#include <string_view>

namespace ampify {

struct LibraryHost {
    static constexpr std::string_view kCapabilityName = "library.file";
    virtual ~LibraryHost() = default;

    // The Library.musicdb file of the user's Apple Music library, which may not exist. JavaScript thread.
    virtual std::filesystem::path LibraryFile() = 0;
};

}  // namespace ampify
