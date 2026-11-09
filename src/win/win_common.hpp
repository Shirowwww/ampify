#pragma once

// Windows helpers shared by the kernel, the launcher, and the installer. Not compiled on Linux.

#include <windows.h>
#include <shlobj.h>

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ampify::win {

// %USERPROFILE%\Ampify. Deliberately outside AppData: MSIX redirects the writes that Apple Music
// makes under AppData, so the launcher and the kernel would not see the same files.
std::filesystem::path KitDirectory();

// Log file for one process, such as logs\launcher.log. Creates the folder. The file is replaced at
// every start, so it only ever holds the current run.
std::filesystem::path JournalFile(const std::string& name);

// The executable this code runs in: the launcher, or AppleMusic.exe / AMPLibraryAgent.exe for the kernel.
std::filesystem::path ExecutablePath();

// Empty when the process is not packaged.
std::string PackageFullName();

// False while a visible top-level window of `processId` is hung. True otherwise, including when it has no window.
bool WindowsResponsive(DWORD processId);

// True for the first Ampify kernel of `processId`, false for any later one, whichever folder it was built in: two
// launchers (the installed one and a bench build) may each load their own copy. The claim lasts until that process
// exits.
bool ClaimKernelOfProcess(DWORD processId);

// The bytes of a file, read whole. Empty when it cannot be read, which the signature check reports as unreadable.
std::vector<std::byte> ReadFileBytes(const std::filesystem::path& file);

// A Windows known folder, such as FOLDERID_LocalAppData. Empty when Windows does not give one.
std::filesystem::path KnownFolder(const KNOWNFOLDERID& folder);

// %LOCALAPPDATA%\Programs\Ampify, where the installer puts Ampify.
std::filesystem::path InstallDirectory();

// The version Settings > Apps shows for a program: the "DisplayVersion" of `key` under HKEY_CURRENT_USER. Only an entry
// that exists is changed, so an update never makes an uninstall entry for a copy the installer did not install.
// False when there is no such key or the value cannot be written.
bool SetDisplayVersion(const std::wstring& key, const std::wstring& version);

// Text of an HRESULT as 0x8000FFFF, for the logs.
std::string HresultText(long code);

// Conversions between UTF-8 (what the logs and the report use) and UTF-16 (what Windows uses).
std::string ToUtf8(std::wstring_view text);
std::wstring FromUtf8(std::string_view text);

// One argument of a command line, quoted the way CommandLineToArgvW reads it back: left as it is when it has no space,
// tab or quote; otherwise in quotes, with a quote escaped and the backslashes before a quote doubled.
std::wstring QuoteArgument(std::wstring_view argument);

// Starts a program and lets it run on its own: its handles are closed at once, none of ours is inherited. With a null
// `application`, the first word of `commandLine` names the program. `hidden` asks for no window. False when Windows
// refuses to start it.
bool StartProcess(const wchar_t* application, const std::wstring& commandLine, DWORD creationFlags,
                  const wchar_t* folder = nullptr, bool hidden = false);

// True when this process runs with administrator rights.
bool IsElevated();

// Ids of the running processes whose image file name is `imageName`, whatever folder they run from.
std::vector<DWORD> ProcessIdsNamed(const wchar_t* imageName);

// The image file names of the running processes, in UTF-8, each once. Empty when they cannot be listed.
std::vector<std::string> RunningImageNames();

// Whether a process named `imageName` runs, for a loop that asks several times a second. A snapshot of every process
// (ProcessIdsNamed) costs about 11 ms with 560 processes; this lists the ids (0.04 ms) and opens only new ones. Each
// process seen stays open until it ends, so its id cannot pass to a new process unnoticed.
class ProcessWatch {
public:
    explicit ProcessWatch(std::wstring imageName) : m_name(std::move(imageName)) {}
    ~ProcessWatch();
    ProcessWatch(const ProcessWatch&) = delete;
    ProcessWatch& operator=(const ProcessWatch&) = delete;

    bool Running();

private:
    struct Seen {
        // Null when Windows refuses to open it (an administrator's process, for a launcher without those rights):
        // tried again after `retryIn` more looks, since opening hundreds of them four times a second costs more
        // than the whole watch. Apple Music always opens, so an id it takes over is seen within that delay.
        HANDLE process = nullptr;
        unsigned retryIn = 0;
        bool named = false;
    };
    Seen Look(DWORD id) const;

    std::wstring m_name;
    std::vector<std::pair<DWORD, Seen>> m_seen;  // sorted by id
    std::vector<DWORD> m_ids;
};

// The locale name ("fr-FR") of the Windows display language, empty when Windows gives none. The one answer to "which
// language does this user read": the kernel, the command line and the installer all take it.
std::string UserLocaleName();

// Windows apps in dark mode (AppsUseLightTheme = 0), for theme.scheme = auto; light when the value cannot be read.
// The kernel picks the scheme with it and the command line tells the Marketplace the same one.
bool SystemUsesDarkMode();

// Local time in minutes from midnight, for theme.schedule.
int LocalMinute();

// The storefront of Apple's public catalog for this PC ("FR"), from Windows' region; "US" when it gives none.
std::wstring CatalogStorefront();

// Ampify's logo, from the module this code is built into (each Ampify file carries it, cmake/version.rc.in), at the
// small size (notification area) or the large one. Windows' application icon when the module has none (the tests).
// Kept for the life of the process: never destroy it.
HICON AmpifyIcon(bool large);

}  // namespace ampify::win
