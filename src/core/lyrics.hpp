#pragma once

// Apple Music's own lyrics of the song playing, as the player gives them (TTML for timed lyrics, plain text
// otherwise), and what Ampify.player.lyrics() returns. Portable: the Windows side only fetches the raw strings
// (src/win/amp_player.cpp), this file parses them. See src/core/prelude/player.cpp.

#include <cstdint>
#include <memory>
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
    bool spaceAfter = false;  // a space followed it in the line's text: "beau" "ti" "ful " "day"
};

// How finely Apple timed a line: each word, the line as a whole, or not at all (plain text).
enum class LyricTiming { Untimed, ByLine, ByWord };

struct LyricLine {
    int64_t startMs = 0;
    int64_t endMs = 0;
    std::string text;
    std::vector<LyricWord> words;  // empty unless timing is ByWord
    LyricTiming timing = LyricTiming::Untimed;
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

// The lyrics parsed from `raw`, kept for the next call with the same text: the karaoke feed and Ampify.player.lyrics()
// share one copy per song, and a revised text is parsed again. Null when `raw` holds no lyrics. One slot for the
// process, because both callers read the song that plays: a call with another text replaces it, so another source of
// lyrics needs a cache of its own.
std::shared_ptr<const Lyrics> SharedLyrics(const RawLyrics& raw);

// What Ampify.player.lyrics() returns: null for no lyrics, else { source, synced, language, lines, text }.
Json LyricsJson(const Lyrics* lyrics);

// One word of a line as the karaoke shows it: its text with the space that follows it, if any, and when it is sung.
// Apple times syllables apart ("beau" "ti" "ful"), so a piece is not always a whole word. A line timed only as a
// whole shares its span among its words by length; an untimed or instant line is one piece.
struct KaraokeWord {
    std::string piece;
    int64_t startMs = 0;
    int64_t endMs = 0;
};

// Never empty.
std::vector<KaraokeWord> KaraokeWords(const LyricLine& line);

// Where the song is in its lyrics, for a karaoke display: the line being sung and how far each of its words is.
struct KaraokeFrame {
    int line = -1;              // the last line started, -1 before the first line or for untimed lyrics
    bool between = false;       // that line has ended and the next has not started (a pause)
    std::vector<double> words;  // per KaraokeWords(line) entry, 0 not reached to 1 sung
};

KaraokeFrame KaraokeAt(const Lyrics& lyrics, int64_t positionMs);

}  // namespace ampify
