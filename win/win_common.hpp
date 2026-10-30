#pragma once

// Windows helpers shared by the kernel and the launcher. Not compiled on Linux.

#include <windows.h>

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace amk::win {

// %USERPROFILE%\AppleMusicModKit. Deliberately outside AppData: MSIX redirects the writes that Apple Music
// makes under AppData, so the launcher and the kernel would not see the same files.
std::filesystem::path KitDirectory();

// Journal file for one process, such as journal\launcher.log. Creates the folder. The file is replaced at
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

}  // namespace amk::win
