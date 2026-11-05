#pragma once

// Apple Music's own lyrics of the song playing, as the player gives them (TTML for timed lyrics, plain text
// otherwise), and what Ampify.player.lyrics() returns. Portable: the Windows side only fetches the raw strings
// (src/win/amp_player.cpp), this file parses them. See src/core/prelude/player.cpp.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "json.hpp"

namespace ampify {

struct LyricWord {
    int64_t startMs = 0;
    int64_t endMs = 0;
    std::string text;
};

struct LyricLine {
    int64_t startMs = 0;
    int64_t endMs = 0;
    std::string text;
    std::vector<LyricWord> words;  // empty when Apple times the line only
};

struct Lyrics {
    bool synced = false;  // false: plain lines, the times are meaningless
    std::string language;
    std::vector<LyricLine> lines;
    std::string text;  // the lines joined by "\n"
};

// The strings ILyricsInfo hands out: GetTTML, GetStaticLyrics and GetLanguage. Any may be empty.
struct RawLyrics {
    std::string ttml;
    std::string staticText;
    std::string language;
};

// Larger documents are refused: a script must not receive megabytes because of a broken answer.
inline constexpr size_t kMaxLyricsBytes = 2 * 1024 * 1024;

// A TTML time such as "12.345", "1:02.5", "00:01:02.500" or "1500ms", in milliseconds. nullopt when malformed.
std::optional<int64_t> ParseTtmlTime(std::string_view text);

// Timed lines from TTML. The scanner is tolerant: unknown tags are skipped, broken markup keeps what was read.
// Empty `lines` when the document holds no lyric line.
Lyrics ParseTtml(std::string_view ttml);

// Timed lyrics when the TTML has lines, else the static text as untimed lines, else nullopt.
std::optional<Lyrics> LyricsFromRaw(const RawLyrics& raw);

// What Ampify.player.lyrics() returns: null, or { source, synced, language, lines, text }.
Json LyricsJson(const std::optional<Lyrics>& lyrics);

// Where the song is in its lyrics, for a karaoke display: the line being sung and how far each of its words is.
struct KaraokeFrame {
    int line = -1;              // the last line started, -1 before the first line or for untimed lyrics
    bool between = false;       // that line has ended and the next has not started (a pause)
    std::vector<double> words;  // per word of `line`, 0 not reached to 1 sung; a line Apple times only as a whole has
                                // its words share its span by length (KaraokePieces gives the same words)
};

KaraokeFrame KaraokeAt(const Lyrics& lyrics, int64_t positionMs);

// The words of `line` as they are written in its text, each with the space that follows it, if any: Apple times
// syllables apart ("beau" "ti" "ful"), and only the line's text tells which words are glued together.
std::vector<std::string> KaraokePieces(const LyricLine& line);

}  // namespace ampify
