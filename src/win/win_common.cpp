#include "win_common.hpp"

#include <knownfolders.h>
#include <shlobj.h>
#include <shellapi.h>
#include <appmodel.h>
#include <tlhelp32.h>

#include <cstddef>
#include <fstream>
#include <system_error>
#include <vector>

namespace ampify::win {

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

// Ampify's logo at one size, from the module this code is built into. Loaded once per size and kept: LR_SHARED would
// hand back the first size loaded whatever size is asked for.
HICON LoadLogo(int width, int height) {
    HMODULE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&LoadLogo), &module);
    HANDLE logo = module ? LoadImageW(module, MAKEINTRESOURCEW(1), IMAGE_ICON, width, height, LR_DEFAULTCOLOR) : nullptr;
    return logo ? static_cast<HICON>(logo) : LoadIconW(nullptr, reinterpret_cast<LPCWSTR>(IDI_APPLICATION));
}

}  // namespace

std::filesystem::path KitDirectory() {
    std::filesystem::path profilePath = KnownFolder(FOLDERID_Profile);
    if (profilePath.empty()) return std::filesystem::temp_directory_path() / L"Ampify";
    return profilePath / L"Ampify";
}

std::filesystem::path JournalFile(const std::string& name) {
    std::filesystem::path directory = KitDirectory() / L"logs";
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

std::vector<std::byte> ReadFileBytes(const std::filesystem::path& file) {
    std::ifstream stream(file, std::ios::binary | std::ios::ate);
    if (!stream) return {};
    std::streamsize size = stream.tellg();
    if (size <= 0) return {};

    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    stream.seekg(0);
    if (!stream.read(reinterpret_cast<char*>(bytes.data()), size)) return {};
    return bytes;
}

std::filesystem::path KnownFolder(const KNOWNFOLDERID& folder) {
    PWSTR path = nullptr;
    std::filesystem::path result;
    if (SUCCEEDED(SHGetKnownFolderPath(folder, 0, nullptr, &path))) result = path;
    CoTaskMemFree(path);
    return result;
}

std::filesystem::path InstallDirectory() {
    return KnownFolder(FOLDERID_LocalAppData) / L"Programs" / L"Ampify";
}

std::string ToUtf8(std::wstring_view text) {
    if (text.empty()) return {};
    int length = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr,
                                     nullptr);
    std::string result(length, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), length, nullptr,
                        nullptr);
    return result;
}

std::wstring FromUtf8(std::string_view text) {
    if (text.empty()) return {};
    int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring result(length, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), length);
    return result;
}

bool IsElevated() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;

    TOKEN_ELEVATION elevation{};
    DWORD size = 0;
    bool elevated = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size) &&
                    elevation.TokenIsElevated != 0;
    CloseHandle(token);
    return elevated;
}

std::vector<DWORD> ProcessIdsNamed(const wchar_t* imageName) {
    std::vector<DWORD> found;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return found;

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, imageName) == 0) found.push_back(entry.th32ProcessID);
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return found;
}

bool AskYesNo(const std::wstring& text, const std::wstring& title) {
    return MessageBoxW(nullptr, text.c_str(), title.c_str(), MB_ICONQUESTION | MB_YESNO) == IDYES;
}

void ShowMessage(const std::wstring& text, bool error) {
    MessageBoxW(nullptr, text.c_str(), L"Ampify", (error ? MB_ICONERROR : MB_ICONINFORMATION) | MB_OK);
}

HICON AmpifyIcon(bool large) {
    static const HICON smallLogo = LoadLogo(GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON));
    static const HICON largeLogo = LoadLogo(GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON));
    return large ? largeLogo : smallLogo;
}

}  // namespace ampify::win
