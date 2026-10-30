// amk_setup.exe: installs the kit and uninstalls it. The same file is copied into the install folder as the uninstaller.
//
//   amk_setup.exe                          install, after a confirmation
//   amk_setup.exe /silent                  install without any dialog (tests, scripts)
//   amk_setup.exe /uninstall               uninstall, asks whether to keep the user's data
//   amk_setup.exe /uninstall /keepdata     uninstall and keep settings, journals and reports
//   amk_setup.exe /uninstall /purgedata    uninstall and delete the user's data folder too
//   add /silent to any uninstall to skip the dialogs (without a data choice, the data is kept)
//
// Exit codes: 0 done (or cancelled by the user), 1 error, 3 refused because Apple Music or the launcher is open.
//
// The kit never writes into Apple Music's package folder. Installing only copies three files, adds a start menu
// shortcut and an uninstall entry. Uninstalling removes exactly those, so Apple Music starts as it did before.

#include <windows.h>
#include <objbase.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shellapi.h>

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#include "../core/apple_music.hpp"
#include "../core/journal.hpp"
#include "../core/kit_layout.hpp"
#include "../win/package.hpp"
#include "../win/win_common.hpp"

namespace fs = std::filesystem;

namespace {

constexpr int kExitDone = 0;
constexpr int kExitError = 1;
constexpr int kExitRefused = 3;

constexpr std::wstring_view kShortcutDescription = L"Lance Apple Music avec le Mod Kit";

bool HasFlag(int argc, wchar_t** argv, const wchar_t* flag) {
    for (int i = 1; i < argc; i++) {
        if (_wcsicmp(argv[i], flag) == 0) return true;
    }
    return false;
}

// Writes the journal to a file. An empty path keeps it in memory: used when the data folder is about to be deleted,
// so the log does not survive in a file of its own.
class Log {
public:
    explicit Log(const fs::path& file)
        : m_file(file, std::ios::trunc),
          m_out(file.empty() ? static_cast<std::ostream*>(&m_memory) : &m_file),
          m_journal(*m_out, amk::Journal::LocalTimeStamp) {}

    void Write(const std::string& line) { m_journal.Write(line); }

private:
    std::ofstream m_file;
    std::ostringstream m_memory;
    std::ostream* m_out;
    amk::Journal m_journal;
};

// Creates the start menu shortcut that starts the launcher.
bool CreateShortcut(const fs::path& shortcut, const fs::path& target, const fs::path& workingDir) {
    IShellLinkW* link = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link)))) return false;

    link->SetPath(target.c_str());
    link->SetWorkingDirectory(workingDir.c_str());
    link->SetDescription(kShortcutDescription.data());
    link->SetIconLocation(target.c_str(), 0);

    IPersistFile* file = nullptr;
    HRESULT hr = link->QueryInterface(IID_PPV_ARGS(&file));
    if (SUCCEEDED(hr)) {
        hr = file->Save(shortcut.c_str(), TRUE);
        file->Release();
    }
    link->Release();
    return SUCCEEDED(hr);
}

void SetRegistryString(HKEY key, const wchar_t* name, const std::wstring& value) {
    RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
                   static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
}

void SetRegistryDword(HKEY key, const wchar_t* name, DWORD value) {
    RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
}

// The entry that makes the kit show in Settings > Apps, and gives the uninstall command.
bool WriteUninstallEntry(const fs::path& target, DWORD sizeKb) {
    HKEY key = nullptr;
    LSTATUS status = RegCreateKeyExW(HKEY_CURRENT_USER, amk::win::FromUtf8(amk::kUninstallKey).c_str(), 0, nullptr,
                                     REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &key, nullptr);
    if (status != ERROR_SUCCESS) return false;

    std::wstring setup = (target / amk::kSetupFile).wstring();
    SetRegistryString(key, L"DisplayName", amk::win::FromUtf8(amk::kDisplayName));
    SetRegistryString(key, L"DisplayVersion", amk::win::FromUtf8(amk::kKitVersion));
    SetRegistryString(key, L"Publisher", amk::win::FromUtf8(amk::kPublisher));
    SetRegistryString(key, L"InstallLocation", target.wstring());
    SetRegistryString(key, L"DisplayIcon", (target / amk::kLauncherFile).wstring() + L",0");
    SetRegistryString(key, L"UninstallString", L"\"" + setup + L"\" /uninstall");
    SetRegistryString(key, L"QuietUninstallString", L"\"" + setup + L"\" /uninstall /silent /keepdata");
    SetRegistryDword(key, L"NoModify", 1);
    SetRegistryDword(key, L"NoRepair", 1);
    SetRegistryDword(key, L"EstimatedSize", sizeKb);
    RegCloseKey(key);
    return true;
}

// Sum of the installed files, in kilobytes, as Settings > Apps shows it.
DWORD InstalledSizeKb(const fs::path& directory) {
    std::uintmax_t bytes = 0;
    for (std::string_view name : amk::InstalledFiles()) {
        std::error_code error;
        std::uintmax_t size = fs::file_size(directory / amk::win::FromUtf8(name), error);
        if (!error) bytes += size;
    }
    return static_cast<DWORD>(bytes / 1024);
}

// Copies one file of the kit. Skips the copy when the source already is the destination.
bool CopyKitFile(const fs::path& source, const fs::path& destination, Log& log) {
    std::error_code error;
    if (fs::exists(destination, error) && fs::equivalent(source, destination, error)) return true;

    fs::copy_file(source, destination, fs::copy_options::overwrite_existing, error);
    if (error) {
        log.Write("Copie impossible de " + amk::win::ToUtf8(destination.wstring()) + " : " + error.message() +
                  ". Si le fichier est utilisé, fermez Apple Music et relancez l'installation.");
        return false;
    }
    return true;
}

int Install(bool silent) {
    Log log(amk::win::JournalFile("installation.log"));
    fs::path source = amk::win::ExecutablePath().parent_path();
    fs::path target = amk::win::InstallDirectory();

    log.Write("Installation du kit " + std::string(amk::kKitVersion) + " depuis " + amk::win::ToUtf8(source.wstring()));
    for (std::string_view name : amk::InstalledFiles()) {
        std::error_code error;
        if (!fs::exists(source / amk::win::FromUtf8(name), error)) {
            std::string message = "Fichier manquant à côté de l'installateur : " + std::string(name);
            log.Write(message);
            if (!silent) amk::win::ShowMessage(amk::win::FromUtf8(message), true);
            return kExitError;
        }
    }

    std::optional<amk::launcher::InstalledPackage> package =
        amk::launcher::FindInstalledPackage(amk::win::FromUtf8(amk::kPackageFamilyName).c_str());
    std::string packageLine = package
                                  ? "Apple Music trouvé : " + amk::win::ToUtf8(package->fullName)
                                  : "Apple Music pour Windows introuvable : le kit s'installe quand même, mais il ne "
                                    "lancera rien tant qu'Apple Music n'est pas installé";
    log.Write(packageLine);

    std::error_code error;
    fs::create_directories(target, error);
    if (error) {
        log.Write("Dossier impossible à créer : " + error.message());
        if (!silent) amk::win::ShowMessage(L"Le dossier d'installation ne peut pas être créé.", true);
        return kExitError;
    }

    for (std::string_view name : amk::InstalledFiles()) {
        std::wstring wideName = amk::win::FromUtf8(name);
        if (!CopyKitFile(source / wideName, target / wideName, log)) {
            if (!silent) {
                amk::win::ShowMessage(L"L'installation n'a pas pu copier " + wideName +
                                          L". Fermez Apple Music puis relancez l'installation.",
                                      true);
            }
            return kExitRefused;
        }
    }

    fs::path shortcut = amk::win::KnownFolder(FOLDERID_Programs) / amk::win::FromUtf8(amk::kShortcutFile);
    bool shortcutOk = CreateShortcut(shortcut, target / amk::win::FromUtf8(amk::kLauncherFile), target);
    log.Write(shortcutOk ? "Raccourci créé dans le menu Démarrer" : "Raccourci impossible à créer");

    bool entryOk = WriteUninstallEntry(target, InstalledSizeKb(target));
    log.Write(entryOk ? "Entrée de désinstallation écrite (Paramètres > Applications)"
                      : "Entrée de désinstallation impossible à écrire");

    log.Write("Installation terminée dans " + amk::win::ToUtf8(target.wstring()));
    if (!silent) {
        std::wstring text = L"Apple Music avec mods est installé.\n\nLancez-le depuis le menu Démarrer : « Apple Music avec mods ».";
        if (!package) text += L"\n\nApple Music pour Windows n'a pas été trouvé : installez-le depuis le Microsoft Store.";
        amk::win::ShowMessage(text);
    }
    return kExitDone;
}

int Uninstall(bool silent, bool keepFlag, bool purgeFlag) {
    fs::path target = amk::win::InstallDirectory();

    // The kernel lives inside Apple Music, and the launcher keeps the app running. Nothing may be deleted under them.
    bool running = !amk::win::ProcessIdsNamed(L"AppleMusic.exe").empty() ||
                   !amk::win::ProcessIdsNamed(L"AMPLibraryAgent.exe").empty() ||
                   !amk::win::ProcessIdsNamed(L"amk_launcher.exe").empty();
    if (running) {
        if (!silent) {
            amk::win::ShowMessage(L"Fermez Apple Music, attendez quelques secondes, puis relancez la désinstallation.",
                                  true);
        }
        return kExitRefused;
    }

    if (!silent && !amk::win::AskYesNo(L"Désinstaller Apple Music avec mods ?\n\nApple Music redevient "
                                        L"exactement ce qu'il était, sans mods.",
                                        L"Apple Music Mod Kit")) {
        return kExitDone;
    }

    // Settings, journals and reports stay unless the user asks for them to go. Silent runs keep them by default.
    bool keepData = true;
    if (purgeFlag) {
        keepData = false;
    } else if (!keepFlag && !silent) {
        keepData = amk::win::AskYesNo(L"Garder vos réglages, vos journaux et vos rapports de test ?\n\n"
                                      L"Oui : ils restent dans " + amk::win::KitDirectory().wstring() +
                                          L".\nNon : ils sont supprimés.",
                                      L"Apple Music Mod Kit");
    }

    Log log(keepData ? amk::win::JournalFile("desinstallation.log") : fs::path());
    log.Write("Désinstallation du kit " + std::string(amk::kKitVersion) +
              (keepData ? " (données gardées)" : " (données supprimées)"));

    // Files first: if one is in use, nothing else is removed yet, so the uninstall can simply be run again.
    for (std::string_view name : amk::InstalledFiles()) {
        std::wstring wideName = amk::win::FromUtf8(name);
        if (wideName == amk::win::FromUtf8(amk::kSetupFile)) continue;

        std::error_code error;
        fs::remove(target / wideName, error);
        if (error && fs::exists(target / wideName)) {
            log.Write("Suppression impossible de " + std::string(name) + " : " + error.message());
            if (!silent) {
                amk::win::ShowMessage(L"Impossible de supprimer " + wideName +
                                          L". Fermez Apple Music puis relancez la désinstallation.",
                                      true);
            }
            return kExitRefused;
        }
        log.Write("Supprimé : " + std::string(name));
    }

    fs::path shortcut = amk::win::KnownFolder(FOLDERID_Programs) / amk::win::FromUtf8(amk::kShortcutFile);
    std::error_code shortcutError;
    fs::remove(shortcut, shortcutError);
    log.Write("Raccourci du menu Démarrer supprimé");

    LSTATUS status = RegDeleteTreeW(HKEY_CURRENT_USER, amk::win::FromUtf8(amk::kUninstallKey).c_str());
    log.Write(status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND ? "Entrée de désinstallation supprimée"
                                                                        : "Entrée de désinstallation : erreur");

    if (!keepData) {
        fs::path data = amk::win::KitDirectory();
        if (amk::IsKitDataFolder(amk::win::ToUtf8(data.wstring()))) {
            std::error_code error;
            fs::remove_all(data, error);
        }
    }

    // The installer cannot delete itself while it runs. A short-lived command does it once this process has exited.
    std::error_code selfError;
    fs::remove(target / amk::win::FromUtf8(amk::kSetupFile), selfError);
    if (selfError || fs::exists(target / amk::win::FromUtf8(amk::kSetupFile))) {
        std::wstring command = L"cmd.exe /c ping -n 3 127.0.0.1 >nul & del /f /q \"" +
                               (target / amk::win::FromUtf8(amk::kSetupFile)).wstring() + L"\" & rmdir \"" +
                               target.wstring() + L"\"";
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESHOWWINDOW;
        startup.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION process{};
        std::vector<wchar_t> buffer(command.begin(), command.end());
        buffer.push_back(L'\0');
        if (CreateProcessW(nullptr, buffer.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                           nullptr, nullptr, &startup, &process)) {
            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);
        }
    } else {
        std::error_code folderError;
        fs::remove(target, folderError);
    }

    log.Write("Désinstallation terminée");
    if (!silent) {
        std::wstring text = L"Apple Music avec mods est désinstallé. Apple Music redevient normal.";
        if (keepData) text += L"\n\nVos réglages et rapports sont restés dans " + amk::win::KitDirectory().wstring() + L".";
        amk::win::ShowMessage(text);
    }
    return kExitDone;
}

}  // namespace

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    bool silent = HasFlag(argc, argv, L"/silent");
    bool uninstall = HasFlag(argc, argv, L"/uninstall");
    bool keep = HasFlag(argc, argv, L"/keepdata");
    bool purge = HasFlag(argc, argv, L"/purgedata");
    LocalFree(argv);

    int code = uninstall ? Uninstall(silent, keep, purge) : Install(silent);

    CoUninitialize();
    return code;
}
