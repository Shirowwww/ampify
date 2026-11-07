#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace ampify {

// The text without the leading and trailing characters std::isspace accepts (space, tab, CR, LF, VT, FF).
std::string_view TrimSpace(std::string_view text);

// Every byte of a file, read in binary mode (no newline translation, a BOM is kept). Nothing when it cannot be opened.
std::optional<std::string> ReadWholeFile(const std::filesystem::path& file);

// How WriteFileAtomically ended. CannotWrite: the temporary file could not be written. CannotReplace: the target could
// not be replaced, for instance because another program holds it open.
enum class WriteResult { Written, CannotWrite, CannotReplace };

// Replaces `file` with `bytes` (binary, no newline translation) through a temporary file beside it, so a crash
// mid-write keeps the previous content. When the rename is refused because a reader holds the target open on Windows,
// the target is removed and the rename tried once more. The temporary file is gone after a failure. The folder must
// exist.
WriteResult WriteFileAtomically(const std::filesystem::path& file, std::string_view bytes);

// A path from UTF-8 text. A plain std::string would be read as the ANSI code page on Windows.
std::filesystem::path Utf8ToPath(std::string_view text);

// Appends `code` as UTF-8, 1 to 4 bytes. The caller has already refused surrogates and values above U+10FFFF.
void AppendUtf8(std::string& out, uint32_t code);

// The number of characters (Unicode code points) of UTF-8 text, the unit of every length limit of a manifest.
size_t CodePointCount(std::string_view text);

// The start of `text` that is at most `maxBytes` long and does not end inside a UTF-8 character, so a clipped label or
// toast never holds half a character.
std::string_view ClipUtf8(std::string_view text, size_t maxBytes);

// The shortest text that reads back as the same double: 5, 0.72 (not 0.71999999999999997), 1e+20. A whole number below
// 9e15 is written in full, never with an exponent.
std::string ShortestNumberText(double value);

// The form of a setting key, [a-z0-9][a-z0-9-]{0,31}: the manifest declares it, a theme's @setting block names it,
// the settings page writes it. Here so that the CSS parser reads it without the manifest.
bool IsSettingKey(std::string_view key);

// Orders two dotted versions part by part (1.10.0 comes after 1.9.0): -1, 0 or 1. A leading "v" is ignored, a missing
// part counts as 0, and any character that is not a digit ends a part.
int CompareDottedVersions(std::string_view left, std::string_view right);

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
