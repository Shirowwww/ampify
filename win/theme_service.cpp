// NOT COMPILED IN THE CURRENT BUILD: see xaml_backend.cpp. Only the file reads and the posts are plain Win32 and C++.

#if defined(AMK_WITH_XAML)

#include "theme_service.hpp"

#include <chrono>
#include <fstream>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "ui_thread.hpp"
#include "win_common.hpp"
#include "xaml_backend.hpp"
#include "../core/settings.hpp"
#include "../core/theme/files.hpp"
#include "../core/theme/library.hpp"
#include "../core/theme/session.hpp"
#include "../core/theme/theme.hpp"
#include "../core/theme/theme_settings.hpp"

namespace amk::win {

namespace {

constexpr auto kTick = std::chrono::seconds(3);
constexpr auto kAttachRetry = std::chrono::seconds(1);

// The folder of one theme is `themes\<name>` under the kit. The file of allowed resource keys is written by the
// inspector on the PC. Without it, resources.xaml is loaded without the key check, and the journal says so.
std::optional<std::set<std::string>> AllowedKeys(const std::filesystem::path& kit) {
    std::ifstream in(kit / "resource-keys.txt");
    if (!in) return std::nullopt;

    std::set<std::string> keys;
    std::string line;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (!line.empty() && line[0] != '#') keys.insert(line);
    }
    return keys;
}

// A change of part is a change of theme: the parts are part of the key that triggers a reload.
std::string PartsKey(const theme::ThemeParts& parts) {
    return std::string("|") + (parts.colors ? "c" : "-") + (parts.resources ? "r" : "-") + (parts.assets ? "a" : "-");
}

// Changes only the theme keys of settings.txt, and keeps the rest of the file as it is.
void UpdateThemeSettings(const std::filesystem::path& kit, const std::function<void(theme::ThemeSettings&)>& change) {
    Settings settings;
    settings.Load((kit / "settings.txt").string());
    theme::ThemeSettings value = theme::ReadThemeSettings(settings);
    change(value);
    theme::WriteThemeSettings(settings, value);
    settings.Save((kit / "settings.txt").string());
}

}  // namespace

// Owned by the service, touched only by the posted tasks, which run on the interface thread.
struct UiSide {
    UiSide(Journal& journal) : session(journal) {}

    XamlTreeBackend backend;
    theme::ThemeSession session;
    bool attached = false;
};

ThemeService::ThemeService(Journal& journal, std::filesystem::path kit)
    : m_journal(journal), m_kit(std::move(kit)), m_ui(std::make_shared<UiSide>(journal)) {}

ThemeService::~ThemeService() {
    Stop();
}

void ThemeService::Start(HINSTANCE module, DWORD processId) {
    if (m_running.exchange(true)) return;
    m_worker = std::thread([this, module, processId] { Run(module, processId); });
}

void ThemeService::Stop() {
    m_running = false;
    if (m_worker.joinable()) m_worker.join();
    DetachUiThread();
}

void ThemeService::Run(HINSTANCE module, DWORD processId) {
    while (m_running && !AttachUiThread(processId, module)) std::this_thread::sleep_for(kAttachRetry);
    if (!m_running) return;
    m_journal.Write("Thèmes : crochet installé sur le thread d'interface");

    std::string lastKey;
    std::uint64_t generation = 0;
    std::shared_ptr<const theme::Theme> loaded;
    std::string activeId;

    while (m_running) {
        Settings settings;
        settings.Load((m_kit / "settings.txt").string());
        theme::ThemeSettings wanted = theme::ReadThemeSettings(settings);

        // A new key means a new generation: the interface restores the old theme and applies the new one.
        std::string key = wanted.active.empty() ? "none" : wanted.active + "|" + wanted.scheme + PartsKey(wanted.parts);
        if (!wanted.active.empty() && wanted.liveReload) key += "|" + theme::FolderSignature(m_kit / "themes" / wanted.active);
        if (key != lastKey) {
            lastKey = key;
            generation++;
            loaded.reset();
            activeId = wanted.active;
            if (!wanted.active.empty()) {
                std::filesystem::path folder = m_kit / "themes" / wanted.active;
                theme::FolderFiles files(folder);
                theme::LoadOptions options;
                options.scheme = wanted.scheme;
                options.parts = wanted.parts;
                std::optional<std::set<std::string>> allowed = AllowedKeys(m_kit);
                options.allowedKeys = allowed ? &*allowed : nullptr;
                loaded = std::make_shared<const theme::Theme>(
                    theme::LoadTheme(wanted.active, folder.generic_string(), files, options));
            }
        }

        theme::SessionRequest request;
        request.themeId = activeId;
        request.theme = loaded;
        request.generation = generation;
        request.inspect = wanted.inspect;

        std::vector<std::string> names{""};  // "" is the choice "Aucun"
        for (const std::string& folder : theme::ListThemeFolders(m_kit / "themes")) names.push_back(folder);

        std::shared_ptr<UiSide> ui = m_ui;
        Journal& journal = m_journal;
        std::filesystem::path kit = m_kit;
        PostToUiThread([ui, request, names, activeId, kit, &journal]() {
            if (!ui->attached) {
                ui->attached = ui->backend.Attach();
                if (!ui->attached) return;
            }

            std::optional<std::string> dump = ui->session.Tick(ui->backend, request);
            if (dump) {
                std::ofstream(JournalFile("inspector.txt")) << *dump;
                UpdateThemeSettings(kit, [](theme::ThemeSettings& value) { value.inspect = false; });
                journal.Write("Inspecteur : arbre écrit dans journal\\inspector.txt");
            }

            ui->backend.EnsureSettingsSection(names, activeId, [kit, activeId, &journal](const std::string& choice) {
                if (choice == activeId) return;  // the combo box reports its first selection too
                UpdateThemeSettings(kit, [&choice](theme::ThemeSettings& value) { value.active = choice; });
                journal.Write("Thème choisi dans Réglages : « " + (choice.empty() ? std::string("aucun") : choice) + " »");
            });
        });

        std::this_thread::sleep_for(kTick);
    }
}

}  // namespace amk::win

#endif  // AMK_WITH_XAML
