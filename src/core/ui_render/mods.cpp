#include "mods.hpp"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <sstream>

#include "../extension.hpp"
#include "../json.hpp"
#include "../manifest.hpp"
#include "../reload.hpp"
#include "../settings.hpp"
#include "../text_util.hpp"

namespace ampify::uirender {

namespace fs = std::filesystem;

namespace {

constexpr int kKitApiMajor = 1;                     // the same as ExtensionLoader
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
        mod.enabled = settings.GetBool(ExtensionHost::SettingKey(mod.id), false);
        mod.settings = manifest.settings;
        for (const SettingSpec& spec : mod.settings) {
            mod.values.push_back(settings.GetString("extensions." + mod.id + "." + spec.key, spec.defaultValue));
        }
        mods.push_back(std::move(mod));
    }
    SortByName(mods);
    return mods;
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

bool WriteSetting(const fs::path& kit, const std::string& key, const std::string& value, std::string& error) {
    return Settings::UpdateFile((kit / "settings.txt").string(), key, value, error);
}

bool SwitchMod(const fs::path& kit, const std::string& id, bool enabled, std::string& error) {
    if (!IsExtensionTarget(id, error)) return false;
    if (!WriteSetting(kit, ExtensionHost::SettingKey(id), enabled ? "1" : "0", error)) return false;
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
    for (const SettingSpec& spec : theme.settings) {
        theme.values.push_back(SettingValue(spec, settings.Get("themes." + id + "." + spec.key)));
    }
    return theme;
}

bool SetThemeSetting(const fs::path& kit, const std::string& id, const std::string& key, const std::string& value,
                     std::string& error) {
    if (!IsExtensionTarget(id, error)) {
        error = "invalid theme id: " + id;
        return false;
    }
    return IsSettingKey(key, error) && WriteSetting(kit, "themes." + id + "." + key, OneLine(value), error);
}

}  // namespace ampify::uirender
