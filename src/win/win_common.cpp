#include "win_common.hpp"

#include <knownfolders.h>
#include <shlobj.h>
#include <shellapi.h>
#include <appmodel.h>
#include <psapi.h>
#include <tlhelp32.h>

#include <algorithm>
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

bool ClaimKernelOfProcess(DWORD processId) {
    // The winner's handle is never closed: the name stays taken until the process exits. A mutex Windows refuses to
    // create claims nothing, so the kernel stays idle rather than risk running twice.
    std::wstring name = L"Local\\Ampify.Kernel." + std::to_wstring(processId);
    HANDLE claim = CreateMutexW(nullptr, FALSE, name.c_str());
    bool first = claim && GetLastError() != ERROR_ALREADY_EXISTS;
    if (claim && !first) CloseHandle(claim);
    return first;
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

std::wstring QuoteArgument(std::wstring_view argument) {
    if (!argument.empty() && argument.find_first_of(L" \t\"") == std::wstring_view::npos) return std::wstring(argument);

    std::wstring out = L"\"";
    size_t i = 0;
    while (true) {
        size_t backslashes = 0;
        while (i < argument.size() && argument[i] == L'\\') {
            backslashes++;
            i++;
        }
        if (i == argument.size()) {
            out.append(backslashes * 2, L'\\');
            break;
        }
        if (argument[i] == L'"') {
            out.append(backslashes * 2 + 1, L'\\');
            out += L'"';
        } else {
            out.append(backslashes, L'\\');
            out += argument[i];
        }
        i++;
    }
    out += L'"';
    return out;
}

bool StartProcess(const wchar_t* application, const std::wstring& commandLine, DWORD creationFlags,
                  const wchar_t* folder, bool hidden) {
    // CreateProcessW may write into the command line, so it gets a copy of its own.
    std::vector<wchar_t> buffer(commandLine.begin(), commandLine.end());
    buffer.push_back(L'\0');
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    if (hidden) {
        startup.dwFlags = STARTF_USESHOWWINDOW;
        startup.wShowWindow = SW_HIDE;
    }
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(application, buffer.data(), nullptr, nullptr, FALSE, creationFlags, nullptr, folder, &startup,
                        &process)) {
        return false;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
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

ProcessWatch::~ProcessWatch() {
    for (auto& [id, seen] : m_seen) {
        if (seen.process) CloseHandle(seen.process);
    }
}

ProcessWatch::Seen ProcessWatch::Look(DWORD id) const {
    constexpr unsigned kRefusedRetryLooks = 7;  // two seconds at four looks a second
    Seen seen;
    seen.process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, id);
    if (!seen.process) {
        seen.retryIn = kRefusedRetryLooks;
        return seen;
    }
    wchar_t path[MAX_PATH];
    DWORD size = MAX_PATH;
    if (QueryFullProcessImageNameW(seen.process, 0, path, &size)) {
        const wchar_t* slash = wcsrchr(path, L'\\');
        seen.named = _wcsicmp(slash ? slash + 1 : path, m_name.c_str()) == 0;
    }
    return seen;
}

bool ProcessWatch::Running() {
    if (m_ids.empty()) m_ids.resize(1024);
    DWORD bytes = 0;
    for (;;) {
        DWORD capacity = static_cast<DWORD>(m_ids.size() * sizeof(DWORD));
        if (!EnumProcesses(m_ids.data(), capacity, &bytes)) return !ProcessIdsNamed(m_name.c_str()).empty();
        if (bytes < capacity) break;
        m_ids.resize(m_ids.size() * 2);
    }
    std::vector<DWORD> ids(m_ids.begin(), m_ids.begin() + bytes / sizeof(DWORD));
    std::sort(ids.begin(), ids.end());

    std::vector<std::pair<DWORD, Seen>> next;
    next.reserve(ids.size());
    auto old = m_seen.begin();
    bool running = false;
    for (DWORD id : ids) {
        while (old != m_seen.end() && old->first < id) {
            if (old->second.process) CloseHandle(old->second.process);
            ++old;
        }
        Seen seen;
        bool known = old != m_seen.end() && old->first == id;
        if (known && old->second.process && WaitForSingleObject(old->second.process, 0) == WAIT_TIMEOUT) {
            seen = old->second;
        } else if (known && !old->second.process && old->second.retryIn > 0) {
            seen = old->second;
            seen.retryIn--;
        } else {
            if (known && old->second.process) CloseHandle(old->second.process);
            seen = Look(id);
        }
        if (known) ++old;
        running = running || seen.named;
        next.emplace_back(id, seen);
    }
    for (; old != m_seen.end(); ++old) {
        if (old->second.process) CloseHandle(old->second.process);
    }
    m_seen = std::move(next);
    return running;
}

bool SetDisplayVersion(const std::wstring& key, const std::wstring& version) {
    HKEY handle = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, KEY_SET_VALUE, &handle) != ERROR_SUCCESS) return false;
    LSTATUS status = RegSetValueExW(handle, L"DisplayVersion", 0, REG_SZ, reinterpret_cast<const BYTE*>(version.c_str()),
                                    static_cast<DWORD>((version.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(handle);
    return status == ERROR_SUCCESS;
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
