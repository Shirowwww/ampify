#pragma once

// Windows helpers shared by the kernel, the launcher, and the installer. Not compiled on Linux.

#include <windows.h>
#include <shlobj.h>

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
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

// Full name of the package this process runs in. Empty when the process is not packaged.
std::string PackageFullName();

// False while a visible top-level window of `processId` is hung. True otherwise, including when it has no window.
bool WindowsResponsive(DWORD processId);

// The bytes of a file, read whole. Empty when it cannot be read, which the signature check reports as unreadable.
std::vector<std::byte> ReadFileBytes(const std::filesystem::path& file);

// A Windows known folder, such as FOLDERID_LocalAppData. Empty when Windows does not give one.
std::filesystem::path KnownFolder(const KNOWNFOLDERID& folder);

// %LOCALAPPDATA%\Programs\Ampify, where the installer puts Ampify.
std::filesystem::path InstallDirectory();

// Conversions between UTF-8 (what the logs and the report use) and UTF-16 (what Windows uses).
std::string ToUtf8(std::wstring_view text);
std::wstring FromUtf8(std::string_view text);

// True when this process runs with administrator rights.
bool IsElevated();

// Ids of the running processes whose image file name is `imageName`, whatever folder they run from.
std::vector<DWORD> ProcessIdsNamed(const wchar_t* imageName);

// Runs a program and waits for it. Its output goes to `outputFile` when one is given. Returns the exit code,
// or -1 when the program did not start.
int RunAndWait(const std::wstring& commandLine, const std::filesystem::path& outputFile = {});

// Runs a program with the administrator prompt. Returns the exit code, or -1 when the user refused or it did not start.
int RunElevatedAndWait(const std::filesystem::path& program, const std::wstring& parameters);

// Yes / No question in a dialog. Returns true for Yes.
bool AskYesNo(const std::wstring& text, const std::wstring& title);

// Dialog with one OK button. `error` picks the error icon.
void ShowMessage(const std::wstring& text, bool error = false);

}  // namespace ampify::win
