// ampify_setup.exe: installs Ampify and uninstalls it. The same file is copied into the install folder as the uninstaller.
//
//   ampify_setup.exe                          install, after a confirmation
//   ampify_setup.exe /silent                  install without any dialog (tests, scripts)
//   ampify_setup.exe /uninstall               uninstall, asks whether to keep the user's data
//   ampify_setup.exe /uninstall /keepdata     uninstall and keep settings, logs and reports
//   ampify_setup.exe /uninstall /purgedata    uninstall and delete the user's data folder too
//   add /silent to any uninstall to skip the dialogs (without a data choice, the data is kept)
//
// Exit codes: 0 done (or cancelled by the user), 1 error, 3 refused because Apple Music or the launcher is open.
//
// Ampify never writes into Apple Music's package folder. Installing only copies three files, adds a start menu
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

constexpr std::wstring_view kShortcutDescription = L"Starts Apple Music with Ampify";

bool HasFlag(int argc, wchar_t** argv, const wchar_t* flag) {
    for (int i = 1; i < argc; i++) {
        if (_wcsicmp(argv[i], flag) == 0) return true;
    }
    return false;
}

// Writes the log to a file. An empty path keeps it in memory: used when the data folder is about to be deleted,
// so the log does not survive in a file of its own.
class Log {
public:
    explicit Log(const fs::path& file)
        : m_file(file, std::ios::trunc),
          m_out(file.empty() ? static_cast<std::ostream*>(&m_memory) : &m_file),
          m_journal(*m_out, ampify::Journal::LocalTimeStamp) {}

    void Write(const std::string& line) { m_journal.Write(line); }

private:
    std::ofstream m_file;
    std::ostringstream m_memory;
    std::ostream* m_out;
    ampify::Journal m_journal;
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

// The entry that makes Ampify show in Settings > Apps, and gives the uninstall command.
bool WriteUninstallEntry(const fs::path& target, DWORD sizeKb) {
    HKEY key = nullptr;
    LSTATUS status = RegCreateKeyExW(HKEY_CURRENT_USER, ampify::win::FromUtf8(ampify::kUninstallKey).c_str(), 0, nullptr,
                                     REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &key, nullptr);
    if (status != ERROR_SUCCESS) return false;

    std::wstring setup = (target / ampify::kSetupFile).wstring();
    SetRegistryString(key, L"DisplayName", ampify::win::FromUtf8(ampify::kDisplayName));
    SetRegistryString(key, L"DisplayVersion", ampify::win::FromUtf8(ampify::kKitVersion));
    SetRegistryString(key, L"Publisher", ampify::win::FromUtf8(ampify::kPublisher));
    SetRegistryString(key, L"InstallLocation", target.wstring());
    SetRegistryString(key, L"DisplayIcon", (target / ampify::kLauncherFile).wstring() + L",0");
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
    for (std::string_view name : ampify::InstalledFiles()) {
        std::error_code error;
        std::uintmax_t size = fs::file_size(directory / ampify::win::FromUtf8(name), error);
        if (!error) bytes += size;
    }
    return static_cast<DWORD>(bytes / 1024);
}

// Copies one file of Ampify. Skips the copy when the source already is the destination.
bool CopyKitFile(const fs::path& source, const fs::path& destination, Log& log) {
    std::error_code error;
    if (fs::exists(destination, error) && fs::equivalent(source, destination, error)) return true;

    fs::copy_file(source, destination, fs::copy_options::overwrite_existing, error);
    if (error) {
        log.Write("Could not copy " + ampify::win::ToUtf8(destination.wstring()) + ": " + error.message() +
                  ". If the file is in use, close Apple Music and run the install again.");
        return false;
    }
    return true;
}

int Install(bool silent) {
    Log log(ampify::win::JournalFile("install.log"));
    fs::path source = ampify::win::ExecutablePath().parent_path();
    fs::path target = ampify::win::InstallDirectory();

    log.Write("Installing Ampify " + std::string(ampify::kKitVersion) + " from " + ampify::win::ToUtf8(source.wstring()));
    for (std::string_view name : ampify::InstalledFiles()) {
        std::error_code error;
        if (!fs::exists(source / ampify::win::FromUtf8(name), error)) {
            std::string message = "File missing next to the installer: " + std::string(name);
            log.Write(message);
            if (!silent) ampify::win::ShowMessage(ampify::win::FromUtf8(message), true);
            return kExitError;
        }
    }

    std::optional<ampify::launcher::InstalledPackage> package =
        ampify::launcher::FindInstalledPackage(ampify::win::FromUtf8(ampify::kPackageFamilyName).c_str());
    std::string packageLine = package
                                  ? "Apple Music found: " + ampify::win::ToUtf8(package->fullName)
                                  : "Apple Music for Windows not found: Ampify installs anyway, but it will "
                                    "start nothing until Apple Music is installed";
    log.Write(packageLine);

    std::error_code error;
    fs::create_directories(target, error);
    if (error) {
        log.Write("Could not create the folder: " + error.message());
        if (!silent) ampify::win::ShowMessage(L"The install folder could not be created.", true);
        return kExitError;
    }

    for (std::string_view name : ampify::InstalledFiles()) {
        std::wstring wideName = ampify::win::FromUtf8(name);
        if (!CopyKitFile(source / wideName, target / wideName, log)) {
            if (!silent) {
                ampify::win::ShowMessage(L"The install could not copy " + wideName +
                                          L". Close Apple Music, then run the install again.",
                                      true);
            }
            return kExitRefused;
        }
    }

    fs::path shortcut = ampify::win::KnownFolder(FOLDERID_Programs) / ampify::win::FromUtf8(ampify::kShortcutFile);
    bool shortcutOk = CreateShortcut(shortcut, target / ampify::win::FromUtf8(ampify::kLauncherFile), target);
    log.Write(shortcutOk ? "Shortcut created in the Start menu" : "Could not create the shortcut");

    bool entryOk = WriteUninstallEntry(target, InstalledSizeKb(target));
    log.Write(entryOk ? "Uninstall entry written (Settings > Apps)"
                      : "Could not write the uninstall entry");

    log.Write("Install finished in " + ampify::win::ToUtf8(target.wstring()));
    if (!silent) {
        std::wstring text = L"Ampify is installed.\n\nStart it from the Start menu: \"Apple Music with Ampify\".";
        if (!package) text += L"\n\nApple Music for Windows was not found: install it from the Microsoft Store.";
        ampify::win::ShowMessage(text);
    }
    return kExitDone;
}

int Uninstall(bool silent, bool keepFlag, bool purgeFlag) {
    fs::path target = ampify::win::InstallDirectory();

    // The kernel lives inside Apple Music, and the launcher keeps the app running. Nothing may be deleted under them.
    bool running = !ampify::win::ProcessIdsNamed(L"AppleMusic.exe").empty() ||
                   !ampify::win::ProcessIdsNamed(L"AMPLibraryAgent.exe").empty() ||
                   !ampify::win::ProcessIdsNamed(L"ampify_launcher.exe").empty();
    if (running) {
        if (!silent) {
            ampify::win::ShowMessage(L"Close Apple Music, wait a few seconds, then run the uninstall again.",
                                  true);
        }
        return kExitRefused;
    }

    if (!silent && !ampify::win::AskYesNo(L"Uninstall Ampify for Apple Music?\n\nApple Music goes back to "
                                        L"exactly how it was, without Ampify.",
                                        L"Ampify")) {
        return kExitDone;
    }

    // Settings, logs and reports stay unless the user asks for them to go. Silent runs keep them by default.
    bool keepData = true;
    if (purgeFlag) {
        keepData = false;
    } else if (!keepFlag && !silent) {
        keepData = ampify::win::AskYesNo(L"Keep your settings, logs and test reports?\n\n"
                                      L"Yes: they stay in " + ampify::win::KitDirectory().wstring() +
                                          L".\nNo: they are deleted.",
                                      L"Ampify");
    }

    Log log(keepData ? ampify::win::JournalFile("uninstall.log") : fs::path());
    log.Write("Uninstalling Ampify " + std::string(ampify::kKitVersion) +
              (keepData ? " (data kept)" : " (data deleted)"));

    // Files first: if one is in use, nothing else is removed yet, so the uninstall can simply be run again.
    for (std::string_view name : ampify::InstalledFiles()) {
        std::wstring wideName = ampify::win::FromUtf8(name);
        if (wideName == ampify::win::FromUtf8(ampify::kSetupFile)) continue;

        std::error_code error;
        fs::remove(target / wideName, error);
        if (error && fs::exists(target / wideName)) {
            log.Write("Could not remove " + std::string(name) + ": " + error.message());
            if (!silent) {
                ampify::win::ShowMessage(L"Could not remove " + wideName +
                                          L". Close Apple Music, then run the uninstall again.",
                                      true);
            }
            return kExitRefused;
        }
        log.Write("Removed: " + std::string(name));
    }

    fs::path shortcut = ampify::win::KnownFolder(FOLDERID_Programs) / ampify::win::FromUtf8(ampify::kShortcutFile);
    std::error_code shortcutError;
    fs::remove(shortcut, shortcutError);
    log.Write("Start menu shortcut removed");

    LSTATUS status = RegDeleteTreeW(HKEY_CURRENT_USER, ampify::win::FromUtf8(ampify::kUninstallKey).c_str());
    log.Write(status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND ? "Uninstall entry removed"
                                                                        : "Uninstall entry: error");

    if (!keepData) {
        fs::path data = ampify::win::KitDirectory();
        if (ampify::IsKitDataFolder(ampify::win::ToUtf8(data.wstring()))) {
            std::error_code error;
            fs::remove_all(data, error);
        }
    }

    // The installer cannot delete itself while it runs. A short-lived command does it once this process has exited.
    std::error_code selfError;
    fs::remove(target / ampify::win::FromUtf8(ampify::kSetupFile), selfError);
    if (selfError || fs::exists(target / ampify::win::FromUtf8(ampify::kSetupFile))) {
        std::wstring command = L"cmd.exe /c ping -n 3 127.0.0.1 >nul & del /f /q \"" +
                               (target / ampify::win::FromUtf8(ampify::kSetupFile)).wstring() + L"\" & rmdir \"" +
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

    log.Write("Uninstall finished");
    if (!silent) {
        std::wstring text = L"Ampify is uninstalled. Apple Music is back to normal.";
        if (keepData) text += L"\n\nYour settings and reports were kept in " + ampify::win::KitDirectory().wstring() + L".";
        ampify::win::ShowMessage(text);
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
