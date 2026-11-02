#include "extension_loader.hpp"

#include <algorithm>
#include <fstream>
#include <mutex>
#include <sstream>
#include <system_error>

#include "kit_layout.hpp"
#include "manifest.hpp"
#include "text_util.hpp"

namespace ampify {

namespace {

namespace fs = std::filesystem;

constexpr int kSupportedKitApiMajor = 1;
constexpr std::string_view kAppTargetPrefix = "app:";

// A theme or a snippet with no script: `ampify dev` asks for its restart like any element, and the theme service, not
// the loader, reloads its style.
bool IsStyleFolder(const fs::path& themeRoot, const std::string& target) {
    std::error_code ec;
    return !themeRoot.empty() && (fs::is_directory(themeRoot / target, ec) ||
                                  fs::is_directory(themeRoot.parent_path() / "snippets" / target, ec));
}

}  // namespace

ExtensionLoader::ExtensionLoader(ExtensionHost& hosts, Host& host, Journal& journal, fs::path root, Clock clock)
    : m_hosts(hosts), m_host(host), m_journal(journal), m_root(std::move(root)), m_clock(std::move(clock)) {}

// A theme script runs while its theme is the active one, and while theme.parts.script is on. The keys are those of
// src/core/theme/theme_settings.cpp; the theme library is not part of the kernel's core, so they are read here.
bool ThemeScriptEnabled(const Settings& settings, const std::string& id) {
    return settings.GetString("theme.active", "") == id && settings.GetBool("theme.parts.script", true);
}

bool ScriptsAllowed(const Settings& settings) { return settings.GetBool(std::string(kExposeApiKey), true); }

void ExtensionLoader::SetAppRoot(fs::path root) { m_appRoot = std::move(root); }

void ExtensionLoader::SetThemeRoot(fs::path root) { m_themeRoot = std::move(root); }

std::string ExtensionLoader::HostId(const Manifest& manifest) {
    return manifest.kind == ElementKind::App ? std::string(kAppTargetPrefix) + manifest.id : manifest.id;
}

LoadReport ExtensionLoader::Discover() {
    LoadReport report;
    LoadFolders(m_root, Origin::Extensions, report);
    if (!m_appRoot.empty()) LoadFolders(m_appRoot, Origin::Apps, report);
    if (!m_themeRoot.empty()) LoadFolders(m_themeRoot, Origin::Themes, report);
    return report;
}

void ExtensionLoader::LoadFolders(const fs::path& root, Origin origin, LoadReport& report) {
    std::error_code ec;
    if (!fs::is_directory(root, ec)) return;

    std::vector<fs::path> folders;
    for (const fs::directory_entry& entry : fs::directory_iterator(root, ec)) {
        std::string name = entry.path().filename().string();
        if (name.empty() || name[0] == '.') continue;  // work in progress of ampify (install, replacement)
        if (entry.is_directory(ec)) folders.push_back(entry.path());
    }
    std::sort(folders.begin(), folders.end());

    for (const fs::path& folder : folders) {
        // Already loaded from this same folder: a rescan leaves it as it is.
        std::string name = folder.filename().string();
        std::string hostId = origin == Origin::Apps ? std::string(kAppTargetPrefix) + name : name;
        if (ScriptExtension* known = Find(hostId); known && FolderOf(*known) == folder) continue;
        LoadFolder(folder, origin, report);
    }
}

void ExtensionLoader::LoadFolder(const fs::path& folder, Origin origin, LoadReport& report) {
    std::string name = folder.filename().string();
    std::optional<std::string> text = ReadWholeFile(folder / "manifest.json");
    if (!text) return;  // a folder without a manifest is not an element

    ManifestResult result = ParseManifest(*text, name, kSupportedKitApiMajor, [&](const std::string& file) {
        std::error_code fileError;
        return fs::is_regular_file(folder / file, fileError);
    });
    for (const std::string& warning : result.warnings) m_journal.Write("[" + name + "] warning: " + warning);
    if (!result.manifest) {
        for (const std::string& error : result.errors) m_journal.Write("[" + name + "] rejected: " + error);
        report.rejected.push_back(name);
        return;
    }

    Manifest manifest = std::move(*result.manifest);
    if (origin == Origin::Themes) {
        // A theme without a main.js has no script: its folder belongs to the theme engine. Its script runs in
        // AppleMusic.exe only, because the interface is the only thing a theme changes.
        if (manifest.kind != ElementKind::Theme) {
            m_journal.Write("[" + name + "] rejected: the themes folder expects a theme");
            report.rejected.push_back(name);
            return;
        }
        if (!manifest.main || m_host.Process() != "ui") return;
    } else {
        // Each kind has its own folder, where ampify installs it: extensions/<id> for an extension, apps/<id> for an app.
        ElementKind expected = origin == Origin::Apps ? ElementKind::App : ElementKind::Extension;
        if (manifest.kind != expected) {
            m_journal.Write("[" + name + "] type " + std::string(ToString(manifest.kind)) +
                            " not loaded from the folder " + (origin == Origin::Apps ? "apps" : "extensions"));
            report.rejected.push_back(name);
            return;
        }
    }

    if (!manifest.RunsIn(m_host.Process())) {
        m_journal.Write("[" + name + "] skipped in the process " + m_host.Process());
        report.skipped.push_back(name);
        return;
    }

    std::string id = manifest.id;
    std::string hostId = HostId(manifest);
    std::string displayName = manifest.name;
    bool isApp = manifest.kind == ElementKind::App;
    auto element = std::make_unique<ScriptExtension>(std::move(manifest), folder, m_host, m_journal, m_clock);
    ScriptExtension* raw = element.get();
    ExtensionInfo info;
    info.id = hostId;
    info.name = displayName;
    // A community element never starts unless the user enables it; one that ships with the kit starts until the user
    // turns it off.
    bool byDefault = origin != Origin::Themes && IsBundledElement(isApp ? "apps" : "extensions", id);
    // expose_api = 0 keeps every JavaScript element off. An app is switched by apps.<id>.enabled, as `ampify app
    // enable` writes it; an extension by extensions.<id>.enabled.
    if (origin == Origin::Themes) {
        info.isEnabled = [id](const Settings& settings) {
            return ScriptsAllowed(settings) && ThemeScriptEnabled(settings, id);
        };
    } else {
        std::string key = isApp ? "apps." + id + ".enabled" : ExtensionHost::SettingKey(id);
        info.isEnabled = [key, byDefault](const Settings& settings) {
            return ScriptsAllowed(settings) && settings.GetBool(key, byDefault);
        };
    }
    info.start = [raw] { return raw->Start(); };
    info.stop = [raw] { raw->Stop(); };

    if (!m_hosts.Add(std::move(info))) {
        m_journal.Write("[" + name + "] rejected: id already in use");
        report.rejected.push_back(name);
        return;
    }
    {
        std::lock_guard<std::mutex> lock(m_elementsMutex);
        m_elements.push_back(std::move(element));
    }
    report.loaded.push_back(hostId);
}

ScriptExtension* ExtensionLoader::Find(const std::string& hostId) const {
    for (const auto& element : m_elements) {
        if (HostId(element->manifest()) == hostId) return element.get();
    }
    return nullptr;
}

ExtensionLoader::Origin ExtensionLoader::OriginOf(const ScriptExtension& element) const {
    switch (element.manifest().kind) {
        case ElementKind::App: return Origin::Apps;
        case ElementKind::Theme: return Origin::Themes;
        default: return Origin::Extensions;
    }
}

fs::path ExtensionLoader::FolderOf(const ScriptExtension& element) const {
    switch (OriginOf(element)) {
        case Origin::Apps: return m_appRoot / element.id();
        case Origin::Themes: return m_themeRoot / element.id();
        case Origin::Extensions: break;
    }
    return m_root / element.id();
}

void ExtensionLoader::Forget(const std::string& hostId) {
    m_hosts.Remove(hostId);  // stops it first, which releases what it holds in the host
    std::lock_guard<std::mutex> lock(m_elementsMutex);
    m_elements.erase(std::remove_if(m_elements.begin(), m_elements.end(),
                                    [&](const auto& element) { return HostId(element->manifest()) == hostId; }),
                     m_elements.end());
}

bool ExtensionLoader::Start(const std::string& target, const Settings& settings) {
    if (!m_hosts.Has(target)) Rescan(settings);  // installed or enabled since the start: load it first
    return m_hosts.Start(target, settings);
}

bool ExtensionLoader::Stop(const std::string& target) { return m_hosts.Stop(target); }

bool ExtensionLoader::Restart(const std::string& target, const Settings& settings) {
    ScriptExtension* element = Find(target);
    if (!element) {
        // A native extension restarts in place. An element the kernel has not seen yet (copied by ampify dev, installed
        // since the start) is found by a rescan, which starts it when the settings enable it.
        if (m_hosts.Has(target)) return m_hosts.Restart(target, settings);
        Rescan(settings);
        return Find(target) != nullptr;
    }

    // The folder is read again, so a new manifest (permissions, processes, type) counts as much as new code.
    fs::path folder = FolderOf(*element);
    Origin origin = OriginOf(*element);
    Forget(target);
    LoadReport report;
    LoadFolder(folder, origin, report);
    if (!Find(target)) {
        m_journal.Write("[" + target + "] cannot reload: the element is no longer valid, it stays stopped");
        return true;
    }
    m_hosts.Start(target, settings);
    return true;
}

LoadReport ExtensionLoader::Rescan(const Settings& settings) {
    std::vector<std::string> gone;
    for (const auto& element : m_elements) {
        std::error_code ec;
        if (!fs::is_regular_file(FolderOf(*element) / "manifest.json", ec)) gone.push_back(HostId(element->manifest()));
    }
    for (const std::string& hostId : gone) {
        m_journal.Write("[" + hostId + "] removed: its folder no longer exists");
        Forget(hostId);
    }

    LoadReport report = Discover();
    for (const std::string& hostId : report.loaded) m_hosts.Start(hostId, settings);
    return report;
}

void ExtensionLoader::Apply(const std::vector<ReloadRequest>& requests, const Settings& settings) {
    for (const ReloadRequest& request : requests) {
        switch (request.action) {
            case ReloadRequest::Action::Start:
                m_journal.Write("Start requested: " + request.target);
                if (!Start(request.target, settings)) m_journal.Write("[" + request.target + "] not found in Ampify");
                break;
            case ReloadRequest::Action::Stop:
                m_journal.Write("Stop requested: " + request.target);
                Stop(request.target);
                break;
            case ReloadRequest::Action::Restart:
                m_journal.Write("Reload requested: " + request.target);
                if (!Restart(request.target, settings) && !IsStyleFolder(m_themeRoot, request.target)) {
                    m_journal.Write("[" + request.target + "] not found in Ampify");
                }
                break;
            case ReloadRequest::Action::Rescan: {
                m_journal.Write("Rescan of the Ampify folders requested");
                LoadReport report = Rescan(settings);
                m_journal.Write("New elements: " + std::to_string(report.loaded.size()));
                break;
            }
        }
    }
}

void ExtensionLoader::Pump() {
    for (auto& element : m_elements) element->Pump();
}

void ExtensionLoader::DeliverPlayer(const std::string& event, const PlayerSnapshot& snapshot) {
    std::lock_guard<std::mutex> lock(m_elementsMutex);
    for (auto& element : m_elements) {
        if (element->running()) element->DeliverPlayer(event, snapshot);
    }
}

void ExtensionLoader::DeliverKey(const std::string& elementId, const std::string& combo) {
    std::lock_guard<std::mutex> lock(m_elementsMutex);
    for (auto& element : m_elements) {
        if (element->id() == elementId && element->running()) element->DeliverKey(combo);
    }
}

void ExtensionLoader::DeliverClick(int controlId) {
    std::optional<UiControl> control = m_host.Ui().Find(controlId);
    if (!control) return;
    std::lock_guard<std::mutex> lock(m_elementsMutex);
    for (auto& element : m_elements) {
        if (element->id() == control->elementId && element->running()) element->DeliverClick(controlId);
    }
}

void ExtensionLoader::DeliverEvent(const std::string& entry, const std::string& payload) {
    std::lock_guard<std::mutex> lock(m_elementsMutex);
    for (auto& element : m_elements) {
        if (element->running()) element->DeliverEvent(entry, payload);
    }
}

void ExtensionLoader::DeliverClickTo(const std::string& elementId, int callbackId) {
    std::lock_guard<std::mutex> lock(m_elementsMutex);
    for (auto& element : m_elements) {
        if (element->id() == elementId && element->running()) element->DeliverClick(callbackId);
    }
}

void ExtensionLoader::DeliverEventTo(const std::string& elementId, const std::string& entry, const std::string& payload) {
    std::lock_guard<std::mutex> lock(m_elementsMutex);
    for (auto& element : m_elements) {
        if (element->id() == elementId && element->running()) element->DeliverEvent(entry, payload);
    }
}

}  // namespace ampify
