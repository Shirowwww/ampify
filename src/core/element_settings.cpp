#include "element_settings.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <utility>

#include "json.hpp"
#include "text_util.hpp"

namespace ampify::settings_panel {

namespace {

constexpr size_t kMaxSettingText = 200;  // the same as a text setting in docs/extensions/manifest.md

// The accented letters a reader often types without the accent, so that "reglage" finds "réglage". UTF-8 bytes.
const std::pair<const char*, const char*> kFoldedLetters[] = {
    {"\xC3\xA9", "e"}, {"\xC3\xA8", "e"}, {"\xC3\xAA", "e"}, {"\xC3\xAB", "e"}, {"\xC3\x89", "e"}, {"\xC3\x88", "e"},
    {"\xC3\xA0", "a"}, {"\xC3\xA2", "a"}, {"\xC3\x80", "a"}, {"\xC3\xB4", "o"}, {"\xC3\x94", "o"}, {"\xC3\xAE", "i"},
    {"\xC3\xAF", "i"}, {"\xC3\xB9", "u"}, {"\xC3\xBB", "u"}, {"\xC3\xBC", "u"}, {"\xC3\x99", "u"}, {"\xC3\xA7", "c"},
    {"\xC3\x87", "c"},
    // German, Spanish and Portuguese.
    {"\xC3\xA4", "a"}, {"\xC3\x84", "a"}, {"\xC3\xA1", "a"}, {"\xC3\x81", "a"}, {"\xC3\xA3", "a"}, {"\xC3\x83", "a"},
    {"\xC3\xB6", "o"}, {"\xC3\x96", "o"}, {"\xC3\xB3", "o"}, {"\xC3\x93", "o"}, {"\xC3\xB5", "o"}, {"\xC3\x95", "o"},
    {"\xC3\x9C", "u"}, {"\xC3\xBA", "u"}, {"\xC3\x9A", "u"}, {"\xC3\xAD", "i"}, {"\xC3\x8D", "i"}, {"\xC3\xB1", "n"},
    {"\xC3\x91", "n"}, {"\xC3\x9F", "ss"},
};

// Lower case, ASCII letters only, with the accents of kFoldedLetters taken off.
std::string Plain(std::string_view text) {
    std::string folded(text);
    for (const auto& [accented, plain] : kFoldedLetters) {
        for (size_t at = folded.find(accented); at != std::string::npos; at = folded.find(accented, at)) {
            folded.replace(at, std::char_traits<char>::length(accented), plain);
        }
    }
    return ToLowerAscii(folded);
}

// The words of a search: the text cut at spaces, each one in Plain form.
std::vector<std::string> WordsOf(std::string_view text) {
    std::vector<std::string> words;
    std::string word;
    for (char c : Plain(text)) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!word.empty()) words.push_back(std::move(word));
            word.clear();
        } else {
            word += c;
        }
    }
    if (!word.empty()) words.push_back(std::move(word));
    return words;
}

// A text as settings.txt keeps it: one line, no spaces at either end, at most 200 characters.
bool FitsSettingText(const std::string& text) {
    auto space = [](char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; };
    size_t characters = static_cast<size_t>(std::count_if(text.begin(), text.end(), [](char c) {
        return (static_cast<unsigned char>(c) & 0xC0) != 0x80;
    }));
    return characters <= kMaxSettingText && text.find_first_of("\r\n") == std::string::npos &&
           (text.empty() || (!space(text.front()) && !space(text.back())));
}

// The value a file gives for one setting, as settings.txt keeps it; nothing when its type or range does not fit.
std::optional<std::string> StoredValue(const SettingSpec& spec, const Json& value) {
    switch (spec.type) {
        case SettingSpec::Type::Toggle: {
            const bool* on = value.AsBool();
            if (!on) return std::nullopt;
            return std::string(*on ? "true" : "false");
        }
        case SettingSpec::Type::Number: {
            const double* number = value.AsNumber();
            if (!number || !std::isfinite(*number)) return std::nullopt;
            if ((spec.min && *number < *spec.min) || (spec.max && *number > *spec.max)) return std::nullopt;
            return SettingNumberText(*number);
        }
        case SettingSpec::Type::Choice: {
            const std::string* text = value.AsString();
            if (!text || std::find(spec.options.begin(), spec.options.end(), *text) == spec.options.end()) {
                return std::nullopt;
            }
            return *text;
        }
        case SettingSpec::Type::Text: {
            const std::string* text = value.AsString();
            if (!text || !FitsSettingText(*text)) return std::nullopt;
            return *text;
        }
    }
    return std::nullopt;
}

// The value of a setting as a file holds it: a switch, a number, or a text.
Json TypedValue(const SettingSpec& spec, const std::string& stored) {
    switch (spec.type) {
        case SettingSpec::Type::Toggle: return Json(stored == "true");
        case SettingSpec::Type::Number: return Json(std::strtod(stored.c_str(), nullptr));
        case SettingSpec::Type::Choice:
        case SettingSpec::Type::Text: break;
    }
    return Json(stored);
}

const std::string* TextField(const Json& object, std::string_view key) {
    const Json* field = object.Find(key);
    return field ? field->AsString() : nullptr;
}

// The format, version, kind and id of a file: they must be those of the element that is asked for.
bool CheckHeader(const Json& root, std::string_view kind, std::string_view id, std::string& error) {
    const std::string* format = TextField(root, "format");
    const Json* versionField = root.Find("version");
    const double* version = versionField ? versionField->AsNumber() : nullptr;
    if (!format || *format != kFileFormat || !version) {
        error = "not an Ampify settings file";
        return false;
    }
    if (*version != kFileVersion) {
        error = "settings file version " + SettingNumberText(*version) + " is not supported";
        return false;
    }
    const std::string* fileKind = TextField(root, "kind");
    const std::string* fileId = TextField(root, "id");
    if (!fileKind || *fileKind != kind || !fileId || *fileId != id) {
        error = "this file holds the settings of another element";
        return false;
    }
    return true;
}

}  // namespace

std::vector<SettingGroup> GroupSettings(const std::vector<SettingSpec>& settings) {
    std::vector<SettingGroup> groups;
    SettingGroup plain;
    for (size_t index = 0; index < settings.size(); index++) {
        if (settings[index].group.empty()) plain.settings.push_back(index);
    }
    if (!plain.settings.empty()) groups.push_back(std::move(plain));
    for (size_t index = 0; index < settings.size(); index++) {
        const std::string& name = settings[index].group;
        if (name.empty()) continue;
        auto found = std::find_if(groups.begin(), groups.end(), [&](const SettingGroup& g) { return g.name == name; });
        if (found == groups.end()) {
            groups.push_back(SettingGroup{name, {}});
            found = groups.end() - 1;
        }
        found->settings.push_back(index);
    }
    return groups;
}

std::vector<size_t> MatchingSettings(const std::vector<SettingText>& texts, std::string_view query) {
    std::vector<std::string> words = WordsOf(query);
    std::vector<size_t> found;
    for (size_t index = 0; index < texts.size(); index++) {
        std::string haystack = Plain(texts[index].label + " " + texts[index].description);
        bool all = std::all_of(words.begin(), words.end(),
                               [&](const std::string& word) { return haystack.find(word) != std::string::npos; });
        if (all) found.push_back(index);
    }
    return found;
}

std::map<std::string, std::string> DefaultValues(const std::vector<SettingSpec>& settings) {
    std::map<std::string, std::string> defaults;
    for (const SettingSpec& spec : settings) defaults[spec.key] = spec.defaultValue;
    return defaults;
}

std::string ExportSettings(std::string_view kind, std::string_view id, const std::vector<SettingSpec>& settings,
                           const std::map<std::string, std::string>& values) {
    Json::Object entries;
    for (const SettingSpec& spec : settings) {
        auto found = values.find(spec.key);
        std::optional<std::string> stored =
            found == values.end() ? std::nullopt : std::optional<std::string>(found->second);
        entries.emplace_back(spec.key, TypedValue(spec, SettingValue(spec, stored)));
    }
    Json root(Json::Object{{"format", Json(std::string(kFileFormat))},
                           {"version", Json(static_cast<double>(kFileVersion))},
                           {"kind", Json(std::string(kind))},
                           {"id", Json(std::string(id))},
                           {"values", Json(std::move(entries))}});
    return root.Dump();
}

std::optional<std::map<std::string, std::string>> ImportSettings(std::string_view text, std::string_view kind,
                                                                 std::string_view id,
                                                                 const std::vector<SettingSpec>& settings,
                                                                 std::string& error) {
    if (text.size() > kMaxFileBytes) {
        error = "the file is bigger than 64 KB";
        return std::nullopt;
    }
    std::optional<Json> root = Json::Parse(text, nullptr, Json::DuplicateKeys::Refuse);
    if (!root || !root->IsObject()) {
        error = "not an Ampify settings file: a JSON object is expected";
        return std::nullopt;
    }
    if (!CheckHeader(*root, kind, id, error)) return std::nullopt;
    const Json* values = root->Find("values");
    if (!values || !values->AsObject()) {
        error = "values must be an object";
        return std::nullopt;
    }
    std::map<std::string, std::string> accepted;
    for (const auto& [key, value] : *values->AsObject()) {
        auto spec = std::find_if(settings.begin(), settings.end(), [&](const SettingSpec& s) { return s.key == key; });
        if (spec == settings.end()) {
            error = "unknown setting: " + key;
            return std::nullopt;
        }
        std::optional<std::string> stored = StoredValue(*spec, value);
        if (!stored) {
            error = "wrong value for " + key;
            return std::nullopt;
        }
        accepted[key] = *stored;
    }
    return accepted;
}

}  // namespace ampify::settings_panel
