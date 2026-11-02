#include "anonymize.hpp"

#include <cctype>
#include <cstdlib>
#include <regex>

namespace ampify::cli {

namespace {

char Lower(char c) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

bool IsWordByte(char c) {
    // Bytes of a UTF-8 sequence count as letters, so "Zoé" is one word.
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || static_cast<unsigned char>(c) >= 0x80;
}

// Replaces every occurrence of `needle`, ignoring ASCII case. With `wholeWord`, only where no letter or digit touches it.
std::string ReplaceIgnoringCase(const std::string& text, std::string_view needle, std::string_view by, bool wholeWord) {
    if (needle.empty() || text.size() < needle.size()) return text;
    std::string result;
    size_t i = 0;
    while (i < text.size()) {
        bool match = i + needle.size() <= text.size();
        for (size_t k = 0; match && k < needle.size(); k++) match = Lower(text[i + k]) == Lower(needle[k]);
        if (match && wholeWord) {
            bool before = i > 0 && IsWordByte(text[i - 1]);
            bool after = i + needle.size() < text.size() && IsWordByte(text[i + needle.size()]);
            bool marker = i > 0 && text[i - 1] == '<' && i + needle.size() < text.size() && text[i + needle.size()] == '>';
            match = !before && !after && !marker;
        }
        if (match) {
            result += by;
            i += needle.size();
        } else {
            result += text[i++];
        }
    }
    return result;
}

std::string Swapped(std::string path, char from, char to) {
    for (char& c : path) {
        if (c == from) c = to;
    }
    return path;
}

bool Contains(const std::string& line, std::string_view part) {
    return line.find(part) != std::string::npos;
}

bool HasHexRun(const std::string& line, size_t length) {
    size_t run = 0;
    for (char c : line) {
        run = std::isxdigit(static_cast<unsigned char>(c)) ? run + 1 : 0;
        if (run >= length) return true;
    }
    return false;
}

// A line written by the player or by an extension ("[id] message"), after the optional "[time] " stamp.
bool IsMediaLine(const std::string& line) {
    static const std::regex kExtensionLine(R"(^(\[[0-9:. -]+\]\s*)?\[[a-z0-9][a-z0-9-]*\]\s)");
    return Contains(line, "Player:") || (!line.empty() && line.find('[') != std::string::npos &&
                                         std::regex_search(line, kExtensionLine));
}

struct Rule {
    std::regex pattern;
    const char* replacement;
    bool (*worth)(const std::string&);  // a cheap test that skips lines the pattern cannot match
};

const std::vector<Rule>& Rules() {
    using std::regex_constants::icase;
    static const std::vector<Rule> rules = {
        {std::regex(R"(([A-Za-z]:[\\/](?:Users|Documents and Settings)[\\/])[^\\/"'\s<>]+)", icase), "$1<user>",
         [](const std::string& l) { return Contains(l, ":\\") || Contains(l, ":/"); }},
        {std::regex(R"(/home/[^/\s"']+)"), "/home/<user>", [](const std::string& l) { return Contains(l, "/home/"); }},
        {std::regex(R"([A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,})"), "<email>",
         [](const std::string& l) { return Contains(l, "@"); }},
        {std::regex(R"(amp-artwork-id://[^\s"']+)"), "<artwork>",
         [](const std::string& l) { return Contains(l, "artwork-id://"); }},
        {std::regex(R"(\b[0-9]{6,}\.dbid\b)"), "<id>.dbid", [](const std::string& l) { return Contains(l, ".dbid"); }},
        {std::regex(R"(DBID:\s*0x[0-9A-Fa-f]+)", icase), "DBID:<id>",
         [](const std::string& l) { return Contains(l, "DBID") || Contains(l, "dbid"); }},
        {std::regex(R"(\b[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}\b)"), "<uuid>",
         [](const std::string& l) { return HasHexRun(l, 8) && Contains(l, "-"); }},
        {std::regex(R"(\b[0-9A-Fa-f]{32,}\b)"), "<hash>", [](const std::string& l) { return HasHexRun(l, 32); }},
        {std::regex(R"(\bvariant\s+[A-Za-z0-9]{6,}\b)"), "variant <id>", [](const std::string& l) { return Contains(l, "variant "); }},
        {std::regex(R"(\b(access_token|refresh_token|token|api_key|apikey|key|secret|session|sk|password|auth|sig|signature)=[^&\s"']+)",
                    icase),
         "$1=<hidden>", [](const std::string& l) { return Contains(l, "="); }},
        {std::regex(R"(\b(Bearer|Basic)\s+[A-Za-z0-9._~+/=-]{8,})", icase), "$1 <hidden>",
         [](const std::string& l) { return Contains(l, "Bearer") || Contains(l, "Basic") || Contains(l, "bearer"); }},
        {std::regex(R"re(\b(track|song|title|artist|album|playlist|station|cover of|lyrics of|lyrics for|playing)(\s*[:=]?\s*)"[^"]*")re",
                    icase),
         "$1$2\"<text>\"", [](const std::string& l) { return Contains(l, "\""); }},
    };
    return rules;
}

std::string AnonymizeLine(std::string line, const Identity& who) {
    for (const std::string& home : who.homes) {
        if (home.size() < 4) continue;
        line = ReplaceIgnoringCase(line, Swapped(home, '/', '\\'), "<home>", false);
        line = ReplaceIgnoringCase(line, Swapped(home, '\\', '/'), "<home>", false);
    }
    for (const Rule& rule : Rules()) {
        if (rule.worth(line)) line = std::regex_replace(line, rule.pattern, rule.replacement);
    }
    if (IsMediaLine(line) && Contains(line, "\"")) {
        static const std::regex kQuoted(R"("[^"]*")");
        line = std::regex_replace(line, kQuoted, "\"<text>\"");
    }
    // Last, so "<user>" and "<home>" written above are not taken for the name. Short names would mask common words.
    if (who.user.size() >= 3) line = ReplaceIgnoringCase(line, who.user, "<user>", true);
    if (who.computer.size() >= 3) line = ReplaceIgnoringCase(line, who.computer, "<computer>", true);
    return line;
}

std::string Environment(const char* name) {
#ifdef _MSC_VER
    char* value = nullptr;
    size_t length = 0;
    if (_dupenv_s(&value, &length, name) != 0 || value == nullptr) return {};
    std::string text(value);
    std::free(value);
    return text;
#else
    const char* value = std::getenv(name);
    return value ? value : "";
#endif
}

}  // namespace

Identity CurrentIdentity() {
    Identity who;
    for (const char* name : {"USERPROFILE", "HOME"}) {
        std::string home = Environment(name);
        while (home.size() > 1 && (home.back() == '\\' || home.back() == '/')) home.pop_back();
        if (!home.empty()) who.homes.push_back(home);
    }
    who.user = Environment("USERNAME");
    if (who.user.empty()) who.user = Environment("USER");
    who.computer = Environment("COMPUTERNAME");
    if (who.computer.empty()) who.computer = Environment("HOSTNAME");
    return who;
}

std::string Anonymize(std::string_view text, const Identity& who) {
    std::string result;
    result.reserve(text.size());
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string_view::npos) end = text.size();
        result += AnonymizeLine(std::string(text.substr(start, end - start)), who);
        if (end == text.size()) break;
        result += '\n';
        start = end + 1;
    }
    return result;
}

std::string HideSecretSettings(std::string_view settings) {
    static const std::regex kSecretWord(
        R"((^|[._-])(token|secret|password|passwd|key|apikey|session|sk|auth|cookie|user|username|account|email|login)([._-]|$))",
        std::regex_constants::icase);
    std::string result;
    size_t start = 0;
    while (start < settings.size()) {
        size_t end = settings.find('\n', start);
        if (end == std::string_view::npos) end = settings.size();
        std::string line(settings.substr(start, end - start));
        size_t equals = line.find('=');
        bool comment = line.find_first_not_of(" \t") != std::string::npos && line[line.find_first_not_of(" \t")] == '#';
        if (!comment && equals != std::string::npos) {
            std::string key = line.substr(0, equals);
            while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.pop_back();
            if (std::regex_search(key, kSecretWord)) line = line.substr(0, equals + 1) + " <hidden>";
        }
        result += line;
        if (end < settings.size()) result += '\n';
        start = end + 1;
    }
    return result;
}

}  // namespace ampify::cli
