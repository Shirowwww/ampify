#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ampify::theme {

// A small INI reader for theme.ini: `[section]` headers, `key = value` lines, and `;` or `#` comments.
// Section and key names are matched case-insensitively. A key that appears twice keeps its last value.
class Ini {
public:
    struct Entry {
        std::string key;
        std::string value;
        int line = 0;
    };

    struct Section {
        std::string name;
        std::vector<Entry> entries;
        int line = 0;
    };

    // Returns the problems found, one message each (with the line number). Empty when the text is clean.
    // Lines that cannot be read are skipped, so the rest of the file still loads.
    std::vector<std::string> Parse(std::string_view text);

    const Section* FindSection(std::string_view name) const;
    std::optional<std::string> Get(std::string_view section, std::string_view key) const;
    const std::vector<Section>& Sections() const { return m_sections; }

private:
    std::vector<Section> m_sections;
};

}  // namespace ampify::theme
