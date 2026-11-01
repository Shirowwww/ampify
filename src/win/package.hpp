#pragma once

// Package and activation calls for the launcher. Windows only.

#include <windows.h>

#include <filesystem>
#include <optional>
#include <string>

namespace ampify::launcher {

struct InstalledPackage {
    std::wstring fullName;            // e.g. AppleInc.AppleMusicWin_1.1540.23042.0_x64__nzyj5cx40ttqa
    std::filesystem::path directory;  // the WindowsApps folder, read-only for us
};

// The installed package of a family name, or nothing when it is not installed.
std::optional<InstalledPackage> FindInstalledPackage(const wchar_t* familyName);

// Starts the package's app through the shell's activation manager. `processId` gets the new process. `arguments`,
// when not empty, is the argument string the app receives (app_launch_flags, built by src/core/launch_flags.hpp).
HRESULT ActivateApp(const wchar_t* appUserModelId, DWORD* processId, const std::wstring& arguments = {});


}  // namespace ampify::launcher
