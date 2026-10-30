#include "win_common.hpp"

#include <knownfolders.h>
#include <shlobj.h>
#include <appmodel.h>

#include <system_error>
#include <vector>

namespace amk::win {

namespace {

struct HungWindowSearch {
    DWORD processId;
    bool hung;
};

BOOL CALLBACK FindHungWindow(HWND window, LPARAM parameter) {
    auto* search = reinterpret_cast<HungWindowSearch*>(parameter);

    DWORD owner = 0;
    GetWindowThreadProcessId(window, &owner);
    if (owner == search->processId && IsWindowVisible(window) && IsHungAppWindow(window)) {
        search->hung = true;
        return FALSE;  // one hung window is enough
    }
    return TRUE;
}

}  // namespace

std::filesystem::path KitDirectory() {
    PWSTR profile = nullptr;
    std::filesystem::path profilePath;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Profile, 0, nullptr, &profile))) {
        profilePath = profile;
    }
    CoTaskMemFree(profile);

    if (profilePath.empty()) return std::filesystem::temp_directory_path() / L"AppleMusicModKit";
    return profilePath / L"AppleMusicModKit";
}

std::filesystem::path JournalFile(const std::string& name) {
    std::filesystem::path directory = KitDirectory() / L"journal";
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    return directory / name;
}

std::filesystem::path ExecutablePath() {
    std::vector<wchar_t> buffer(32768);
    DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    return std::filesystem::path(std::wstring(buffer.data(), length));
}

std::string PackageFullName() {
    UINT32 length = 0;
    if (GetCurrentPackageFullName(&length, nullptr) != ERROR_INSUFFICIENT_BUFFER || length == 0) return {};

    std::wstring name(length, L'\0');  // `length` counts the terminating null
    if (GetCurrentPackageFullName(&length, name.data()) != ERROR_SUCCESS) return {};
    name.resize(length - 1);

    // Package full names are ASCII, so a plain narrowing copy is exact.
    return std::string(name.begin(), name.end());
}

bool WindowsResponsive(DWORD processId) {
    HungWindowSearch search{processId, false};
    EnumWindows(FindHungWindow, reinterpret_cast<LPARAM>(&search));
    return !search.hung;
}

}  // namespace amk::win
