// Windows side of platform.hpp. Programs are started with CreateProcessW, never through cmd.exe, so a path or a
// URL with quotes or ampersands cannot run anything else.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

#include "../core/apple_music.hpp"
#include "../win/background.hpp"
#include "../win/package.hpp"
#include "../win/win_common.hpp"
#include "platform.hpp"

namespace ampify::platform {

namespace {

using win::FromUtf8;
using win::ToUtf8;

std::wstring CommandLineOf(const std::vector<std::string>& command) {
    std::wstring line;
    for (const std::string& part : command) {
        if (!line.empty()) line += L' ';
        line += win::QuoteArgument(FromUtf8(part));
    }
    return line;
}

}  // namespace

std::vector<std::string> Initialize(int, char**) {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    int count = 0;
    LPWSTR* parts = CommandLineToArgvW(GetCommandLineW(), &count);
    std::vector<std::string> arguments;
    if (parts == nullptr) return arguments;
    for (int i = 0; i < count; i++) arguments.push_back(ToUtf8(parts[i]));
    LocalFree(parts);
    return arguments;
}

std::filesystem::path DefaultKitDirectory() {
    const wchar_t* profile = _wgetenv(L"USERPROFILE");
    if (profile == nullptr || *profile == L'\0') return std::filesystem::current_path() / "Ampify";
    return std::filesystem::path(profile) / L"Ampify";
}

std::filesystem::path ExecutableDirectory() {
    wchar_t buffer[MAX_PATH * 4];
    DWORD length = GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(std::size(buffer)));
    if (length == 0 || length >= std::size(buffer)) return std::filesystem::current_path();
    return std::filesystem::path(std::wstring(buffer, length)).parent_path();
}

int Run(const std::vector<std::string>& command, const std::filesystem::path& stdoutFile) {
    if (command.empty()) return -1;

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);

    HANDLE output = nullptr;
    if (!stdoutFile.empty()) {
        SECURITY_ATTRIBUTES inheritable{sizeof(inheritable), nullptr, TRUE};
        output = CreateFileW(stdoutFile.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &inheritable, CREATE_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
        if (output == INVALID_HANDLE_VALUE) return -1;
        startup.hStdOutput = output;
    }

    std::wstring line = CommandLineOf(command);
    std::vector<wchar_t> buffer(line.begin(), line.end());
    buffer.push_back(L'\0');

    PROCESS_INFORMATION process{};
    BOOL created = CreateProcessW(nullptr, buffer.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                                  nullptr, &startup, &process);
    if (output) CloseHandle(output);
    if (!created) return -1;

    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return static_cast<int>(code);
}

bool StartDetached(const std::vector<std::string>& command) {
    if (command.empty()) return false;
    return win::StartProcess(nullptr, CommandLineOf(command), CREATE_NEW_PROCESS_GROUP);
}

std::optional<std::filesystem::path> AppPackageFolder() {
    std::wstring family = FromUtf8(kPackageFamilyName);
    std::optional<launcher::InstalledPackage> package = launcher::FindInstalledPackage(family.c_str());
    if (!package) return std::nullopt;
    return package->directory;
}

std::optional<std::filesystem::path> AppPreferencesFile() {
    const wchar_t* local = _wgetenv(L"LOCALAPPDATA");
    if (local == nullptr || *local == L'\0') return std::nullopt;
    return std::filesystem::path(local) / L"Packages" / FromUtf8(kPackageFamilyName) / L"LocalCache" /
           L"Roaming" / L"Apple Computer" / L"Preferences" / L"AMPLibraryAgent.exe.plist";
}

std::vector<std::string> OpenFolderCommand(const std::filesystem::path& folder) {
    return {"explorer.exe", ToUtf8(folder.wstring())};
}

bool IsProcessRunning(const std::string& imageName) {
    std::filesystem::path listing = TemporaryFile("ampify-tasklist");
    int code = Run({"tasklist.exe", "/FI", "IMAGENAME eq " + imageName, "/NH"}, listing);
    bool running = false;
    if (code == 0) {
        std::ifstream in(listing, std::ios::binary);
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        running = text.find(imageName) != std::string::npos;
    }
    std::error_code ec;
    std::filesystem::remove(listing, ec);
    return running;
}

AutoStart AutoStartState() {
    return AutoStart{true, win::BackgroundRegistered(), win::BackgroundRunning()};
}

bool EnableAutoStart(const std::filesystem::path& launcher, std::string& error) {
    if (!win::RegisterBackground(launcher)) {
        error = "Windows refused the sign-in entry (HKCU\\...\\Run)";
        return false;
    }
    if (!win::StartBackground(launcher)) {
        error = "cannot start " + ToUtf8(launcher.wstring());
        return false;
    }
    return true;
}

bool DisableAutoStart(std::string& error) {
    if (!win::UnregisterBackground()) {
        error = "Windows refused to remove the sign-in entry (HKCU\\...\\Run)";
        return false;
    }
    if (!win::StopBackground()) {
        error = "the background launcher did not stop";
        return false;
    }
    return true;
}

bool RestartAutoStart(const std::filesystem::path& launcher, std::string& error) {
    if (!win::StopBackground()) {
        error = "the background launcher did not stop";
        return false;
    }
    if (!win::StartBackground(launcher)) {
        error = "cannot start " + ToUtf8(launcher.wstring());
        return false;
    }
    return true;
}

bool IsInstallDirectory(const std::filesystem::path& directory) {
    std::error_code ec;
    return std::filesystem::equivalent(directory, win::InstallDirectory(), ec);
}

}  // namespace ampify::platform
