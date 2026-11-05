#include "lyrics.hpp"

#include <algorithm>
#include <cctype>

#include "text_util.hpp"

namespace ampify {

namespace {

constexpr size_t kMaxOpenElements = 64;
constexpr size_t kMaxEntityLength = 12;
constexpr size_t kMaxTimeDigits = 9;

bool IsSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }

std::string Trimmed(std::string_view text) {
    size_t begin = 0, end = text.size();
    while (begin < end && IsSpace(text[begin])) ++begin;
    while (end > begin && IsSpace(text[end - 1])) --end;
    return std::string(text.substr(begin, end - begin));
}

void AppendUtf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

// The text of one entity, without & and ;. False when it is not one this scanner knows.
bool DecodeEntity(std::string_view name, std::string& out) {
    if (name == "amp") out += '&';
    else if (name == "lt") out += '<';
    else if (name == "gt") out += '>';
    else if (name == "quot") out += '"';
    else if (name == "apos") out += '\'';
    else if (name.size() >= 2 && name[0] == '#') {
        const bool hex = name[1] == 'x' || name[1] == 'X';
        std::string_view digits = name.substr(hex ? 2 : 1);
        if (digits.empty() || digits.size() > 8) return false;
        uint32_t cp = 0;
        for (char c : digits) {
            int value = std::isdigit(static_cast<unsigned char>(c)) ? c - '0'
                        : hex && std::isxdigit(static_cast<unsigned char>(c)) ? std::tolower(c) - 'a' + 10
                                                                              : -1;
            if (value < 0) return false;
            cp = cp * (hex ? 16 : 10) + static_cast<uint32_t>(value);
        }
        if (cp == 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
        AppendUtf8(out, cp);
    } else {
        return false;
    }
    return true;
}

std::string DecodeEntities(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '&') {
            size_t semicolon = text.find(';', i + 1);
            if (semicolon != std::string_view::npos && semicolon - i - 1 <= kMaxEntityLength &&
                DecodeEntity(text.substr(i + 1, semicolon - i - 1), out)) {
                i = semicolon;
                continue;
            }
        }
        out += text[i];
    }
    return out;
}

// ---- Times --------------------------------------------------------------------------------------------------------

// "12" or "12.345": whole units and thousandths. No sign, no exponent.
bool ParseDecimal(std::string_view text, bool allowFraction, int64_t& whole, int64_t& thousandths) {
    size_t dot = text.find('.');
    if (dot != std::string_view::npos && !allowFraction) return false;
    std::string_view integer = text.substr(0, dot);
    std::string_view fraction = dot == std::string_view::npos ? std::string_view() : text.substr(dot + 1);
    if ((integer.empty() && fraction.empty()) || integer.size() > kMaxTimeDigits) return false;
    whole = 0;
    for (char c : integer) {
        if (!std::isdigit(static_cast<unsigned char>(c))) return false;
        whole = whole * 10 + (c - '0');
    }
    thousandths = 0;
    int place = 100;
    for (char c : fraction) {
        if (!std::isdigit(static_cast<unsigned char>(c))) return false;
        thousandths += place * (c - '0');
        place /= 10;
    }
    return true;
}

}  // namespace

std::optional<int64_t> ParseTtmlTime(std::string_view text) {
    std::string trimmed = Trimmed(text);
    std::string_view rest = trimmed;
    bool milliseconds = false;
    if (rest.size() > 2 && rest.substr(rest.size() - 2) == "ms") {
        milliseconds = true;
        rest.remove_suffix(2);
    } else if (!rest.empty() && rest.back() == 's') {
        rest.remove_suffix(1);
    }
    if (rest.empty()) return std::nullopt;

    int64_t parts[3] = {0, 0, 0};
    int64_t thousandths = 0;
    int64_t noFraction = 0;
    size_t count = 0;
    while (true) {
        size_t colon = rest.find(':');
        std::string_view part = rest.substr(0, colon);
        const bool last = colon == std::string_view::npos;
        if (count == 3 || (!last && milliseconds)) return std::nullopt;
        // Only the last part may carry a fraction.
        if (!ParseDecimal(part, last, parts[count], last ? thousandths : noFraction)) return std::nullopt;
        ++count;
        if (last) break;
        rest.remove_prefix(colon + 1);
    }
    if (milliseconds) return parts[0];
    int64_t seconds = 0;
    for (size_t i = 0; i < count; ++i) seconds = seconds * 60 + parts[i];
    return seconds * 1000 + thousandths;
}

namespace {

// ---- The scanner --------------------------------------------------------------------------------------------------

struct Attribute {
    std::string name;  // lower case, without the namespace prefix: "ttm:role" is "role"
    std::string value;
};

struct Tag {
    std::string name;  // same form
    std::vector<Attribute> attributes;
    bool closing = false;
    bool selfClosing = false;

    const std::string* Find(std::string_view attribute) const {
        for (const Attribute& a : attributes) {
            if (a.name == attribute) return &a.value;
        }
        return nullptr;
    }
};

std::string LocalLowerName(std::string_view raw) {
    size_t colon = raw.rfind(':');
    if (colon != std::string_view::npos) raw.remove_prefix(colon + 1);
    return ToLowerAscii(raw);
}

// Reads the tag whose '<' is at `pos`. Returns the index after '>', or npos when the input ends inside the tag.
size_t ReadTag(std::string_view in, size_t pos, Tag& tag) {
    size_t i = pos + 1;
    if (i < in.size() && in[i] == '/') {
        tag.closing = true;
        ++i;
    }
    size_t nameStart = i;
    while (i < in.size() && !IsSpace(in[i]) && in[i] != '/' && in[i] != '>') ++i;
    tag.name = LocalLowerName(in.substr(nameStart, i - nameStart));
    while (i < in.size()) {
        while (i < in.size() && IsSpace(in[i])) ++i;
        if (i >= in.size()) break;
        if (in[i] == '>') return i + 1;
        if (in[i] == '/') {
            tag.selfClosing = true;
            ++i;
            continue;
        }
        size_t attributeStart = i;
        while (i < in.size() && !IsSpace(in[i]) && in[i] != '=' && in[i] != '>' && in[i] != '/') ++i;
        Attribute attribute{LocalLowerName(in.substr(attributeStart, i - attributeStart)), ""};
        while (i < in.size() && IsSpace(in[i])) ++i;
        if (i < in.size() && in[i] == '=') {
            ++i;
            while (i < in.size() && IsSpace(in[i])) ++i;
            if (i < in.size() && (in[i] == '"' || in[i] == '\'')) {
                char quote = in[i++];
                size_t valueStart = i;
                while (i < in.size() && in[i] != quote) ++i;
                attribute.value = DecodeEntities(in.substr(valueStart, i - valueStart));
                if (i < in.size()) ++i;
            } else {
                size_t valueStart = i;
                while (i < in.size() && !IsSpace(in[i]) && in[i] != '>') ++i;
                attribute.value = DecodeEntities(in.substr(valueStart, i - valueStart));
            }
        }
        if (!attribute.name.empty()) tag.attributes.push_back(std::move(attribute));
    }
    return std::string_view::npos;
}

// Text with runs of white space folded into one space, appended to `target` (nothing at its start).
void AppendFolded(std::string& target, std::string_view text) {
    for (char c : text) {
        if (IsSpace(c)) {
            if (!target.empty() && target.back() != ' ' && target.back() != '\n') target += ' ';
        } else {
            target += c;
        }
    }
}

std::optional<int64_t> TimeOf(const Tag& tag, std::string_view attribute) {
    const std::string* value = tag.Find(attribute);
    return value ? ParseTtmlTime(*value) : std::nullopt;
}

struct Timing {
    std::optional<int64_t> begin;
    std::optional<int64_t> end;
};

Timing TimingOf(const Tag& tag) {
    Timing timing{TimeOf(tag, "begin"), TimeOf(tag, "end")};
    if (!timing.end && timing.begin) {
        if (std::optional<int64_t> duration = TimeOf(tag, "dur")) timing.end = *timing.begin + *duration;
    }
    return timing;
}

struct Frame {
    std::string name;
    bool word = false;  // a span with its own begin time
    bool ignored = false;  // a translation or a transliteration: not part of the lyric line
    Timing timing;
    std::string own;  // the text directly inside this span
};

struct PendingLine {
    LyricLine line;
    Timing timing;
    std::vector<Timing> wordEnds;  // parallel to line.words: the end each word declared
};

class Scanner {
public:
    Lyrics Run(std::string_view in) {
        size_t i = 0;
        while (i < in.size()) {
            size_t lt = in.find('<', i);
            if (lt == std::string_view::npos) lt = in.size();
            if (lt > i) Text(in.substr(i, lt - i));
            if (lt >= in.size()) break;
            i = Markup(in, lt);
            if (i == std::string_view::npos) break;
        }
        while (!m_stack.empty()) Pop();
        FinishLine();  // a line whose element was never pushed (nesting beyond the limit)
        return Finish();
    }

private:
    // Handles the markup at `pos`; returns where scanning goes on.
    size_t Markup(std::string_view in, size_t pos) {
        if (in.compare(pos, 4, "<!--") == 0) return SkipPast(in, pos + 4, "-->");
        if (in.compare(pos, 9, "<![CDATA[") == 0) {
            size_t end = in.find("]]>", pos + 9);
            size_t stop = end == std::string_view::npos ? in.size() : end;
            AddText(in.substr(pos + 9, stop - pos - 9));
            return end == std::string_view::npos ? in.size() : end + 3;
        }
        if (pos + 1 >= in.size()) return in.size();
        char next = in[pos + 1];
        if (next == '?') return SkipPast(in, pos + 2, "?>");
        if (next == '!') return SkipPast(in, pos + 2, ">");
        if (!std::isalpha(static_cast<unsigned char>(next)) && next != '/') {
            Text("<");  // a stray '<' in the text
            return pos + 1;
        }
        Tag tag;
        size_t after = ReadTag(in, pos, tag);
        if (after == std::string_view::npos) return after;
        if (tag.closing) Close(tag.name);
        else Open(tag);
        return after;
    }

    static size_t SkipPast(std::string_view in, size_t from, std::string_view end) {
        size_t found = in.find(end, from);
        return found == std::string_view::npos ? in.size() : found + end.size();
    }

    void Open(const Tag& tag) {
        if (tag.name == "tt" && m_language.empty()) {
            if (const std::string* lang = tag.Find("lang")) m_language = *lang;
        }
        if (tag.name == "br") {
            if (InLine() && !Ignored()) {
                while (!m_text.empty() && m_text.back() == ' ') m_text.pop_back();
                if (!m_text.empty() && m_text.back() != '\n') m_text += '\n';
            }
            return;
        }
        if (tag.name == "p") {
            FinishLine();
            m_line = PendingLine{};
            m_line.timing = TimingOf(tag);
            m_inLine = true;
        }
        if (tag.selfClosing) {
            if (tag.name == "p") FinishLine();
            return;
        }
        if (m_stack.size() >= kMaxOpenElements) return;
        Frame frame;
        frame.name = tag.name;
        if (tag.name == "span") {
            frame.timing = TimingOf(tag);
            frame.word = frame.timing.begin.has_value();
            const std::string* role = tag.Find("role");
            frame.ignored = role && (*role == "x-translation" || *role == "x-roman");
        }
        m_stack.push_back(std::move(frame));
    }

    void Close(const std::string& name) {
        auto found = std::find_if(m_stack.rbegin(), m_stack.rend(), [&](const Frame& f) { return f.name == name; });
        if (found == m_stack.rend()) return;
        while (m_stack.back().name != name) Pop();
        Pop();
    }

    void Pop() {
        Frame frame = std::move(m_stack.back());
        m_stack.pop_back();
        if (frame.name == "span" && frame.word && !frame.ignored && InLine()) {
            std::string text = Trimmed(frame.own);
            if (!text.empty()) {
                m_line.line.words.push_back({*frame.timing.begin, frame.timing.end.value_or(*frame.timing.begin), text});
                m_line.wordEnds.push_back(frame.timing);
            }
        }
        if (frame.name == "p") FinishLine();
    }

    bool InLine() const { return m_inLine; }
    bool Ignored() const {
        return std::any_of(m_stack.begin(), m_stack.end(), [](const Frame& f) { return f.ignored; });
    }

    void Text(std::string_view raw) { AddText(DecodeEntities(raw)); }

    void AddText(std::string_view text) {
        if (!InLine() || Ignored()) return;
        AppendFolded(m_text, text);
        if (!m_stack.empty() && m_stack.back().name == "span") AppendFolded(m_stack.back().own, text);
    }

    void FinishLine() {
        if (!m_inLine) return;
        m_inLine = false;
        std::string text = Trimmed(m_text);
        m_text.clear();
        if (text.empty()) return;
        m_line.line.text = std::move(text);
        m_lines.push_back(std::move(m_line));
        m_line = PendingLine{};
    }

    // Fills in the times a line or a word did not declare, from its neighbours.
    Lyrics Finish() {
        Lyrics lyrics;
        lyrics.language = m_language;
        for (const PendingLine& pending : m_lines) {
            if (pending.timing.begin || !pending.line.words.empty()) lyrics.synced = true;
        }
        for (size_t i = 0; i < m_lines.size(); ++i) {
            PendingLine& pending = m_lines[i];
            LyricLine line = std::move(pending.line);
            if (lyrics.synced) {
                int64_t previousEnd = lyrics.lines.empty() ? 0 : lyrics.lines.back().endMs;
                line.startMs = pending.timing.begin ? *pending.timing.begin
                               : !line.words.empty() ? line.words.front().startMs
                                                      : previousEnd;
                std::optional<int64_t> nextStart;
                if (i + 1 < m_lines.size()) {
                    const PendingLine& next = m_lines[i + 1];
                    nextStart = next.timing.begin ? next.timing.begin
                                : !next.line.words.empty() ? std::optional<int64_t>(next.line.words.front().startMs)
                                                           : std::nullopt;
                }
                line.endMs = pending.timing.end ? *pending.timing.end
                             : !line.words.empty() ? line.words.back().endMs
                                                    : nextStart.value_or(line.startMs);
                line.endMs = std::max(line.endMs, line.startMs);
                for (size_t w = 0; w < line.words.size(); ++w) {
                    LyricWord& word = line.words[w];
                    if (!pending.wordEnds[w].end) {
                        word.endMs = w + 1 < line.words.size() ? line.words[w + 1].startMs : line.endMs;
                    }
                    word.endMs = std::max(word.endMs, word.startMs);
                }
            } else {
                line.words.clear();
            }
            lyrics.lines.push_back(std::move(line));
        }
        for (const LyricLine& line : lyrics.lines) {
            if (!lyrics.text.empty()) lyrics.text += '\n';
            lyrics.text += line.text;
        }
        return lyrics;
    }

    std::vector<Frame> m_stack;
    std::vector<PendingLine> m_lines;
    PendingLine m_line;
    bool m_inLine = false;
    std::string m_text;
    std::string m_language;
};

Lyrics FromStatic(const std::string& staticText) {
    Lyrics lyrics;
    std::vector<std::string> lines;
    size_t start = 0;
    while (start <= staticText.size()) {
        size_t end = staticText.find('\n', start);
        if (end == std::string::npos) end = staticText.size();
        lines.push_back(Trimmed(std::string_view(staticText).substr(start, end - start)));
        start = end + 1;
    }
    // No blank line at either end, and never two in a row: a blank line only separates stanzas.
    while (!lines.empty() && lines.back().empty()) lines.pop_back();
    size_t first = 0;
    while (first < lines.size() && lines[first].empty()) ++first;
    for (size_t i = first; i < lines.size(); ++i) {
        if (lines[i].empty() && lyrics.lines.back().text.empty()) continue;
        LyricLine line;
        line.text = lines[i];
        lyrics.lines.push_back(std::move(line));
    }
    for (size_t i = 0; i < lyrics.lines.size(); ++i) {
        if (i > 0) lyrics.text += '\n';
        lyrics.text += lyrics.lines[i].text;
    }
    return lyrics;
}

Json WordsJson(const std::vector<LyricWord>& words) {
    Json::Array out;
    for (const LyricWord& word : words) {
        Json::Object member;
        member.emplace_back("start", Json(static_cast<double>(word.startMs)));
        member.emplace_back("end", Json(static_cast<double>(word.endMs)));
        member.emplace_back("text", Json(word.text));
        out.emplace_back(std::move(member));
    }
    return Json(std::move(out));
}

}  // namespace

Lyrics ParseTtml(std::string_view ttml) {
    if (ttml.size() > kMaxLyricsBytes) return {};
    return Scanner().Run(ttml);
}

namespace {

// The words of a line, timed. Apple times many lines only as a whole: their words then share the line's span by
// length, so they still light up one after the other. Kept out of the parsed lyrics, which say only what Apple gave.
std::vector<LyricWord> TimedWords(const LyricLine& line) {
    if (!line.words.empty() || line.endMs <= line.startMs) return line.words;
    std::vector<LyricWord> words;
    size_t letters = 0;
    for (size_t at = 0; at < line.text.size();) {
        size_t end = line.text.find(' ', at);
        if (end == std::string::npos) end = line.text.size();
        if (end > at) {
            words.push_back({0, 0, line.text.substr(at, end - at)});
            letters += end - at;
        }
        at = end + 1;
    }
    const double span = static_cast<double>(line.endMs - line.startMs);
    size_t before = 0;
    for (LyricWord& word : words) {
        word.startMs = line.startMs + static_cast<int64_t>(span * before / letters);
        before += word.text.size();
        word.endMs = line.startMs + static_cast<int64_t>(span * before / letters);
    }
    return words;
}

}  // namespace

KaraokeFrame KaraokeAt(const Lyrics& lyrics, int64_t positionMs) {
    KaraokeFrame frame;
    if (!lyrics.synced) return frame;
    // Lines come in time order: the last one whose start has passed.
    auto after = std::upper_bound(lyrics.lines.begin(), lyrics.lines.end(), positionMs,
                                  [](int64_t at, const LyricLine& line) { return at < line.startMs; });
    if (after == lyrics.lines.begin()) return frame;
    const LyricLine& line = *(after - 1);
    frame.line = static_cast<int>(after - lyrics.lines.begin()) - 1;
    frame.between = line.endMs > line.startMs && positionMs >= line.endMs;
    const std::vector<LyricWord> words = TimedWords(line);
    frame.words.reserve(words.size());
    for (const LyricWord& word : words) {
        double done = 0;
        if (positionMs >= word.endMs) done = 1;
        else if (positionMs > word.startMs) done = static_cast<double>(positionMs - word.startMs) / (word.endMs - word.startMs);
        frame.words.push_back(done);
    }
    return frame;
}

std::vector<std::string> KaraokePieces(const LyricLine& line) {
    const std::vector<LyricWord> words = TimedWords(line);
    std::vector<std::string> pieces;
    pieces.reserve(words.size());
    size_t cursor = 0;
    for (size_t i = 0; i < words.size(); i++) {
        const std::string& word = words[i].text;
        size_t at = line.text.find(word, cursor);
        bool last = i + 1 == words.size();
        if (at == std::string::npos) {  // a text that does not hold the word: spaces between all words
            pieces.push_back(last ? word : word + " ");
            continue;
        }
        cursor = at + word.size();
        bool spaced = cursor < line.text.size() && line.text[cursor] == ' ';
        pieces.push_back(spaced && !last ? word + " " : word);
    }
    return pieces;
}

std::optional<Lyrics> LyricsFromRaw(const RawLyrics& raw) {
    Lyrics lyrics = ParseTtml(raw.ttml);
    if (lyrics.lines.empty() && raw.staticText.size() <= kMaxLyricsBytes) lyrics = FromStatic(raw.staticText);
    if (lyrics.lines.empty()) return std::nullopt;
    if (!raw.language.empty()) lyrics.language = raw.language;
    return lyrics;
}

Json LyricsJson(const std::optional<Lyrics>& lyrics) {
    if (!lyrics) return Json();
    Json::Array lines;
    for (const LyricLine& line : lyrics->lines) {
        Json::Object member;
        if (lyrics->synced) {
            member.emplace_back("start", Json(static_cast<double>(line.startMs)));
            member.emplace_back("end", Json(static_cast<double>(line.endMs)));
        }
        member.emplace_back("text", Json(line.text));
        member.emplace_back("words", WordsJson(line.words));
        lines.emplace_back(std::move(member));
    }
    Json::Object root;
    root.emplace_back("source", Json("apple"));
    root.emplace_back("synced", Json(lyrics->synced));
    root.emplace_back("language", Json(lyrics->language));
    root.emplace_back("lines", Json(std::move(lines)));
    root.emplace_back("text", Json(lyrics->text));
    return Json(std::move(root));
}

}  // namespace ampify
