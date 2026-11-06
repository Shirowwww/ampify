#include "text_util.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <system_error>

namespace ampify {

namespace {

// The number at the start of `text`, then `text` moves past the one separator that ends it. No digits count as 0.
uint64_t TakeVersionPart(std::string_view& text) {
    uint64_t value = 0;
    size_t digits = 0;
    while (digits < text.size() && text[digits] >= '0' && text[digits] <= '9') {
        value = value * 10 + static_cast<uint64_t>(text[digits] - '0');
        ++digits;
    }
    text.remove_prefix(std::min(digits + 1, text.size()));
    return value;
}

std::string_view WithoutVersionPrefix(std::string_view version) {
    return !version.empty() && (version.front() == 'v' || version.front() == 'V') ? version.substr(1) : version;
}

}  // namespace

std::string_view TrimSpace(std::string_view text) {
    size_t first = 0;
    size_t last = text.size();
    while (first < last && std::isspace(static_cast<unsigned char>(text[first]))) first++;
    while (last > first && std::isspace(static_cast<unsigned char>(text[last - 1]))) last--;
    return text.substr(first, last - first);
}

std::optional<std::string> ReadWholeFile(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::nullopt;
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

WriteResult WriteFileAtomically(const std::filesystem::path& file, std::string_view bytes) {
    std::filesystem::path temporary = file;
    temporary += ".tmp";
    std::error_code ec;
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (!out) {
            out.close();
            std::filesystem::remove(temporary, ec);
            return WriteResult::CannotWrite;
        }
    }
    std::filesystem::rename(temporary, file, ec);
    std::error_code ignored;
    if (ec && std::filesystem::is_regular_file(file, ignored)) {
        // A reader can hold the target open on Windows: remove it, then rename once more.
        std::filesystem::remove(file, ec);
        std::filesystem::rename(temporary, file, ec);
    }
    if (!ec) return WriteResult::Written;
    std::filesystem::remove(temporary, ec);
    return WriteResult::CannotReplace;
}

std::filesystem::path Utf8ToPath(std::string_view text) {
    std::u8string utf8;
    utf8.reserve(text.size());
    for (char c : text) utf8.push_back(static_cast<char8_t>(static_cast<unsigned char>(c)));
    return std::filesystem::path(utf8);
}

void AppendUtf8(std::string& out, uint32_t code) {
    if (code < 0x80) {
        out += static_cast<char>(code);
    } else if (code < 0x800) {
        out += static_cast<char>(0xC0 | (code >> 6));
        out += static_cast<char>(0x80 | (code & 0x3F));
    } else if (code < 0x10000) {
        out += static_cast<char>(0xE0 | (code >> 12));
        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (code >> 18));
        out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code & 0x3F));
    }
}

std::string_view ClipUtf8(std::string_view text, size_t maxBytes) {
    if (text.size() <= maxBytes) return text;
    size_t cut = maxBytes;
    while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) cut--;
    return text.substr(0, cut);
}

std::string ShortestNumberText(double value) {
    char buffer[32];
    if (std::floor(value) == value && std::fabs(value) < 9e15) {
        std::snprintf(buffer, sizeof buffer, "%lld", static_cast<long long>(value));
        return buffer;
    }
    auto [end, error] = std::to_chars(buffer, buffer + sizeof buffer, value);
    return error == std::errc() ? std::string(buffer, end) : std::to_string(value);
}

int CompareDottedVersions(std::string_view left, std::string_view right) {
    left = WithoutVersionPrefix(left);
    right = WithoutVersionPrefix(right);
    while (!left.empty() || !right.empty()) {
        uint64_t a = TakeVersionPart(left);
        uint64_t b = TakeVersionPart(right);
        if (a != b) return a < b ? -1 : 1;
    }
    return 0;
}

}  // namespace ampify
