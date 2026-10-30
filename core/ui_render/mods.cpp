#include "mods.hpp"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <sstream>

#include "../extension.hpp"
#include "../manifest.hpp"
#include "../reload.hpp"
#include "../settings.hpp"

namespace amk::uirender {

namespace fs = std::filesystem;

namespace {

constexpr int kKitApiMajor = 1;                     // the same as ExtensionLoader
constexpr std::uintmax_t kReloadLimit = 64 * 1024;  // rewritten past this size, as amk does

bool ReadText(const fs::path& file, std::string& text) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return false;
    std::ostringstream buffer;
    buffer << in.rdbuf();
    text = buffer.str();
    return true;
}

}  // namespace

std::vector<ModEntry> ListMods(const fs::path& kit) {
    std::vector<ModEntry> mods;
    Settings settings;
    settings.Load((kit / "settings.txt").string());

    std::error_code ec;
    fs::path root = kit / "extensions";
    for (const fs::directory_entry& entry : fs::directory_iterator(root, ec)) {
        std::string folder = entry.path().filename().string();
        if (folder.empty() || folder[0] == '.' || !entry.is_directory(ec)) continue;
        std::string text;
        if (!ReadText(entry.path() / "manifest.json", text)) continue;
        ManifestResult result = ParseManifest(text, folder, kKitApiMajor, [&](const std::string& file) {
            std::error_code fileError;
            return fs::is_regular_file(entry.path() / file, fileError);
        });
        if (!result.manifest || result.manifest->kind != ElementKind::Extension) continue;
        ModEntry mod;
        mod.id = result.manifest->id;
        mod.name = result.manifest->name.empty() ? mod.id : result.manifest->name;
        mod.description = result.manifest->description;
        mod.enabled = settings.GetBool(ExtensionHost::SettingKey(mod.id), false);
        mods.push_back(std::move(mod));
    }
    std::sort(mods.begin(), mods.end(), [](const ModEntry& a, const ModEntry& b) {
        return a.name == b.name ? a.id < b.id : a.name < b.name;
    });
    return mods;
}

bool SwitchMod(const fs::path& kit, const std::string& id, bool enabled, std::string& error) {
    if (!IsReloadTarget(id) || id.rfind("app:", 0) == 0) {
        error = "identifiant d'extension invalide : " + id;
        return false;
    }
    Settings settings;
    std::string settingsPath = (kit / "settings.txt").string();
    settings.Load(settingsPath);
    settings.SetBool(ExtensionHost::SettingKey(id), enabled);
    if (!settings.Save(settingsPath)) {
        error = "écriture impossible : " + settingsPath;
        return false;
    }

    // One write per call, so the kernel sees a whole line; a file that grew too big starts again.
    fs::path reload = kit / "reload.txt";
    std::error_code ec;
    std::uintmax_t size = fs::file_size(reload, ec);
    std::ios::openmode mode = std::ios::binary | (ec || size > kReloadLimit ? std::ios::trunc : std::ios::app);
    std::ofstream out(reload, mode);
    out << (enabled ? "start " : "stop ") << id << "\n";
    out.flush();
    if (!out) {
        error = "écriture impossible : " + reload.string();
        return false;
    }
    return true;
}

}  // namespace amk::uirender
