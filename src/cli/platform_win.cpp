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
#include <shobjidl.h>

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

bool CursorPosition(int& x, int& y) {
    // Apple Music reads the place in physical pixels: a process that is not DPI aware would get scaled ones on a screen
    // set above 100 %. Refused (and harmless) when the process already has an awareness.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    POINT point{};
    if (!GetCursorPos(&point)) return false;
    x = static_cast<int>(point.x);
    y = static_cast<int>(point.y);
    return true;
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

bool EndProcesses(const std::string& imageName) {
    constexpr DWORD kEndWaitMs = 5000;
    for (DWORD id : win::ProcessIdsNamed(FromUtf8(imageName).c_str())) {
        HANDLE process = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, id);
        if (!process) continue;
        if (TerminateProcess(process, 1)) WaitForSingleObject(process, kEndWaitMs);
        CloseHandle(process);
    }
    return win::ProcessIdsNamed(FromUtf8(imageName).c_str()).empty();
}

std::string UserLocale() {
    wchar_t name[LOCALE_NAME_MAX_LENGTH] = {};
    if (GetUserDefaultLocaleName(name, LOCALE_NAME_MAX_LENGTH) == 0) return "en";
    return ToUtf8(name);
}

bool AskYesNo(const std::string& title, const std::string& text) {
    // Not forced to the front: it may come while a game runs, and then waits in the taskbar.
    UINT style = MB_YESNO | MB_ICONQUESTION;
    return MessageBoxW(nullptr, FromUtf8(text).c_str(), FromUtf8(title).c_str(), style) == IDYES;
}

void ShowNotice(const std::string& title, const std::string& text) {
    // Windows shows the balloon as a notification; the icon must stay a moment, or the notification goes with it.
    constexpr DWORD kNoticeShownMs = 8000;
    // Without an id of its own, a balloon from a console run is filed under the console's program (PowerShell), which
    // then shows Ampify's logo on its own notifications. Windows shows this id as the notification's header.
    SetCurrentProcessExplicitAppUserModelID(L"Ampify");
    HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof windowClass;
    windowClass.lpfnWndProc = DefWindowProcW;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = L"AmpifyUpdateNotice";
    RegisterClassExW(&windowClass);
    HWND window = CreateWindowExW(0, windowClass.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, instance,
                                  nullptr);
    if (!window) return;
    NOTIFYICONDATAW icon{};
    icon.cbSize = sizeof icon;
    icon.hWnd = window;
    icon.uID = 1;
    icon.uFlags = NIF_ICON | NIF_TIP;
    icon.hIcon = win::AmpifyIcon(false);
    wcsncpy_s(icon.szTip, L"Ampify", _TRUNCATE);
    if (Shell_NotifyIconW(NIM_ADD, &icon)) {
        // The balloon goes in a second call, on an icon the shell already shows.
        icon.uFlags = NIF_INFO;
        icon.dwInfoFlags = NIIF_USER | NIIF_LARGE_ICON;
        icon.hBalloonIcon = win::AmpifyIcon(true);
        wcsncpy_s(icon.szInfoTitle, FromUtf8(title).c_str(), _TRUNCATE);
        wcsncpy_s(icon.szInfo, FromUtf8(text).c_str(), _TRUNCATE);
        Shell_NotifyIconW(NIM_MODIFY, &icon);
        Sleep(kNoticeShownMs);
        Shell_NotifyIconW(NIM_DELETE, &icon);
    }
    DestroyWindow(window);
}

}  // namespace ampify::platform
