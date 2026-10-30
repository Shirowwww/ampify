// Windows side of platform.hpp. Programs are started with CreateProcessW, never through cmd.exe, so a path or a
// URL with quotes or ampersands cannot run anything else.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

#include "platform.hpp"

namespace amk::platform {

namespace {

std::wstring ToWide(const std::string& text) {
    if (text.empty()) return {};
    int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring wide(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), size);
    return wide;
}

std::string ToUtf8(const std::wstring& wide) {
    if (wide.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr,
                                   nullptr);
    std::string text(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), text.data(), size, nullptr, nullptr);
    return text;
}

// The quoting rules that CommandLineToArgvW reads back: backslashes are doubled only before a quote.
std::wstring QuoteArgument(const std::wstring& argument) {
    if (!argument.empty() && argument.find_first_of(L" \t\"") == std::wstring::npos) return argument;

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

std::wstring CommandLineOf(const std::vector<std::string>& command) {
    std::wstring line;
    for (const std::string& part : command) {
        if (!line.empty()) line += L' ';
        line += QuoteArgument(ToWide(part));
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
    if (profile == nullptr || *profile == L'\0') return std::filesystem::current_path() / "AppleMusicModKit";
    return std::filesystem::path(profile) / L"AppleMusicModKit";
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

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    std::wstring line = CommandLineOf(command);
    std::vector<wchar_t> buffer(line.begin(), line.end());
    buffer.push_back(L'\0');

    PROCESS_INFORMATION process{};
    BOOL created = CreateProcessW(nullptr, buffer.data(), nullptr, nullptr, FALSE, CREATE_NEW_PROCESS_GROUP, nullptr,
                                  nullptr, &startup, &process);
    if (!created) return false;
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
}

bool IsProcessRunning(const std::string& imageName) {
    std::filesystem::path listing = TemporaryFile("amk-tasklist");
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

}  // namespace amk::platform
