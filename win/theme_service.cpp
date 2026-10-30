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
#include "../core/theme/css.hpp"
#include "../core/theme/inspector.hpp"
#include "../core/theme/layout.hpp"
#include "../core/theme/library.hpp"
#include "../core/theme/session.hpp"
#include "../core/theme/theme_settings.hpp"
#include "../core/theme/worker.hpp"

namespace amk::win {

namespace {

constexpr auto kTick = std::chrono::seconds(3);
constexpr auto kAttachRetry = std::chrono::seconds(1);

// Windows apps in dark mode, for theme.scheme = auto. Light when the value cannot be read (older Windows).
bool SystemUsesDarkMode() {
    DWORD light = 1;
    DWORD size = sizeof(light);
    LONG status = RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                               L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &light, &size);
    return status == ERROR_SUCCESS && light == 0;
}

// Local time in minutes from midnight, for theme.schedule.
int LocalMinute() {
    SYSTEMTIME now{};
    GetLocalTime(&now);
    return now.wHour * 60 + now.wMinute;
}

// After an inspection, writes sidebar.json when it does not exist yet: every NavigationViewItem of the tree, in the
// current order, none hidden. The user then edits it (docs/themes/FORMAT.md, « Barre latérale et accueil »).
void WriteSidebarIfMissing(const std::filesystem::path& kit, theme::TreeBackend& tree) {
    std::filesystem::path file = kit / std::string(theme::LayoutFileName(theme::LayoutKind::Sidebar));
    std::error_code error;
    if (std::filesystem::exists(file, error)) return;
    std::optional<theme::Selector> items = theme::ParseSelector("NavigationViewItem");
    if (!items) return;
    std::vector<std::string> names = theme::NamesMatching(tree.Snapshot(), *items);
    if (names.empty()) return;
    std::ofstream(file, std::ios::binary) << theme::WriteLayout(theme::GenerateLayout(std::nullopt, names));
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

    // Reads the settings, the theme, the snippets, sidebar.json and home.json, and reloads only what changed. The
    // Windows mode and the local time choose the scheme for theme.scheme = auto and theme.schedule.
    theme::ThemeWorker worker(m_kit);

    while (m_running) {
        Settings settings;
        settings.Load((m_kit / "settings.txt").string());
        worker.SetSystemDark(SystemUsesDarkMode());
        theme::SessionRequest request = worker.Tick(settings, LocalMinute());
        std::string activeId = request.themeId;

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
                std::ofstream(JournalFile("inspector.json")) << theme::InspectTreeJson(ui->backend);
                WriteSidebarIfMissing(kit, ui->backend);
                UpdateThemeSettings(kit, [](theme::ThemeSettings& value) { value.inspect = false; });
                journal.Write("Inspecteur : arbre écrit dans journal\\inspector.txt et inspector.json");
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
