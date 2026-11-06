#include "mods.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <sstream>

#include "../extension.hpp"
#include "../json.hpp"
#include "../kit_layout.hpp"
#include "../manifest.hpp"
#include "../natives/service_flags.hpp"
#include "../reload.hpp"
#include "../settings.hpp"
#include "../text_util.hpp"

namespace ampify::uirender {

namespace fs = std::filesystem;

namespace {

constexpr std::uintmax_t kReloadLimit = 64 * 1024;  // rewritten past this size, as ampify does

// One write per call, so the kernel sees a whole line; a file that grew too big starts again.
bool AppendReload(const fs::path& kit, const std::string& line, std::string& error) {
    fs::path reload = kit / "reload.txt";
    std::error_code ec;
    std::uintmax_t size = fs::file_size(reload, ec);
    std::ios::openmode mode = std::ios::binary | (ec || size > kReloadLimit ? std::ios::trunc : std::ios::app);
    std::ofstream out(reload, mode);
    out << line << "\n";
    out.flush();
    if (!out) {
        error = "cannot write: " + reload.string();
        return false;
    }
    return true;
}

bool IsExtensionTarget(const std::string& id, std::string& error) {
    if (IsReloadTarget(id) && id.rfind("app:", 0) != 0) return true;
    error = "invalid extension id: " + id;
    return false;
}

// A key a manifest may declare, never the activation key.
bool IsSettingKey(const std::string& key, std::string& error) {
    bool valid = !key.empty() && key != "enabled" && std::all_of(key.begin(), key.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
    });
    if (!valid) error = "invalid setting key: " + key;
    return valid;
}

// settings.txt holds one value per line.
std::string OneLine(std::string value) {
    std::replace_if(value.begin(), value.end(), [](char c) { return c == '\r' || c == '\n'; }, ' ');
    return value;
}

// The valid manifests of a kind in one folder of the kit (extensions, themes), with their folder.
std::vector<Manifest> ReadManifests(const fs::path& root, ElementKind kind) {
    std::vector<Manifest> manifests;
    std::error_code ec;
    for (const fs::directory_entry& entry : fs::directory_iterator(root, ec)) {
        std::string folder = entry.path().filename().string();
        if (folder.empty() || folder[0] == '.' || !entry.is_directory(ec)) continue;
        std::optional<std::string> text = ReadWholeFile(entry.path() / "manifest.json");
        if (!text) continue;
        ManifestResult result = ParseManifest(*text, folder, kKitApiMajor, [&](const std::string& file) {
            std::error_code fileError;
            return fs::is_regular_file(entry.path() / file, fileError);
        });
        if (result.manifest && result.manifest->kind == kind) manifests.push_back(std::move(*result.manifest));
    }
    return manifests;
}

ModEntry EntryOf(const Manifest& manifest) {
    ModEntry entry;
    entry.id = manifest.id;
    entry.name = manifest.name.empty() ? manifest.id : manifest.name;
    entry.description = manifest.description;
    entry.translations = manifest.translations;
    return entry;
}

void SortByName(std::vector<ModEntry>& entries) {
    std::sort(entries.begin(), entries.end(), [](const ModEntry& a, const ModEntry& b) {
        return a.name == b.name ? a.id < b.id : a.name < b.name;
    });
}

}  // namespace

const std::string& ModEntry::NameIn(const std::string& language) const {
    auto found = translations.find(language);
    return found == translations.end() ? name : found->second.name;
}

const std::string& ModEntry::DescriptionIn(const std::string& language) const {
    auto found = translations.find(language);
    return found == translations.end() ? description : found->second.description;
}

std::vector<ModEntry> ListMods(const fs::path& kit) {
    std::vector<ModEntry> mods;
    Settings settings;
    settings.Load((kit / "settings.txt").string());

    for (const Manifest& manifest : ReadManifests(kit / "extensions", ElementKind::Extension)) {
        ModEntry mod = EntryOf(manifest);
        mod.enabled = settings.GetBool(ExtensionHost::SettingKey(mod.id), IsOnByDefault("extensions", mod.id));
        mod.settings = manifest.settings;
        for (const SettingSpec& spec : mod.settings) {
            mod.values.push_back(settings.GetString("extensions." + mod.id + "." + spec.key, spec.defaultValue));
        }
        mods.push_back(std::move(mod));
    }
    SortByName(mods);
    return mods;
}

bool IsModOn(const fs::path& kit, const std::string& id) {
    Settings settings;
    settings.Load((kit / "settings.txt").string());
    return settings.GetBool(ExtensionHost::SettingKey(id), IsOnByDefault("extensions", id));
}

std::vector<ModEntry> ListThemes(const fs::path& kit) {
    std::vector<ModEntry> themes;
    for (const Manifest& manifest : ReadManifests(kit / "themes", ElementKind::Theme)) {
        themes.push_back(EntryOf(manifest));
        themes.back().theme = true;
    }
    SortByName(themes);
    return themes;
}

bool ChooseTheme(const fs::path& kit, const std::string& id, std::string& error) {
    if (!id.empty() && !IsExtensionTarget(id, error)) {
        error = "invalid theme id: " + id;
        return false;
    }
    return Settings::UpdateFile((kit / "settings.txt").string(), [&id](Settings& settings) {
        std::string scheme = settings.GetString("theme.scheme", "");
        bool keep = scheme == "auto" || (!id.empty() && settings.GetString("theme.active", "") == id);
        settings.Set("theme.active", id.empty() ? "none" : id);
        if (!keep) settings.Remove("theme.scheme");
    }, error);
}

namespace {

constexpr char kAccentKey[] = "theme.accent";

// The same rule as theme::ParseAccent, which this library cannot call: ampify_theme links ampify_core, not the reverse.
std::optional<std::string> NormalizeAccent(std::string_view text) {
    if (!text.empty() && text.front() == '#') text.remove_prefix(1);
    if (text.size() != 6) return std::nullopt;
    std::string digits;
    for (char c : text) {
        if (!std::isxdigit(static_cast<unsigned char>(c))) return std::nullopt;
        digits.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    }
    return digits;
}

}  // namespace

std::string ChosenAccent(const fs::path& kit) {
    Settings settings;
    settings.Load((kit / "settings.txt").string());
    std::string value = settings.GetString(kAccentKey, "");
    if (value == kArtworkAccent) return value;
    return NormalizeAccent(value).value_or("");
}

bool ChooseAccent(const fs::path& kit, const std::string& accent, std::string& error) {
    std::optional<std::string> digits = accent.empty() ? std::optional<std::string>("")
                                       : accent == kArtworkAccent ? std::optional<std::string>(accent)
                                                                  : NormalizeAccent(accent);
    if (!digits) {
        error = "accent expects RRGGBB or #RRGGBB: " + accent;
        return false;
    }
    return Settings::UpdateFile((kit / "settings.txt").string(), [&digits](Settings& settings) {
        if (digits->empty()) {
            settings.Remove(kAccentKey);
        } else {
            settings.Set(kAccentKey, *digits);
        }
    }, error);
}

const std::vector<AccentPreset>& AccentPresets() {
    static const std::vector<AccentPreset> presets = {
        {"FA2D48", "accent.pink"},  {"FF3B30", "accent.red"},  {"FF9F0A", "accent.orange"},
        {"FFD60A", "accent.yellow"}, {"1ED760", "accent.green"}, {"30B0C7", "accent.teal"},
        {"0A84FF", "accent.blue"},  {"BF5AF2", "accent.purple"}};
    return presets;
}

bool WriteSetting(const fs::path& kit, const std::string& key, const std::string& value, std::string& error) {
    return Settings::UpdateFile((kit / "settings.txt").string(), key, value, error);
}

bool SwitchMod(const fs::path& kit, const std::string& id, bool enabled, std::string& error) {
    if (!IsExtensionTarget(id, error)) return false;
    bool written = Settings::UpdateFile((kit / "settings.txt").string(), [&](Settings& settings) {
        settings.Set(ExtensionHost::SettingKey(id), enabled ? "1" : "0");
        if (!enabled) return;
        // The user switched on an element whose permissions ask for a service: the service goes on with it, so
        // that the element works at once. An element still cannot turn a service on by itself.
        for (const Manifest& manifest : ReadManifests(kit / "extensions", ElementKind::Extension)) {
            if (manifest.id != id) continue;
            for (const std::string& permission : manifest.permissions) {
                if (const char* key = ServiceSettingOf(permission)) settings.Set(key, "1");
            }
        }
    }, error);
    if (!written) return false;
    return AppendReload(kit, (enabled ? "start " : "stop ") + id, error);
}

bool SetModSetting(const fs::path& kit, const std::string& id, const std::string& key, const std::string& value,
                   std::string& error) {
    if (!IsExtensionTarget(id, error) || !IsSettingKey(key, error)) return false;
    if (!WriteSetting(kit, "extensions." + id + "." + key, OneLine(value), error)) return false;
    return AppendReload(kit, "restart " + id, error);
}

std::string ActiveThemeId(const fs::path& kit) {
    Settings settings;
    settings.Load((kit / "settings.txt").string());
    std::string id = settings.GetString("theme.active", "");
    return ToLowerAscii(id) == "none" ? std::string() : id;
}

std::optional<ModEntry> ActiveThemeSettings(const fs::path& kit) {
    Settings settings;
    settings.Load((kit / "settings.txt").string());
    std::string id = ActiveThemeId(kit);
    std::string error;
    if (id.empty() || !IsExtensionTarget(id, error)) return std::nullopt;
    std::optional<std::string> text = ReadWholeFile(kit / "themes" / id / "manifest.json");
    if (!text) return std::nullopt;
    ModEntry theme;
    theme.id = id;
    theme.theme = true;
    theme.settings = DeclaredSettings(*text);
    if (theme.settings.empty()) return std::nullopt;
    std::optional<Json> manifest = Json::Parse(*text);
    const Json* name = manifest ? manifest->Find("name") : nullptr;
    theme.name = name && name->AsString() && !name->AsString()->empty() ? *name->AsString() : id;
    if (const Json* translations = manifest ? manifest->Find("translations") : nullptr) {
        std::vector<std::string> ignored;  // a bad translation leaves the English texts, the card still shows
        theme.translations = ReadManifestTranslations(*translations, ignored);
    }
    for (const SettingSpec& spec : theme.settings) {
        theme.values.push_back(SettingValue(spec, settings.Get("themes." + id + "." + spec.key)));
    }
    return theme;
}

std::vector<ModEntry> EnabledSnippetSettings(const fs::path& kit) {
    Settings settings;
    settings.Load((kit / "settings.txt").string());
    std::vector<ModEntry> snippets;
    for (const Manifest& manifest : ReadManifests(kit / "snippets", ElementKind::Snippet)) {
        if (manifest.settings.empty() || !settings.GetBool("snippets." + manifest.id + ".enabled", false)) continue;
        ModEntry snippet = EntryOf(manifest);
        snippet.snippet = true;
        snippet.enabled = true;
        snippet.settings = manifest.settings;
        for (const SettingSpec& spec : snippet.settings) {
            snippet.values.push_back(SettingValue(spec, settings.Get("snippets." + snippet.id + "." + spec.key)));
        }
        snippets.push_back(std::move(snippet));
    }
    SortByName(snippets);
    return snippets;
}

bool SetSnippetSetting(const fs::path& kit, const std::string& id, const std::string& key, const std::string& value,
                       std::string& error) {
    if (!IsExtensionTarget(id, error)) {
        error = "invalid snippet id: " + id;
        return false;
    }
    return IsSettingKey(key, error) && WriteSetting(kit, "snippets." + id + "." + key, OneLine(value), error);
}

bool SetThemeSetting(const fs::path& kit, const std::string& id, const std::string& key, const std::string& value,
                     std::string& error) {
    if (!IsExtensionTarget(id, error)) {
        error = "invalid theme id: " + id;
        return false;
    }
    return IsSettingKey(key, error) && WriteSetting(kit, "themes." + id + "." + key, OneLine(value), error);
}

namespace {

constexpr size_t kMaxSettingText = 200;  // the length docs/extensions/manifest.md allows a text

struct KindFolder {
    std::string_view kind;
    const char* folder;
    ElementKind element;
};
constexpr KindFolder kSettingKinds[] = {{"extension", "extensions", ElementKind::Extension},
                                        {"theme", "themes", ElementKind::Theme},
                                        {"snippet", "snippets", ElementKind::Snippet}};

const KindFolder* FolderOf(std::string_view kind) {
    for (const KindFolder& entry : kSettingKinds) {
        if (entry.kind == kind) return &entry;
    }
    return nullptr;
}

// The value as settings.txt keeps it when it fits the spec: SettingValue with no default to fall back on.
std::optional<std::string> AcceptedValue(const SettingSpec& spec, const std::string& value) {
    if (spec.type == SettingSpec::Type::Text) {
        bool oneLine = value.find_first_of("\r\n") == std::string::npos;
        return oneLine && value.size() <= kMaxSettingText ? std::optional<std::string>(value) : std::nullopt;
    }
    SettingSpec strict = spec;
    strict.defaultValue.clear();
    std::string accepted = SettingValue(strict, value);
    return accepted.empty() ? std::nullopt : std::optional<std::string>(accepted);
}

// Where settings.txt keeps the element's settings: "themes.glass." for the theme glass.
std::string KeyPrefix(const ModEntry& element) {
    return std::string(element.snippet ? "snippets." : element.theme ? "themes." : "extensions.") + element.id + ".";
}

}  // namespace

std::optional<ModEntry> ElementSettings(const fs::path& kit, std::string_view kind, const std::string& id) {
    const KindFolder* folder = FolderOf(kind);
    std::string error;
    if (!folder || !IsExtensionTarget(id, error)) return std::nullopt;
    for (const Manifest& manifest : ReadManifests(kit / folder->folder, folder->element)) {
        if (manifest.id != id || manifest.settings.empty()) continue;
        Settings settings;
        settings.Load((kit / "settings.txt").string());
        ModEntry entry = EntryOf(manifest);
        entry.theme = folder->element == ElementKind::Theme;
        entry.snippet = folder->element == ElementKind::Snippet;
        entry.settings = manifest.settings;
        std::string prefix = std::string(folder->folder) + "." + id + ".";
        entry.enabled = settings.GetBool(prefix + "enabled", false);
        for (const SettingSpec& spec : entry.settings) {
            entry.values.push_back(SettingValue(spec, settings.Get(prefix + spec.key)));
        }
        return entry;
    }
    return std::nullopt;
}

bool ChangeElementSetting(const fs::path& kit, std::string_view kind, const std::string& id, const std::string& key,
                          const std::string& value, std::string& error) {
    std::optional<ModEntry> element = ElementSettings(kit, kind, id);
    if (!element) {
        error = "no installed " + std::string(kind) + " \"" + id + "\" declares settings";
        return false;
    }
    auto spec = std::find_if(element->settings.begin(), element->settings.end(),
                             [&key](const SettingSpec& candidate) { return candidate.key == key; });
    if (spec == element->settings.end()) {
        error = "\"" + id + "\" has no setting \"" + key + "\"";
        return false;
    }
    std::optional<std::string> accepted = AcceptedValue(*spec, value);
    if (!accepted) {
        error = "\"" + value + "\" does not fit the setting \"" + key + "\" of \"" + id + "\"";
        return false;
    }
    if (element->snippet) return SetSnippetSetting(kit, id, key, *accepted, error);
    if (element->theme) return SetThemeSetting(kit, id, key, *accepted, error);
    return SetModSetting(kit, id, key, *accepted, error);
}

bool ChangeElementValues(const fs::path& kit, std::string_view kind, const std::string& id,
                         const std::map<std::string, std::string>& values, std::string& error) {
    std::optional<ModEntry> element = ElementSettings(kit, kind, id);
    if (!element) {
        error = "no installed " + std::string(kind) + " \"" + id + "\" declares settings";
        return false;
    }
    std::map<std::string, std::string> accepted;
    for (const auto& [key, value] : values) {
        auto spec = std::find_if(element->settings.begin(), element->settings.end(),
                                 [&key](const SettingSpec& candidate) { return candidate.key == key; });
        if (spec == element->settings.end()) {
            error = "\"" + id + "\" has no setting \"" + key + "\"";
            return false;
        }
        std::optional<std::string> fit = AcceptedValue(*spec, value);
        if (!fit) {
            error = "\"" + value + "\" does not fit the setting \"" + key + "\" of \"" + id + "\"";
            return false;
        }
        accepted[key] = *fit;
    }
    // One write for all of them, so an extension restarts once and a theme applies once.
    std::string prefix = KeyPrefix(*element);
    bool written = Settings::UpdateFile((kit / "settings.txt").string(), [&](Settings& settings) {
        for (const auto& [key, value] : accepted) settings.Set(prefix + key, OneLine(value));
    }, error);
    if (!written || element->theme || element->snippet) return written;
    return AppendReload(kit, "restart " + id, error);
}

bool ResetElementValues(const fs::path& kit, std::string_view kind, const std::string& id,
                        const std::vector<std::string>& keys, std::string& error) {
    std::optional<ModEntry> element = ElementSettings(kit, kind, id);
    if (!element) {
        error = "no installed " + std::string(kind) + " \"" + id + "\" declares settings";
        return false;
    }
    std::vector<std::string> removed = keys;
    if (removed.empty()) {
        for (const SettingSpec& spec : element->settings) removed.push_back(spec.key);
    }
    for (const std::string& key : removed) {
        bool declared = std::any_of(element->settings.begin(), element->settings.end(),
                                    [&key](const SettingSpec& spec) { return spec.key == key; });
        if (!declared) {
            error = "\"" + id + "\" has no setting \"" + key + "\"";
            return false;
        }
    }
    std::string prefix = KeyPrefix(*element);
    bool written = Settings::UpdateFile((kit / "settings.txt").string(), [&](Settings& settings) {
        for (const std::string& key : removed) settings.Remove(prefix + key);
    }, error);
    if (!written || element->theme || element->snippet) return written;
    return AppendReload(kit, "restart " + id, error);
}

}  // namespace ampify::uirender
