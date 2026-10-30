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

LoadReport ExtensionLoader::Discover() {
    LoadReport report;
    std::error_code ec;
    if (!fs::is_directory(m_root, ec)) return report;

    std::vector<fs::path> folders;
    for (const fs::directory_entry& entry : fs::directory_iterator(m_root, ec)) {
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
        if (manifest.kind != ElementKind::Extension && manifest.kind != ElementKind::App) {
            m_journal.Write("[" + name + "] type " + std::string(ToString(manifest.kind)) +
                            " non chargé par cette version du noyau");
            report.rejected.push_back(name);
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
    return report;
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

}  // namespace amk
