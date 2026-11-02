#include "ini.hpp"

#include <cctype>

#include "../text_util.hpp"

namespace ampify::theme {

namespace {

bool EqualsIgnoreCase(std::string_view left, std::string_view right) {
    if (left.size() != right.size()) return false;
    for (size_t i = 0; i < left.size(); i++) {
        if (std::tolower(static_cast<unsigned char>(left[i])) != std::tolower(static_cast<unsigned char>(right[i]))) {
            return false;
        }
    }
    return true;
}

}  // namespace

std::vector<std::string> Ini::Parse(std::string_view text) {
    m_sections.clear();
    std::vector<std::string> problems;
    text = WithoutBom(text);

    // Keys before any [section] header go in an unnamed section, so a stray key is reported, not lost.
    Section* current = nullptr;
    int lineNumber = 0;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string_view::npos) end = text.size();
        std::string_view raw = text.substr(start, end - start);
        start = end + 1;
        lineNumber++;

        std::string line(TrimSpace(raw));
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;

        if (line.front() == '[') {
            if (line.back() != ']' || line.size() < 3) {
                problems.push_back("line " + std::to_string(lineNumber) + ": section header not closed");
                current = nullptr;
                continue;
            }
            m_sections.push_back({std::string(TrimSpace(std::string_view(line).substr(1, line.size() - 2))), {}, lineNumber});
            current = &m_sections.back();
            continue;
        }

        size_t equals = line.find('=');
        if (equals == std::string::npos) {
            problems.push_back("line " + std::to_string(lineNumber) + ": line without \"=\" ignored");
            continue;
        }

        std::string key(TrimSpace(std::string_view(line).substr(0, equals)));
        std::string value(TrimSpace(std::string_view(line).substr(equals + 1)));
        if (key.empty()) {
            problems.push_back("line " + std::to_string(lineNumber) + ": empty key ignored");
            continue;
        }

        if (!current) {
            m_sections.push_back({std::string(), {}, lineNumber});
            current = &m_sections.back();
        }
        current->entries.push_back({key, value, lineNumber});
    }
    return problems;
}

const Ini::Section* Ini::FindSection(std::string_view name) const {
    for (const Section& section : m_sections) {
        if (EqualsIgnoreCase(section.name, name)) return &section;
    }
    return nullptr;
}

std::optional<std::string> Ini::Get(std::string_view section, std::string_view key) const {
    const Section* found = FindSection(section);
    if (!found) return std::nullopt;

    std::optional<std::string> value;
    for (const Entry& entry : found->entries) {
        if (EqualsIgnoreCase(entry.key, key)) value = entry.value;  // the last one wins
    }
    return value;
}

}  // namespace ampify::theme
