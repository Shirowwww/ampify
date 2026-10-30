#include "extension_loader.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <system_error>

#include "manifest.hpp"

namespace amk {

namespace {

namespace fs = std::filesystem;

constexpr int kSupportedKitApiMajor = 1;

std::optional<std::string> ReadText(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return std::nullopt;
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

}  // namespace

ExtensionLoader::ExtensionLoader(ExtensionHost& hosts, Host& host, Journal& journal, fs::path root, Clock clock)
    : m_hosts(hosts), m_host(host), m_journal(journal), m_root(std::move(root)), m_clock(std::move(clock)) {}

// A theme script runs while its theme is the active one, and while theme.parts.script is on. The keys are those of
// core/theme/theme_settings.cpp; the theme library is not part of the kernel's core, so they are read here.
bool ThemeScriptEnabled(const Settings& settings, const std::string& id) {
    return settings.GetString("theme.active", "") == id && settings.GetBool("theme.parts.script", true);
}

void ExtensionLoader::SetThemeRoot(fs::path root) { m_themeRoot = std::move(root); }

LoadReport ExtensionLoader::Discover() {
    LoadReport report;
    LoadFolders(m_root, report, false);
    if (!m_themeRoot.empty()) LoadFolders(m_themeRoot, report, true);
    return report;
}

void ExtensionLoader::LoadFolders(const fs::path& root, LoadReport& report, bool themes) {
    std::error_code ec;
    if (!fs::is_directory(root, ec)) return;

    std::vector<fs::path> folders;
    for (const fs::directory_entry& entry : fs::directory_iterator(root, ec)) {
        if (entry.is_directory(ec)) folders.push_back(entry.path());
    }
    std::sort(folders.begin(), folders.end());

    for (const fs::path& folder : folders) {
        std::string name = folder.filename().string();
        std::optional<std::string> text = ReadText(folder / "manifest.json");
        if (!text) continue;  // a folder without a manifest is not an element

        ManifestResult result = ParseManifest(*text, name, kSupportedKitApiMajor, [&](const std::string& file) {
            std::error_code fileError;
            return fs::is_regular_file(folder / file, fileError);
        });
        for (const std::string& warning : result.warnings) m_journal.Write("[" + name + "] avertissement : " + warning);
        if (!result.manifest) {
            for (const std::string& error : result.errors) m_journal.Write("[" + name + "] rejeté : " + error);
            report.rejected.push_back(name);
            continue;
        }

        Manifest manifest = std::move(*result.manifest);
        if (themes) {
            // A theme without a main.js has no script: its folder belongs to the theme engine. Its script runs in
            // AppleMusic.exe only, because the interface is the only thing a theme changes.
            if (manifest.kind != ElementKind::Theme) {
                m_journal.Write("[" + name + "] rejeté : le dossier des thèmes attend un thème");
                report.rejected.push_back(name);
                continue;
            }
            if (!manifest.main || m_host.Process() != "ui") continue;
        } else if (manifest.kind != ElementKind::Extension && manifest.kind != ElementKind::App) {
            m_journal.Write("[" + name + "] type " + std::string(ToString(manifest.kind)) +
                            " non chargé par cette version du noyau");
            report.rejected.push_back(name);
            continue;
        }

        if (!manifest.RunsIn(m_host.Process())) {
            m_journal.Write("[" + name + "] ignoré dans le processus " + m_host.Process());
            report.skipped.push_back(name);
            continue;
        }

        std::string id = manifest.id;
        std::string displayName = manifest.name;
        auto element = std::make_unique<ScriptExtension>(std::move(manifest), folder, m_host, m_journal, m_clock);
        ScriptExtension* raw = element.get();
        ExtensionInfo info;
        info.id = id;
        info.name = displayName;
        info.enabledByDefault = false;  // a community element never starts unless the user enables it
        if (themes) info.isEnabled = [id](const Settings& settings) { return ThemeScriptEnabled(settings, id); };
        info.start = [raw] { return raw->Start(); };
        info.stop = [raw] { raw->Stop(); };

        if (!m_hosts.Add(std::move(info))) {
            m_journal.Write("[" + name + "] rejeté : identifiant déjà utilisé");
            report.rejected.push_back(name);
            continue;
        }
        m_elements.push_back(std::move(element));
        report.loaded.push_back(id);
    }
}

bool ExtensionLoader::Restart(const std::string& id, const Settings& settings) {
    return m_hosts.Restart(id, settings);
}

void ExtensionLoader::Pump() {
    for (auto& element : m_elements) element->Pump();
}

void ExtensionLoader::DeliverPlayer(const std::string& event, const PlayerSnapshot& snapshot) {
    for (auto& element : m_elements) {
        if (element->running()) element->DeliverPlayer(event, snapshot);
    }
}

void ExtensionLoader::DeliverKey(const std::string& elementId, const std::string& combo) {
    for (auto& element : m_elements) {
        if (element->id() == elementId && element->running()) element->DeliverKey(combo);
    }
}

void ExtensionLoader::DeliverClick(int controlId) {
    std::optional<UiControl> control = m_host.Ui().Find(controlId);
    if (!control) return;
    for (auto& element : m_elements) {
        if (element->id() == control->elementId && element->running()) element->DeliverClick(controlId);
    }
}

}  // namespace amk
