#include "mods.hpp"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <sstream>

#include "../extension.hpp"
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

    std::error_code ec;
    fs::path root = kit / "extensions";
    for (const fs::directory_entry& entry : fs::directory_iterator(root, ec)) {
        std::string folder = entry.path().filename().string();
        if (folder.empty() || folder[0] == '.' || !entry.is_directory(ec)) continue;
        std::optional<std::string> text = ReadWholeFile(entry.path() / "manifest.json");
        if (!text) continue;
        ManifestResult result = ParseManifest(*text, folder, kKitApiMajor, [&](const std::string& file) {
            std::error_code fileError;
            return fs::is_regular_file(entry.path() / file, fileError);
        });
        if (!result.manifest || result.manifest->kind != ElementKind::Extension) continue;
        ModEntry mod;
        mod.id = result.manifest->id;
        mod.name = result.manifest->name.empty() ? mod.id : result.manifest->name;
        mod.description = result.manifest->description;
        mod.translations = result.manifest->translations;
        mod.enabled = settings.GetBool(ExtensionHost::SettingKey(mod.id), false);
        mod.settings = result.manifest->settings;
        for (const SettingSpec& spec : mod.settings) {
            mod.values.push_back(settings.GetString("extensions." + mod.id + "." + spec.key, spec.defaultValue));
        }
        mods.push_back(std::move(mod));
    }
    std::sort(mods.begin(), mods.end(), [](const ModEntry& a, const ModEntry& b) {
        return a.name == b.name ? a.id < b.id : a.name < b.name;
    });
    return mods;
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
    if (!IsExtensionTarget(id, error)) return false;
    bool validKey = !key.empty() && key != "enabled" && std::all_of(key.begin(), key.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
    });
    if (!validKey) {
        error = "invalid setting key: " + key;
        return false;
    }
    std::string line = value;
    std::replace_if(line.begin(), line.end(), [](char c) { return c == '\r' || c == '\n'; }, ' ');
    if (!WriteSetting(kit, "extensions." + id + "." + key, line, error)) return false;
    return AppendReload(kit, "restart " + id, error);
}

}  // namespace ampify::uirender
