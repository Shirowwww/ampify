#pragma once

// The optional host service behind Ampify.fs.writeText (permission fs.output). The file goes to
// <kit>\output\<id>\<name> and nowhere else; the rules are in core/output_files.hpp.

#include <string>
#include <string_view>

namespace ampify {

struct OutputFilesHost {
    static constexpr std::string_view kCapabilityName = "fs.output";
    virtual ~OutputFilesHost() = default;

    // Writes through core/output_files.hpp (WriteOutputFile). False with `error` in English on failure.
    virtual bool WriteOutputText(const std::string& elementId, const std::string& name, const std::string& text,
                                 std::string* error) = 0;
};

}  // namespace ampify
