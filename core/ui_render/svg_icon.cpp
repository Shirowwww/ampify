#include "svg_icon.hpp"

#include <cstdlib>
#include <sstream>

namespace amk::uirender {

namespace {

constexpr std::size_t kMaxPathData = 4000;

bool PathCharacter(char c) {
    if (c >= '0' && c <= '9') return true;
    switch (c) {
        case 'M': case 'm': case 'L': case 'l': case 'H': case 'h': case 'V': case 'v': case 'C': case 'c':
        case 'S': case 's': case 'Q': case 'q': case 'T': case 't': case 'A': case 'a': case 'Z': case 'z':
        case 'e': case 'E': case ' ': case ',': case '.': case '-': case '+': case '\n': case '\t': case '\r':
            return true;
        default:
            return false;
    }
}

// The value of `name="..."` (or single quotes) found at or after `from` inside the tag that ends at `end`.
bool AttributeValue(std::string_view tag, std::string_view name, std::string& value) {
    std::size_t at = 0;
    while ((at = tag.find(name, at)) != std::string_view::npos) {
        bool startOk = at == 0 || tag[at - 1] == ' ' || tag[at - 1] == '\t' || tag[at - 1] == '\n';
        std::size_t equal = at + name.size();
        while (equal < tag.size() && tag[equal] == ' ') equal++;
        if (startOk && equal < tag.size() && tag[equal] == '=') {
            std::size_t quote = equal + 1;
            while (quote < tag.size() && tag[quote] == ' ') quote++;
            if (quote < tag.size() && (tag[quote] == '"' || tag[quote] == '\'')) {
                std::size_t close = tag.find(tag[quote], quote + 1);
                if (close == std::string_view::npos) return false;
                value = std::string(tag.substr(quote + 1, close - quote - 1));
                return true;
            }
        }
        at += name.size();
    }
    return false;
}

}  // namespace

SvgIcon ParseSvgIcon(std::string_view svg) {
    SvgIcon icon;
    std::size_t open = svg.find("<svg");
    if (open == std::string_view::npos) return icon;
    std::size_t openEnd = svg.find('>', open);
    if (openEnd == std::string_view::npos) return icon;
    std::string viewBox;
    if (AttributeValue(svg.substr(open, openEnd - open), "viewBox", viewBox)) {
        for (char& c : viewBox) {
            if (c == ',') c = ' ';
        }
        std::istringstream in(viewBox);
        double x = 0, y = 0, w = 0, h = 0;
        if (in >> x >> y >> w >> h && w > 0 && h > 0 && w < 10000 && h < 10000) {
            icon.width = w;
            icon.height = h;
        }
    }

    std::size_t at = openEnd;
    while ((at = svg.find("<path", at)) != std::string_view::npos) {
        std::size_t end = svg.find('>', at);
        if (end == std::string_view::npos) break;
        std::string data;
        if (AttributeValue(svg.substr(at + 5, end - at - 5), "d", data) && !data.empty() && data.size() <= kMaxPathData) {
            bool safe = true;
            for (char c : data) safe = safe && PathCharacter(c);
            if (safe) icon.paths.push_back(data);
        }
        at = end;
    }
    return icon;
}

bool IsGlyphIcon(std::string_view icon) {
    if (icon.empty() || icon.find('<') != std::string_view::npos) return false;
    // A private-use code point of Segoe Fluent Icons takes 3 bytes in UTF-8; two of them at most.
    return icon.size() <= 8;
}

}  // namespace amk::uirender
