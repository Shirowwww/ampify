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

// Waits for a process with a full command line. Output goes to a file that the child inherits.
int StartAndWait(std::wstring commandLine, const std::filesystem::path& outputFile) {
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;

    HANDLE output = INVALID_HANDLE_VALUE;
    if (!outputFile.empty()) {
        SECURITY_ATTRIBUTES inherit{sizeof(inherit), nullptr, TRUE};
        output = CreateFileW(outputFile.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &inherit, CREATE_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
        if (output != INVALID_HANDLE_VALUE) {
            startup.dwFlags |= STARTF_USESTDHANDLES;
            startup.hStdOutput = output;
            startup.hStdError = output;
        }
    }

    PROCESS_INFORMATION process{};
    std::vector<wchar_t> buffer(commandLine.begin(), commandLine.end());
    buffer.push_back(L'\0');
    BOOL started = CreateProcessW(nullptr, buffer.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr,
                                  &startup, &process);
    if (output != INVALID_HANDLE_VALUE) CloseHandle(output);
    if (!started) return -1;

    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return static_cast<int>(code);
}

}  // namespace

std::filesystem::path KitDirectory() {
    std::filesystem::path profilePath = KnownFolder(FOLDERID_Profile);
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
    return KnownFolder(FOLDERID_LocalAppData) / L"Programs" / L"AppleMusicModKit";
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

int RunAndWait(const std::wstring& commandLine, const std::filesystem::path& outputFile) {
    return StartAndWait(commandLine, outputFile);
}

int RunElevatedAndWait(const std::filesystem::path& program, const std::wstring& parameters) {
    if (IsElevated()) return StartAndWait(L"\"" + program.wstring() + L"\" " + parameters, {});

    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOCLOSEPROCESS;
    info.lpVerb = L"runas";
    info.lpFile = program.c_str();
    info.lpParameters = parameters.c_str();
    info.nShow = SW_HIDE;
    if (!ShellExecuteExW(&info) || !info.hProcess) return -1;

    WaitForSingleObject(info.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(info.hProcess, &code);
    CloseHandle(info.hProcess);
    return static_cast<int>(code);
}

bool AskYesNo(const std::wstring& text, const std::wstring& title) {
    return MessageBoxW(nullptr, text.c_str(), title.c_str(), MB_ICONQUESTION | MB_YESNO) == IDYES;
}

void ShowMessage(const std::wstring& text, bool error) {
    MessageBoxW(nullptr, text.c_str(), L"Apple Music Mod Kit", (error ? MB_ICONERROR : MB_ICONINFORMATION) | MB_OK);
}

}  // namespace amk::win
