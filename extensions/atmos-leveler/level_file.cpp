#include "level_file.hpp"

#include <charconv>
#include <cmath>
#include <optional>

namespace atmos {

namespace {

constexpr std::string_view kHeader = "# Ampify, atmos-leveler\n";

void AppendNumber(std::string& out, double value, int decimals) {
    char buffer[64];
    auto [end, error] = std::to_chars(buffer, buffer + sizeof(buffer), value, std::chars_format::fixed, decimals);
    if (error == std::errc()) out.append(buffer, end);
}

// The words of one line, split on spaces and tabs.
std::vector<std::string_view> Words(std::string_view line) {
    std::vector<std::string_view> words;
    size_t at = 0;
    while (at < line.size()) {
        size_t start = line.find_first_not_of(" \t\r", at);
        if (start == std::string_view::npos) break;
        size_t end = line.find_first_of(" \t\r", start);
        if (end == std::string_view::npos) end = line.size();
        words.push_back(line.substr(start, end - start));
        at = end;
    }
    return words;
}

std::optional<double> Number(std::string_view word) {
    double value = 0;
    auto [end, error] = std::from_chars(word.data(), word.data() + word.size(), value);
    if (error != std::errc() || end != word.data() + word.size() || !std::isfinite(value)) return std::nullopt;
    return value;
}

}  // namespace

std::string FormatLevels(const VariantStore& atmos, const TargetHistory& stereo) {
    std::string out(kHeader);
    for (const auto& [variant, level] : atmos.Levels()) {
        // An id the parser would split cannot be read back: it is left out rather than saved wrong.
        if (variant.empty() || variant.find_first_of(" \t\r\n") != std::string::npos) continue;
        out += "atmos " + variant + ' ';
        AppendNumber(out, level.lufs, 3);
        out += ' ';
        AppendNumber(out, level.seconds, 1);
        out += '\n';
    }
    for (double lufs : stereo.Values()) {
        out += "stereo ";
        AppendNumber(out, lufs, 3);
        out += '\n';
    }
    return out;
}

SavedLevels ParseLevels(std::string_view text) {
    SavedLevels saved;
    size_t at = 0;
    while (at < text.size()) {
        size_t end = text.find('\n', at);
        if (end == std::string_view::npos) end = text.size();
        std::vector<std::string_view> words = Words(text.substr(at, end - at));
        at = end + 1;

        if (words.size() == 4 && words[0] == "atmos") {
            std::optional<double> lufs = Number(words[2]);
            std::optional<double> seconds = Number(words[3]);
            if (lufs && seconds) saved.atmos.Put(std::string(words[1]), StoredLevel{*lufs, *seconds});
        } else if (words.size() == 2 && words[0] == "stereo") {
            if (std::optional<double> lufs = Number(words[1])) saved.stereo.push_back(*lufs);
        }
    }
    return saved;
}

}  // namespace atmos
