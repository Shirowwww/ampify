// ampify_setup.exe: installs Ampify and uninstalls it. The same file is copied into the install folder as the uninstaller.
//
//   ampify_setup.exe                          install, after a confirmation
//   ampify_setup.exe /silent                  install without any dialog (tests, scripts)
//   ampify_setup.exe /uninstall               uninstall, asks whether to keep the user's data
//   ampify_setup.exe /uninstall /keepdata     uninstall and keep settings, logs and reports
//   ampify_setup.exe /uninstall /purgedata    uninstall and delete the user's data folder too
//   add /silent to any uninstall to skip the dialogs (without a data choice, the data is kept)
//
// Exit codes: 0 done (or cancelled by the user), 1 error, 3 refused because Apple Music is open.
//
// Ampify never writes into Apple Music's package folder. Installing copies the kit's files, puts the elements that ship
// with it (the Marketplace, Now Playing) in the user's Ampify folder, writes an uninstall entry, and starts the
// background launcher, now and at each Windows sign-in: from then on Apple Music starts with Ampify from its usual
// icon (src/win/background.hpp). Uninstalling stops it and removes all of it, so Apple Music starts as it did before.

#include <windows.h>
#include <objbase.h>
#include <shlobj.h>
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
#include "../win/background.hpp"
#include "../win/package.hpp"
#include "../win/win_common.hpp"

namespace fs = std::filesystem;

namespace {

constexpr int kExitDone = 0;
constexpr int kExitError = 1;
constexpr int kExitRefused = 3;

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

// Versions up to 1.0.2 started Apple Music from their own Start menu shortcut. Removed on install and uninstall.
void RemoveOldShortcut(Log& log) {
    fs::path shortcut = ampify::win::KnownFolder(FOLDERID_Programs) / ampify::win::FromUtf8(ampify::kShortcutFile);
    std::error_code error;
    if (fs::remove(shortcut, error)) log.Write("Old Start menu shortcut removed");
}

void SetRegistryString(HKEY key, const wchar_t* name, const std::wstring& value) {
    RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
                   static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
}

void SetRegistryDword(HKEY key, const wchar_t* name, DWORD value) {
    RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
}

// Reads the user's Path. Absent, it is empty and `type` is REG_EXPAND_SZ, the type Windows gives it. False when it
// cannot be read whole, so nothing is ever written from a partial value.
bool ReadUserPath(HKEY key, std::wstring& path, DWORD& type) {
    DWORD size = 0;
    LSTATUS status = RegQueryValueExW(key, L"Path", nullptr, &type, nullptr, &size);
    if (status == ERROR_FILE_NOT_FOUND) {
        type = REG_EXPAND_SZ;
        path.clear();
        return true;
    }
    if (status != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) return false;
    path.assign(size / sizeof(wchar_t) + 1, L'\0');
    DWORD read = static_cast<DWORD>(path.size() * sizeof(wchar_t));
    if (RegQueryValueExW(key, L"Path", nullptr, &type, reinterpret_cast<BYTE*>(path.data()), &read) != ERROR_SUCCESS) {
        return false;
    }
    path.resize(wcsnlen(path.c_str(), path.size()));
    return true;
}

// Puts the install folder in the user's Path (HKCU\Environment), or takes it out, so `ampify` works in any new
// terminal. The value keeps its type, so %VARIABLES% in it stay unexpanded.
bool UpdateUserPath(const fs::path& folder, bool add) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Environment", 0, KEY_QUERY_VALUE | KEY_SET_VALUE, &key) != ERROR_SUCCESS) {
        return false;
    }
    std::wstring current;
    DWORD type = REG_EXPAND_SZ;
    if (!ReadUserPath(key, current, type)) {
        RegCloseKey(key);
        return false;
    }
    std::string before = ampify::win::ToUtf8(current);
    std::string folderText = ampify::win::ToUtf8(folder.wstring());
    std::string after = add ? ampify::PathWithFolder(before, folderText) : ampify::PathWithoutFolder(before, folderText);
    bool ok = true;
    if (after != before) {
        std::wstring value = ampify::win::FromUtf8(after);
        ok = value.empty() ? RegDeleteValueW(key, L"Path") == ERROR_SUCCESS
                           : RegSetValueExW(key, L"Path", 0, type, reinterpret_cast<const BYTE*>(value.c_str()),
                                            static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
        // Tells Explorer, so terminals opened from now on see the new Path.
        DWORD_PTR ignored = 0;
        SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, reinterpret_cast<LPARAM>(L"Environment"),
                            SMTO_ABORTIFHUNG, 2000, &ignored);
    }
    RegCloseKey(key);
    return ok;
}

// The kernel lives inside Apple Music: no file of Ampify may be replaced or deleted under it.
bool AppleMusicRunning() {
    return !ampify::win::ProcessIdsNamed(L"AppleMusic.exe").empty() ||
           !ampify::win::ProcessIdsNamed(L"AMPLibraryAgent.exe").empty();
}

// What the install left of the automatic start, for the log and the final message.
struct AutoStartResult {
    bool registered = false;
    bool started = false;
    bool deferred = false;  // run as administrator: the launcher starts at the next sign-in instead
};

// Registers the background launcher at sign-in and starts it. An installer run as administrator does not start it:
// a launcher with administrator rights could not be stopped by ampify restore or the uninstaller from a normal session.
AutoStartResult SetUpAutoStart(const fs::path& launcher, Log& log) {
    AutoStartResult result;
    result.registered = ampify::win::RegisterBackground(launcher);
    log.Write(result.registered ? "Background launcher registered: it starts at each Windows sign-in"
                                : "Could not register the background launcher at sign-in");
    result.deferred = ampify::win::IsElevated();
    if (result.deferred) {
        log.Write("Installer run as administrator: the background launcher starts at the next sign-in");
        return result;
    }
    result.started = ampify::win::StartBackground(launcher);
    log.Write(result.started ? "Background launcher started" : "Could not start the background launcher");
    return result;
}

std::wstring InstalledMessage(const AutoStartResult& autoStart, bool appleMusicFound, bool bundledOk) {
    std::wstring text = L"Ampify is installed.\n\nOpen Apple Music as usual: Ampify loads with it each time."
                        L"\n\nIn a new terminal, ampify help lists the commands.";
    if (autoStart.deferred) {
        text += L"\n\nThe installer ran as administrator, so Ampify starts with your next Windows sign-in. To use it "
                L"now, run \"ampify apply\" in a terminal that is not an administrator one.";
    } else if (!autoStart.registered || !autoStart.started) {
        text += L"\n\nThe background launcher could not be set up (details in logs\\install.log): run "
                L"\"ampify apply\" in a terminal to try again.";
    }
    if (!appleMusicFound) text += L"\n\nApple Music for Windows was not found: install it from the Microsoft Store.";
    if (!bundledOk) {
        text += L"\n\nThe Marketplace or Now Playing could not be copied to your Ampify folder (details in "
                L"logs\\install.log). Run the install again from the extracted folder.";
    }
    return text;
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

    if (AppleMusicRunning()) {
        log.Write("Apple Music is open: nothing installed");
        if (!silent) ampify::win::ShowMessage(L"Close Apple Music, wait a few seconds, then run the install again.", true);
        return kExitRefused;
    }

    // The background launcher runs from the install folder: it stops while its file is replaced, and starts again if
    // the install stops half way.
    fs::path launcher = target / ampify::win::FromUtf8(ampify::kLauncherFile);
    bool wasRunning = ampify::win::BackgroundRunning();
    if (!ampify::win::StopBackground()) log.Write("The background launcher did not stop: its file may stay in use");
    auto stopHere = [&](int code) {
        if (wasRunning) ampify::win::StartBackground(launcher);
        return code;
    };

    std::error_code error;
    fs::create_directories(target, error);
    if (error) {
        log.Write("Could not create the folder: " + error.message());
        if (!silent) ampify::win::ShowMessage(L"The install folder could not be created.", true);
        return stopHere(kExitError);
    }

    for (std::string_view name : ampify::InstalledFiles()) {
        std::wstring wideName = ampify::win::FromUtf8(name);
        if (!CopyKitFile(source / wideName, target / wideName, log)) {
            if (!silent) {
                ampify::win::ShowMessage(L"The install could not copy " + wideName +
                                          L". Close Apple Music, then run the install again.",
                                      true);
            }
            return stopHere(kExitRefused);
        }
    }

    // A copy left by an older version would keep its old page, so each install replaces them.
    std::vector<std::string> notInstalled = ampify::InstallBundledElements(source, ampify::win::KitDirectory());
    for (const std::string& line : notInstalled) log.Write("Not installed: " + line);
    if (notInstalled.empty()) log.Write("Marketplace and Now Playing installed in the Ampify folder");

    RemoveOldShortcut(log);
    log.Write(UpdateUserPath(target, true) ? "Install folder in the user's Path: ampify works in a new terminal"
                                           : "Could not add the install folder to the user's Path");
    AutoStartResult autoStart = SetUpAutoStart(launcher, log);

    bool entryOk = WriteUninstallEntry(target, InstalledSizeKb(target));
    log.Write(entryOk ? "Uninstall entry written (Settings > Apps)"
                      : "Could not write the uninstall entry");

    log.Write("Install finished in " + ampify::win::ToUtf8(target.wstring()));
    if (!silent) ampify::win::ShowMessage(InstalledMessage(autoStart, package.has_value(), notInstalled.empty()));
    return kExitDone;
}

int Uninstall(bool silent, bool keepFlag, bool purgeFlag) {
    fs::path target = ampify::win::InstallDirectory();

    if (AppleMusicRunning()) {
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

    // The background launcher goes first: from now on Apple Music starts without Ampify, even if a file stays.
    log.Write(ampify::win::UnregisterBackground() ? "Background launcher removed from sign-in"
                                                  : "Could not remove the background launcher from sign-in");
    if (!ampify::win::StopBackground()) {
        log.Write("The background launcher did not stop");
        if (!silent) ampify::win::ShowMessage(L"Ampify could not stop its background launcher. Try again.", true);
        return kExitRefused;
    }

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

    RemoveOldShortcut(log);
    log.Write(UpdateUserPath(target, false) ? "Install folder removed from the user's Path"
                                            : "Could not remove the install folder from the user's Path");

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
        ampify::win::StartProcess(nullptr, command, CREATE_NO_WINDOW, nullptr, true);
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
