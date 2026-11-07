#include "manifest.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <initializer_list>
#include <set>
#include <string_view>
#include <vector>

#include "json.hpp"
#include "permissions.hpp"
#include "release_facts.hpp"
#include "text_util.hpp"

namespace ampify {

namespace {

constexpr std::string_view kKnownFields[] = {
    "$schema", "id", "name", "version", "kind", "author", "description", "kitApi", "homepage", "license",
    "main", "colors", "defaultScheme", "resources", "icon", "permissions", "appVersions", "processes", "type",
    "preview", "tags", "settings", "translations",
};

constexpr std::string_view kTranslationMembers[] = {"name", "description", "settings", "options", "groups",
                                                    "settingDescriptions"};
constexpr std::string_view kSettingTypes[] = {"toggle", "choice", "text", "number"};  // in SettingSpec::Type order

constexpr size_t kMaxIdLength = 40;
constexpr size_t kMaxName = 60;
constexpr size_t kMaxDescription = 300;
constexpr size_t kMaxLicense = 64;
constexpr size_t kMaxFileStem = 64;
constexpr size_t kMaxSchemeName = 32;
constexpr size_t kMaxAuthor = 39;
constexpr size_t kMaxTag = 20;
constexpr size_t kMaxPermissions = 32;
constexpr size_t kMaxTags = 5;
constexpr size_t kMaxAppVersions = 16;
constexpr size_t kMaxProcesses = 2;

constexpr const char* kSettingsKinds = "settings: only an extension, a theme or a snippet declares settings";
constexpr size_t kMaxSettings = 16;
constexpr size_t kMinOptions = 2;
constexpr size_t kMaxOptions = 20;
constexpr size_t kMaxOptionText = 40;
constexpr size_t kMaxLabel = 60;
constexpr size_t kMaxSettingText = 200;
constexpr size_t kMaxGroupText = 40;
constexpr size_t kMaxDescriptionText = 200;

std::string Between(size_t min, size_t max) {
    return std::to_string(min) + " to " + std::to_string(max) + " characters";
}

// Edit distance between two names (Levenshtein), to suggest the name a person meant.
size_t EditDistance(std::string_view a, std::string_view b) {
    std::vector<size_t> previous(b.size() + 1);
    std::vector<size_t> current(b.size() + 1);
    for (size_t j = 0; j <= b.size(); j++) previous[j] = j;
    for (size_t i = 1; i <= a.size(); i++) {
        current[0] = i;
        for (size_t j = 1; j <= b.size(); j++) {
            size_t substitution = previous[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1);
            current[j] = std::min({previous[j] + 1, current[j - 1] + 1, substitution});
        }
        std::swap(previous, current);
    }
    return previous[b.size()];
}

// " (did you mean \"x\"?)" for the known name closest to `text`, two edits at most; empty when none is close.
template <typename Names>
std::string DidYouMean(const std::string& text, const Names& names) {
    constexpr size_t kMaxEdits = 2;
    std::string_view best;
    size_t bestDistance = kMaxEdits + 1;
    for (std::string_view name : names) {
        size_t distance = EditDistance(text, name);
        if (distance < bestDistance) {
            bestDistance = distance;
            best = name;
        }
    }
    return best.empty() ? std::string() : " (did you mean \"" + std::string(best) + "\"?)";
}

std::vector<std::string_view> PermissionNames() {
    std::vector<std::string_view> names;
    for (const PermissionInfo& permission : kPermissions) names.push_back(permission.name);
    return names;
}

size_t CodePoints(std::string_view text) { return CodePointCount(text); }

bool IsAsciiAlnum(char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0; }
bool IsDigit(char c) { return c >= '0' && c <= '9'; }

// Numbers separated by dots, between `minParts` and `maxParts` of them: 1.0, 1.2.3.
bool IsDottedNumber(std::string_view text, size_t minParts, size_t maxParts) {
    size_t parts = 0;
    while (true) {
        size_t end = text.find('.');
        std::string_view part = text.substr(0, end);
        if (part.empty() || !std::all_of(part.begin(), part.end(), IsDigit)) return false;
        parts++;
        if (end == std::string_view::npos) break;
        text.remove_prefix(end + 1);
    }
    return parts >= minParts && parts <= maxParts;
}

// A plain file name at the root of the folder, with one of `extensions`: no folder, no "..".
bool IsRootFile(std::string_view name, std::initializer_list<std::string_view> extensions) {
    for (std::string_view extension : extensions) {
        if (name.size() <= extension.size() + 1 || !name.ends_with(extension)) continue;
        std::string_view stem = name.substr(0, name.size() - extension.size() - 1);
        if (name[stem.size()] != '.' || stem.size() > kMaxFileStem || !IsAsciiAlnum(stem.front())) continue;
        auto nameCharacter = [](char c) { return IsAsciiAlnum(c) || c == '.' || c == '_' || c == '-'; };
        if (std::all_of(stem.begin(), stem.end(), nameCharacter)) return true;
    }
    return false;
}

bool IsVersionText(std::string_view text) { return IsDottedNumber(text, 3, 3); }
bool IsKitApiText(std::string_view text) { return IsDottedNumber(text, 2, 2); }
bool IsAppVersionText(std::string_view text) { return IsDottedNumber(text, 2, 4); }
bool IsKindText(std::string_view text) { return KindNamed(text).has_value(); }
bool IsJsFile(std::string_view name) { return IsRootFile(name, {"js"}); }
bool IsIniFile(std::string_view name) { return IsRootFile(name, {"ini"}); }
bool IsXamlFile(std::string_view name) { return IsRootFile(name, {"xaml"}); }
bool IsSvgFile(std::string_view name) { return IsRootFile(name, {"svg"}); }
bool IsImageFile(std::string_view name) { return IsRootFile(name, {"png", "jpg", "jpeg"}); }
bool IsTypeText(std::string_view text) { return text == "script" || text == "module"; }
bool IsHttpsText(std::string_view text) { return IsHttpsAddress(text); }
bool IsProcessText(std::string_view text) { return text == "ui" || text == "engine"; }

// A GitHub user name: letters, digits and inner hyphens.
bool IsAuthorText(std::string_view text) {
    if (text.empty() || text.size() > kMaxAuthor || !IsAsciiAlnum(text.front()) || !IsAsciiAlnum(text.back())) {
        return false;
    }
    return std::all_of(text.begin(), text.end(), [](char c) { return IsAsciiAlnum(c) || c == '-'; });
}

bool IsTagText(std::string_view text) {
    auto lowerOrDigit = [](char c) { return (c >= 'a' && c <= 'z') || IsDigit(c); };
    if (text.empty() || text.size() > kMaxTag || !lowerOrDigit(text.front())) return false;
    return std::all_of(text.begin(), text.end(), [&](char c) { return lowerOrDigit(c) || c == '-'; });
}

std::string UnknownPermission(const std::string& permission) {
    return "unknown permission: " + permission + DidYouMean(permission, PermissionNames());
}

// One rule for a text field or for the items of a list.
struct TextRule {
    bool (*valid)(std::string_view) = nullptr;  // null for any text
    size_t minLength = 0;                       // in characters, 0 for none
    size_t maxLength = 0;
    std::string hint;                           // what a valid value looks like
    std::string (*refusal)(const std::string& value) = nullptr;  // the message, in place of "invalid value (hint)"

    bool Accepts(const std::string& value) const {
        size_t length = CodePoints(value);
        return length >= minLength && (maxLength == 0 || length <= maxLength) && (!valid || valid(value));
    }
    std::string Refusal(const std::string& value) const {
        return refusal ? refusal(value) : "invalid value (" + hint + ")";
    }
};

struct Rules {
    TextRule id{IsElementId, 0, 0, "lowercase letters, digits and hyphens, " + Between(2, kMaxIdLength)};
    TextRule name{nullptr, 1, kMaxName, Between(1, kMaxName)};
    TextRule version{IsVersionText, 0, 0, "format MAJOR.MINOR.PATCH"};
    TextRule kind{IsKindText, 0, 0, "extension, theme, app or snippet"};
    TextRule author{IsAuthorText, 0, 0, "GitHub username"};
    TextRule description{nullptr, 1, kMaxDescription, Between(1, kMaxDescription)};
    TextRule kitApi{IsKitApiText, 0, 0, "format MAJOR.MINOR"};
    TextRule homepage{IsHttpsText, 0, 0, "https:// address"};
    TextRule authoredHomepage{IsHttpsText, 0, kMaxAddressLength, "https:// address"};
    TextRule license{nullptr, 1, kMaxLicense, Between(1, kMaxLicense)};
    TextRule main{IsJsFile, 0, 0, "a .js file name, at the root of the folder"};
    TextRule colors{IsIniFile, 0, 0, "a .ini file name, at the root of the folder"};
    TextRule scheme{IsSchemeName, 0, 0, Between(1, kMaxSchemeName) + ": letters, digits, _ or -"};
    TextRule resources{IsXamlFile, 0, 0, "a .xaml file name, at the root of the folder"};
    TextRule icon{IsSvgFile, 0, 0, "a .svg file name, at the root of the folder"};
    TextRule preview{IsImageFile, 0, 0, "a .png, .jpg or .jpeg file name, at the root of the folder"};
    TextRule type{IsTypeText, 0, 0, "script or module"};
    TextRule process{IsProcessText, 0, 0, "ui or engine"};
    TextRule appVersion{IsAppVersionText, 0, 0, "Apple Music version, for example 1.1540.23042.0"};
    TextRule tag{IsTagText, 0, 0, "lowercase letters, digits and hyphens, " + Between(1, kMaxTag)};
    TextRule permission{IsKnownPermission, 0, 0, "", UnknownPermission};
};

const Rules& Rule() {
    static const Rules rules;
    return rules;
}

// The members of one JSON object, read under rules, with messages that start with the member's place.
class Fields {
public:
    Fields(const Json& object, std::string where, std::vector<std::string>& errors)
        : m_object(object), m_where(std::move(where)), m_errors(errors) {}

    const Json& Object() const { return m_object; }
    std::string Label(std::string_view key) const {
        return m_where.empty() ? std::string(key) : m_where + "." + std::string(key);
    }
    void Problem(std::string_view key, const std::string& text) { m_errors.push_back(Label(key) + ": " + text); }

    // The text, kept even when it breaks the rule, so a reader of a manifest with errors still has it.
    std::optional<std::string> Text(std::string_view key, const TextRule& rule, bool required) {
        const Json* member = m_object.Find(key);
        if (!member) {
            if (required) Problem(key, "required field missing");
            return std::nullopt;
        }
        if (!member->IsString()) {
            Problem(key, "text expected");
            return std::nullopt;
        }
        if (!rule.Accepts(*member->AsString())) Problem(key, rule.Refusal(*member->AsString()));
        return *member->AsString();
    }

    // The valid items of a list of texts, at most `maxItems`; a repeated item is refused when `unique`.
    std::vector<std::string> List(std::string_view key, const TextRule& rule, size_t maxItems, bool unique);

    // An object of texts of 1 to `max` characters, such as the labels of a translation.
    std::map<std::string, std::string> Texts(std::string_view key, size_t max);

    // Members not among `known`: an error, or a warning when `warnings` is given.
    template <typename Names>
    void Unknown(const Names& known, std::vector<std::string>* warnings);

private:
    const Json& m_object;
    std::string m_where;
    std::vector<std::string>& m_errors;
};

std::vector<std::string> Fields::List(std::string_view key, const TextRule& rule, size_t maxItems, bool unique) {
    std::vector<std::string> result;
    const Json* member = m_object.Find(key);
    if (!member) return result;
    if (!member->IsArray()) {
        Problem(key, "list expected");
        return result;
    }
    if (member->AsArray()->size() > maxItems) Problem(key, "at most " + std::to_string(maxItems) + " items");
    for (size_t i = 0; i < member->AsArray()->size(); i++) {
        const Json& item = (*member->AsArray())[i];
        std::string place = std::string(key) + "[" + std::to_string(i) + "]";
        if (!item.IsString()) {
            Problem(place, "text expected");
        } else if (!rule.Accepts(*item.AsString())) {
            Problem(place, rule.Refusal(*item.AsString()));
        } else if (unique && std::find(result.begin(), result.end(), *item.AsString()) != result.end()) {
            Problem(place, "duplicate value: " + *item.AsString());
        } else {
            result.push_back(*item.AsString());
        }
    }
    return result;
}

std::map<std::string, std::string> Fields::Texts(std::string_view key, size_t max) {
    std::map<std::string, std::string> texts;
    const Json* member = m_object.Find(key);
    if (!member) return texts;
    if (!member->IsObject()) {
        Problem(key, "object expected");
        return texts;
    }
    Fields inner(*member, Label(key), m_errors);
    const TextRule rule{nullptr, 1, max, Between(1, max)};
    for (const auto& [name, value] : *member->AsObject()) {
        (void)value;
        if (std::optional<std::string> text = inner.Text(name, rule, true); text && rule.Accepts(*text)) {
            texts[name] = *text;
        }
    }
    return texts;
}

template <typename Names>
void Fields::Unknown(const Names& known, std::vector<std::string>* warnings) {
    for (const auto& [key, value] : *m_object.AsObject()) {
        (void)value;
        if (std::find(std::begin(known), std::end(known), key) != std::end(known)) continue;
        if (warnings) warnings->push_back("unknown field ignored: " + Label(key) + DidYouMean(key, known));
        else Problem(key, "unknown field" + DidYouMean(key, known));
    }
}

// settings.txt holds one value per line and trims its ends: a value that would not come back as written is refused.
bool FitsSettingsFile(const std::string& text) {
    auto space = [](char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; };
    return text.find_first_of("\r\n") == std::string::npos &&
           (text.empty() || (!space(text.front()) && !space(text.back())));
}

// A choice's values: kMinOptions to kMaxOptions different texts of 1 to kMaxOptionText characters.
bool ReadOptions(const Json* field, std::vector<std::string>& options) {
    const Json::Array* list = field ? field->AsArray() : nullptr;
    if (!list || list->size() < kMinOptions || list->size() > kMaxOptions) return false;
    for (const Json& item : *list) {
        const std::string* option = item.AsString();
        if (!option || CodePoints(*option) < 1 || CodePoints(*option) > kMaxOptionText || !FitsSettingsFile(*option)) {
            return false;
        }
        if (std::find(options.begin(), options.end(), *option) != options.end()) return false;
        options.push_back(*option);
    }
    return true;
}

void ReadChoiceValue(const Json& item, const std::string& where, SettingSpec& spec, std::vector<std::string>& errors) {
    const Json* value = item.Find("default");
    if (item.Find("options") && !ReadOptions(item.Find("options"), spec.options)) {
        return errors.push_back(where + ": options must be " + std::to_string(kMinOptions) + " to " +
                                std::to_string(kMaxOptions) + " different texts of " + Between(1, kMaxOptionText) +
                                ", on one line, without spaces at either end");
    }
    if (!value || !value->AsString() ||
        std::find(spec.options.begin(), spec.options.end(), *value->AsString()) == spec.options.end()) {
        return errors.push_back(where + ": default must be one of the options");
    }
    spec.defaultValue = *value->AsString();
}

void ReadNumberValue(const Json& item, const std::string& where, SettingSpec& spec, std::vector<std::string>& errors) {
    const Json* value = item.Find("default");
    for (auto [name, bound] : {std::pair{"min", &spec.min}, std::pair{"max", &spec.max}}) {
        const Json* field = item.Find(name);
        if (!field) continue;
        if (!field->AsNumber()) return errors.push_back(where + ": " + name + " must be a number");
        *bound = *field->AsNumber();
    }
    if (!value || !value->AsNumber() || (spec.min && *value->AsNumber() < *spec.min) ||
        (spec.max && *value->AsNumber() > *spec.max)) {
        return errors.push_back(where + ": default must be a number between min and max");
    }
    spec.defaultValue = ShortestNumberText(*value->AsNumber());
}

// The type-dependent half of a setting: its options or bounds, and a default that fits them.
void ReadSettingValue(const Json& item, const std::string& where, SettingSpec& spec, std::vector<std::string>& errors) {
    const Json* value = item.Find("default");
    if ((item.Find("options") != nullptr) != (spec.type == SettingSpec::Type::Choice)) {
        errors.push_back(where + ": options belong to a choice, and a choice needs them");
    }
    if ((item.Find("min") || item.Find("max")) && spec.type != SettingSpec::Type::Number) {
        errors.push_back(where + ": min and max belong to a number");
    }
    switch (spec.type) {
        case SettingSpec::Type::Toggle:
            if (!value || !value->AsBool()) return errors.push_back(where + ": default must be true or false");
            spec.defaultValue = *value->AsBool() ? "true" : "false";
            return;
        case SettingSpec::Type::Choice: return ReadChoiceValue(item, where, spec, errors);
        case SettingSpec::Type::Text:
            if (!value || !value->AsString() || CodePoints(*value->AsString()) > kMaxSettingText ||
                !FitsSettingsFile(*value->AsString())) {
                return errors.push_back(where + ": default must be a text of " + std::to_string(kMaxSettingText) +
                                        " characters at most, on one line, without spaces at either end");
            }
            spec.defaultValue = *value->AsString();
            return;
        case SettingSpec::Type::Number: return ReadNumberValue(item, where, spec, errors);
    }
}

// The optional "group" or "description" of a setting: a text of 1 to `max` characters, on one line.
void ReadSettingText(const Json& item, const char* field, size_t max, const std::string& where, std::string& out,
                     std::vector<std::string>& errors) {
    const Json* value = item.Find(field);
    if (!value) return;
    const std::string* text = value->AsString();
    if (!text || CodePoints(*text) < 1 || CodePoints(*text) > max || text->find_first_of("\r\n") != std::string::npos) {
        errors.push_back(where + ": " + field + " must be " + Between(1, max) + ", on one line");
        return;
    }
    out = *text;
}

std::optional<SettingSpec::Type> SettingTypeNamed(std::string_view name) {
    for (size_t index = 0; index < std::size(kSettingTypes); index++) {
        if (kSettingTypes[index] == name) return static_cast<SettingSpec::Type>(index);
    }
    return std::nullopt;
}

// The fields every setting has: an unknown one, the key, the label and the type.
std::optional<SettingSpec::Type> ReadSettingHead(const Json& item, const std::string& where, SettingSpec& spec,
                                                 std::vector<std::string>& errors) {
    static constexpr std::string_view kFields[] = {"key", "label", "type", "default", "options", "min", "max",
                                                   "group", "description"};
    for (const auto& [field, value] : *item.AsObject()) {
        (void)value;
        if (std::find(std::begin(kFields), std::end(kFields), field) == std::end(kFields)) {
            errors.push_back(where + ": unknown field " + field);
        }
    }
    const std::string* key = item.Find("key") ? item.Find("key")->AsString() : nullptr;
    if (!key || !IsDeclarableSettingKey(*key)) {
        errors.push_back(where + ": key must be [a-z0-9][a-z0-9-]{0,31}, other than " + std::string(kEnabledKey));
    } else {
        spec.key = *key;
    }
    const std::string* label = item.Find("label") ? item.Find("label")->AsString() : nullptr;
    if (!label || CodePoints(*label) < 1 || CodePoints(*label) > kMaxLabel) {
        errors.push_back(where + ": label must be " + Between(1, kMaxLabel));
    } else {
        spec.label = *label;
    }
    const std::string* type = item.Find("type") ? item.Find("type")->AsString() : nullptr;
    std::optional<SettingSpec::Type> parsed = type ? SettingTypeNamed(*type) : std::nullopt;
    if (!parsed) errors.push_back(where + ": type must be toggle, choice, text or number");
    return parsed;
}

std::optional<SettingSpec> ReadSetting(const Json& item, const std::string& where, std::vector<std::string>& errors) {
    if (!item.IsObject()) {
        errors.push_back(where + " must be an object");
        return std::nullopt;
    }
    size_t before = errors.size();
    SettingSpec spec;
    std::optional<SettingSpec::Type> type = ReadSettingHead(item, where, spec, errors);
    if (!type) return std::nullopt;
    spec.type = *type;
    ReadSettingText(item, "group", kMaxGroupText, where, spec.group, errors);
    ReadSettingText(item, "description", kMaxDescriptionText, where, spec.description, errors);
    ReadSettingValue(item, where, spec, errors);
    if (errors.size() != before) return std::nullopt;
    return spec;
}

void ReadSettings(const Json& field, std::vector<SettingSpec>& settings, std::vector<std::string>& errors) {
    const Json::Array* list = field.AsArray();
    if (!list || list->empty() || list->size() > kMaxSettings) {
        errors.push_back("settings must be a list of 1 to " + std::to_string(kMaxSettings) + " entries");
        return;
    }
    for (size_t index = 0; index < list->size(); index++) {
        std::string where = "settings[" + std::to_string(index) + "]";
        std::optional<SettingSpec> spec = ReadSetting((*list)[index], where, errors);
        if (!spec) continue;
        bool taken = std::any_of(settings.begin(), settings.end(),
                                 [&](const SettingSpec& s) { return s.key == spec->key; });
        if (taken) errors.push_back(where + ": key " + spec->key + " is already used");
        else settings.push_back(std::move(*spec));
    }
}

// One language of "translations": a name and a description under the English rules, and the texts of the settings.
std::optional<ManifestText> ReadTranslation(Fields& entry, std::vector<std::string>& errors) {
    size_t before = errors.size();
    entry.Unknown(kTranslationMembers, nullptr);
    ManifestText text;
    text.name = entry.Text("name", Rule().name, true).value_or("");
    text.description = entry.Text("description", Rule().description, true).value_or("");
    text.settings = entry.Texts("settings", kMaxLabel);
    text.options = entry.Texts("options", kMaxLabel);
    text.groups = entry.Texts("groups", kMaxGroupText);
    text.settingDescriptions = entry.Texts("settingDescriptions", kMaxDescriptionText);
    if (errors.size() != before) return std::nullopt;
    return text;
}

// "translations": {"fr": {...}}, only for the languages Ampify speaks. A language with an error is left out.
std::map<std::string, ManifestText> ReadTranslations(Fields& fields, std::vector<std::string>& errors) {
    std::map<std::string, ManifestText> translations;
    const Json* member = fields.Object().Find("translations");
    if (!member) return translations;
    if (!member->IsObject()) {
        fields.Problem("translations", "object expected");
        return translations;
    }
    Fields languages(*member, fields.Label("translations"), errors);
    for (const auto& [code, entry] : *member->AsObject()) {
        if (std::find(std::begin(kTranslationLanguages), std::end(kTranslationLanguages), code) ==
            std::end(kTranslationLanguages)) {
            languages.Problem(code, "unknown language (" + std::string(kTranslationLanguageList) + ")");
        } else if (!entry.IsObject()) {
            languages.Problem(code, "object expected");
        } else {
            Fields language(entry, languages.Label(code), errors);
            if (std::optional<ManifestText> text = ReadTranslation(language, errors)) translations[code] = *text;
        }
    }
    return translations;
}

void CheckKitApi(Fields& fields, const std::string& kitApi, int supportedKitApiMajor) {
    if (!IsKitApiText(kitApi)) return;  // already refused by its rule
    int major = -1;
    const char* begin = kitApi.data();
    std::from_chars(begin, begin + kitApi.find('.'), major);  // a major too large to read stays -1: not supported
    if (major != supportedKitApiMajor) {
        fields.Problem("kitApi", "unsupported API version (expected " + std::to_string(supportedKitApiMajor) + ".x)");
    }
}

// The files a manifest names, its script's type, and the lists only a manifest has.
void ReadManifestOnly(Fields& fields, ManifestMode mode, Manifest& m) {
    const Rules& rules = Rule();
    m.main = fields.Text("main", rules.main, false);
    if (std::optional<std::string> type = fields.Text("type", rules.type, false)) m.module = *type == "module";
    m.colors = fields.Text("colors", rules.colors, false);
    m.defaultScheme = fields.Text("defaultScheme", rules.scheme, false);
    m.resources = fields.Text("resources", rules.resources, false);
    m.icon = fields.Text("icon", rules.icon, false);
    if (mode == ManifestMode::Author) m.preview = fields.Text("preview", rules.preview, false);
    // The kernel has always taken a repeated Apple Music version; authoring does not.
    m.appVersions = fields.List("appVersions", rules.appVersion, kMaxAppVersions, mode == ManifestMode::Author);
    if (fields.Object().Find("processes")) {
        m.processes = fields.List("processes", rules.process, kMaxProcesses, true);
        if (m.processes.empty()) fields.Problem("processes", "at least one process, ui or engine");
    }
}

// The settings field: only a kind that declares settings has it, and none counts when one breaks a rule.
void ReadSettingsField(const Json& root, std::optional<ElementKind> kind, Manifest& m, ManifestResult& result) {
    const Json* field = root.Find("settings");
    if (!field) return;
    if (!kind || !TraitsOf(*kind).declaresSettings) result.settingsErrors.push_back(kSettingsKinds);
    else ReadSettings(*field, m.settings, result.settingsErrors);
    if (!result.settingsErrors.empty()) m.settings.clear();
    result.errors.insert(result.errors.end(), result.settingsErrors.begin(), result.settingsErrors.end());
}

bool HasStylePart(const Manifest& m, std::string_view part, const std::function<bool(const std::string&)>& hasFile) {
    if (part == "colors") return m.colors.has_value();
    if (part == "resources") return m.resources.has_value();
    return hasFile && hasFile(std::string(part));
}

// The rules of the kinds table (element_ref.hpp), then the files the manifest names.
void CheckKindAndFiles(const Manifest& m, const std::function<bool(const std::string&)>& hasFile,
                       std::vector<std::string>& errors) {
    const ElementKindTraits& traits = TraitsOf(m.kind);
    std::string kind(traits.name);
    if (traits.main == ElementKindTraits::Main::Required && !m.main) errors.push_back("main: required for an " + kind);
    if (!traits.style[0].empty() && !HasStylePart(m, traits.style[0], hasFile) &&
        !HasStylePart(m, traits.style[1], hasFile)) {
        errors.push_back(std::string(traits.style[0]) + " or " + std::string(traits.style[1]) +
                         ": at least one of the two is required for a " + kind);
    }
    if (traits.main == ElementKindTraits::Main::Refused && m.main) errors.push_back("main: not allowed for a " + kind);
    if (!hasFile) return;
    const std::pair<const char*, const std::optional<std::string>*> files[] = {
        {"main", &m.main}, {"colors", &m.colors}, {"resources", &m.resources}, {"icon", &m.icon},
        {"preview", &m.preview}};
    for (const auto& [key, file] : files) {
        if (*file && !hasFile(**file)) errors.push_back(std::string(key) + ": file missing from the folder: " + **file);
    }
}

// A key written twice in one object: each field reads its first value, so the author is told, not refused.
void WarnRepeatedKeys(const Json& value, const std::string& where, std::vector<std::string>& warnings) {
    if (const Json::Object* members = value.AsObject()) {
        std::set<std::string> seen;
        for (const auto& [key, member] : *members) {
            std::string label = where.empty() ? key : where + "." + key;
            if (!seen.insert(key).second) warnings.push_back("duplicate key: " + label);
            WarnRepeatedKeys(member, label, warnings);
        }
    } else if (const Json::Array* items = value.AsArray()) {
        for (size_t i = 0; i < items->size(); i++) {
            WarnRepeatedKeys((*items)[i], where + "[" + std::to_string(i) + "]", warnings);
        }
    }
}

std::optional<Json> ParseRoot(std::string_view text, ManifestResult& result) {
    std::string parseError;
    std::optional<Json> root = Json::Parse(text, &parseError, Json::DuplicateKeys::Allow);
    if (!root || !root->IsObject()) {
        result.errors.push_back("manifest.json is not a valid JSON object" +
                                (parseError.empty() ? std::string() : ": " + parseError));
        return std::nullopt;
    }
    WarnRepeatedKeys(*root, "", result.warnings);
    return root;
}

}  // namespace

std::string_view ToString(SettingSpec::Type type) { return kSettingTypes[static_cast<size_t>(type)]; }

bool IsReleaseVersion(std::string_view text) { return IsVersionText(text); }

bool IsMainFileName(std::string_view text) { return IsJsFile(text); }

bool IsSchemeName(std::string_view text) {
    if (text.empty() || text.size() > kMaxSchemeName) return false;
    return std::all_of(text.begin(), text.end(), [](char c) { return IsAsciiAlnum(c) || c == '_' || c == '-'; });
}

bool IsHttpsAddress(std::string_view text, size_t maxLength) {
    constexpr std::string_view kScheme = "https://";
    return text.size() > kScheme.size() && text.starts_with(kScheme) &&
           text.find_first_of(" \t\n\v\f\r") == std::string_view::npos &&
           (maxLength == 0 || CodePoints(text) <= maxLength);
}

const SettingSpec* Manifest::Setting(std::string_view key) const {
    auto it = std::find_if(settings.begin(), settings.end(), [&](const SettingSpec& spec) { return spec.key == key; });
    return it == settings.end() ? nullptr : &*it;
}

bool ElementInfo::HasPermission(std::string_view permission) const {
    return std::find(permissions.begin(), permissions.end(), permission) != permissions.end();
}

bool Manifest::RunsIn(std::string_view process) const {
    return std::find(processes.begin(), processes.end(), process) != processes.end();
}

std::vector<std::string> Manifest::NetworkHosts() const {
    std::vector<std::string> hosts;
    for (const std::string& permission : permissions) {
        if (permission.starts_with(kNetworkPermissionPrefix)) {
            hosts.push_back(permission.substr(kNetworkPermissionPrefix.size()));
        }
    }
    return hosts;
}

void ReadElementInfo(const Json& object, std::string_view where, ManifestMode mode, int supportedKitApiMajor,
                     ElementInfo& info, std::vector<std::string>& errors) {
    const Rules& rules = Rule();
    Fields fields(object, std::string(where), errors);
    info.id = fields.Text("id", rules.id, true).value_or("");
    info.name = fields.Text("name", rules.name, true).value_or("");
    info.version = fields.Text("version", rules.version, true).value_or("");
    std::optional<std::string> kind = fields.Text("kind", rules.kind, true);
    info.kind = kind ? KindNamed(*kind).value_or(ElementKind::Extension) : ElementKind::Extension;
    info.author = fields.Text("author", rules.author, true).value_or("");
    info.description = fields.Text("description", rules.description, true).value_or("");
    info.translations = ReadTranslations(fields, errors);
    info.kitApi = fields.Text("kitApi", rules.kitApi, true).value_or("");
    CheckKitApi(fields, info.kitApi, supportedKitApiMajor);
    bool authoring = mode == ManifestMode::Author;
    info.homepage = fields.Text("homepage", authoring ? rules.authoredHomepage : rules.homepage, false);
    info.license = fields.Text("license", rules.license, false);
    info.permissions = fields.List("permissions", rules.permission, kMaxPermissions, true);
    if (authoring) info.tags = fields.List("tags", rules.tag, kMaxTags, true);
}

ManifestResult ParseManifest(std::string_view text, std::string_view folderName, int supportedKitApiMajor,
                             const std::function<bool(const std::string&)>& hasFile, ManifestMode mode) {
    ManifestResult result;
    std::optional<Json> root = ParseRoot(text, result);
    if (!root) return result;

    Fields fields(*root, "", result.errors);
    fields.Unknown(kKnownFields, mode == ManifestMode::Author ? nullptr : &result.warnings);
    Manifest& m = result.partial;
    ReadElementInfo(*root, "", mode, supportedKitApiMajor, m, result.errors);
    const Json* id = root->Find("id");
    if (!folderName.empty() && id && id->IsString() && m.id != folderName) {
        fields.Problem("id", "must be the folder name (" + std::string(folderName) + ")");
    }
    ReadManifestOnly(fields, mode, m);
    const Json* kind = root->Find("kind");
    std::optional<ElementKind> knownKind = kind && kind->AsString() ? KindNamed(*kind->AsString()) : std::nullopt;
    ReadSettingsField(*root, knownKind, m, result);
    CheckKindAndFiles(m, hasFile, result.errors);

    if (result.errors.empty()) result.manifest = m;
    return result;
}

std::vector<SettingSpec> DeclaredSettings(std::string_view manifestText, std::vector<std::string>* errors) {
    ManifestResult result = ParseManifest(manifestText, "", kKitApiMajor, nullptr);
    if (errors) *errors = std::move(result.settingsErrors);
    return std::move(result.partial.settings);
}

namespace {

bool IsOn(std::string_view lowered) {
    return lowered == "1" || lowered == "true" || lowered == "on" || lowered == "yes";
}

bool IsOff(std::string_view lowered) {
    return lowered == "0" || lowered == "false" || lowered == "off" || lowered == "no";
}

std::optional<double> SettingNumber(std::string_view text) {
    double number = 0;
    const char* end = text.data() + text.size();
    auto [stop, error] = std::from_chars(text.data(), end, number);
    if (text.empty() || error != std::errc() || stop != end || !std::isfinite(number)) return std::nullopt;
    return number;
}

}  // namespace

std::string SettingProblem(const SettingSpec& spec, std::string_view stored) {
    switch (spec.type) {
        case SettingSpec::Type::Toggle: {
            std::string lowered = ToLowerAscii(stored);
            return IsOn(lowered) || IsOff(lowered) ? std::string() : "\"" + std::string(stored) + "\" is not on or off";
        }
        case SettingSpec::Type::Choice: {
            if (std::find(spec.options.begin(), spec.options.end(), stored) != spec.options.end()) return {};
            std::string offered;
            for (const std::string& option : spec.options) offered += (offered.empty() ? "" : ", ") + option;
            return "\"" + std::string(stored) + "\" is not one of its choices: " + offered;
        }
        case SettingSpec::Type::Text:
            return {};
        case SettingSpec::Type::Number: {
            std::optional<double> number = SettingNumber(stored);
            if (!number) return "\"" + std::string(stored) + "\" is not a number";
            if (spec.min && *number < *spec.min) {
                return std::string(stored) + " is below its minimum, " + ShortestNumberText(*spec.min);
            }
            if (spec.max && *number > *spec.max) {
                return std::string(stored) + " is above its maximum, " + ShortestNumberText(*spec.max);
            }
            return {};
        }
    }
    return {};
}

std::string SettingValue(const SettingSpec& spec, const std::optional<std::string>& stored) {
    if (!stored || !SettingProblem(spec, *stored).empty()) return spec.defaultValue;
    switch (spec.type) {
        case SettingSpec::Type::Toggle:
            return IsOn(ToLowerAscii(*stored)) ? "true" : "false";
        case SettingSpec::Type::Number:
            return ShortestNumberText(*SettingNumber(*stored));
        case SettingSpec::Type::Choice:
        case SettingSpec::Type::Text:
            return *stored;
    }
    return spec.defaultValue;
}

}  // namespace ampify
