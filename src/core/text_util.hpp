#pragma once

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace ampify {

// The text without the leading and trailing characters std::isspace accepts (space, tab, CR, LF, VT, FF).
std::string_view TrimSpace(std::string_view text);

// Every byte of a file, read in binary mode (no newline translation, a BOM is kept). Nothing when it cannot be opened.
std::optional<std::string> ReadWholeFile(const std::filesystem::path& file);

// ASCII lowercase copy. Enough for file names and package names, which are ASCII.
inline std::string ToLowerAscii(std::string_view text) {
    std::string result(text);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

// Text without the UTF-8 byte order mark that Windows editors (Notepad, PowerShell 5) put at the start of a file.
inline std::string_view WithoutBom(std::string_view text) {
    return text.starts_with("\xEF\xBB\xBF") ? text.substr(3) : text;
}

// The last part of a Windows or POSIX path: "C:\Apps\AppleMusic.exe" gives "AppleMusic.exe".
inline std::string_view FileName(std::string_view path) {
    size_t cut = path.find_last_of("\\/");
    return cut == std::string_view::npos ? path : path.substr(cut + 1);
}

}  // namespace ampify
