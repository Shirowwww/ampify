// Windows only: the helpers of src/win/win_common.cpp that the launcher, the installer and ampify share.
#include <windows.h>
#include <shellapi.h>

#include <iterator>
#include <string>

#include "../src/win/win_common.hpp"
#include "test.hpp"

TEST_CASE(WinCommon_QuotedArgumentsReadBackUnchanged) {
    CHECK(ampify::win::QuoteArgument(L"plain") == L"plain");
    CHECK(ampify::win::QuoteArgument(L"") == L"\"\"");
    const std::wstring arguments[] = {L"two words", L"say \"hi\"", L"C:\\Program Files\\", L"a\\\\\"b", L"", L"tab\there"};
    std::wstring line = L"program.exe";
    for (const std::wstring& argument : arguments) line += L" " + ampify::win::QuoteArgument(argument);

    int count = 0;
    LPWSTR* parsed = CommandLineToArgvW(line.c_str(), &count);
    CHECK(parsed != nullptr && count == 1 + static_cast<int>(std::size(arguments)));
    if (parsed == nullptr) return;
    for (int i = 1; i < count && i <= static_cast<int>(std::size(arguments)); i++) CHECK(parsed[i] == arguments[i - 1]);
    LocalFree(parsed);
}

TEST_CASE(WinCommon_Utf8RoundTrips) {
    const std::string text = "caf\xC3\xA9 \xF0\x9F\x8E\xB5";
    CHECK(ampify::win::FromUtf8(text) == L"caf\u00E9 \U0001F3B5");
    CHECK(ampify::win::ToUtf8(ampify::win::FromUtf8(text)) == text);
    CHECK(ampify::win::ToUtf8(L"").empty());
}

TEST_CASE(WinCommon_ProcessWatchSeesAProgramStartAndEnd) {
    wchar_t temp[MAX_PATH];
    GetTempPathW(MAX_PATH, temp);
    const std::wstring probe = std::wstring(temp) + L"ampify_watch_probe.exe";
    CHECK(CopyFileW(L"C:\\Windows\\System32\\PING.EXE", probe.c_str(), FALSE) != 0);

    ampify::win::ProcessWatch watch(L"ampify_watch_probe.exe");
    ampify::win::ProcessWatch self(L"ampify_tests.exe");
    CHECK(!watch.Running());
    CHECK(self.Running());

    std::wstring line = L"\"" + probe + L"\" -n 30 127.0.0.1";
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION started{};
    CHECK(CreateProcessW(probe.c_str(), line.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
                         &startup, &started) != 0);
    CHECK(watch.Running());
    CHECK(watch.Running());

    TerminateProcess(started.hProcess, 0);
    WaitForSingleObject(started.hProcess, 5000);
    CloseHandle(started.hThread);
    CloseHandle(started.hProcess);
    CHECK(!watch.Running());
    DeleteFileW(probe.c_str());
}

namespace {

constexpr wchar_t kTestKey[] = L"Software\\AmpifyTests\\DisplayVersion";

std::wstring ReadDisplayVersion() {
    wchar_t buffer[64]{};
    DWORD size = sizeof(buffer);
    if (RegGetValueW(HKEY_CURRENT_USER, kTestKey, L"DisplayVersion", RRF_RT_REG_SZ, nullptr, buffer, &size) !=
        ERROR_SUCCESS) {
        return L"<unreadable>";
    }
    return buffer;
}

}  // namespace

TEST_CASE(WinCommon_AnUpdateChangesTheVersionOfAnExistingUninstallEntryOnly) {
    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\AmpifyTests");
    // No entry: the copy was not installed by the installer, so no entry is made for it.
    CHECK(!ampify::win::SetDisplayVersion(kTestKey, L"1.1.0"));
    HKEY probe = nullptr;
    CHECK(RegOpenKeyExW(HKEY_CURRENT_USER, kTestKey, 0, KEY_READ, &probe) != ERROR_SUCCESS);
    if (probe) RegCloseKey(probe);

    HKEY key = nullptr;
    CHECK(RegCreateKeyExW(HKEY_CURRENT_USER, kTestKey, 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr) == ERROR_SUCCESS);
    const std::wstring before = L"1.0.0";
    RegSetValueExW(key, L"DisplayVersion", 0, REG_SZ, reinterpret_cast<const BYTE*>(before.c_str()),
                   static_cast<DWORD>((before.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    CHECK(ReadDisplayVersion() == L"1.0.0");
    CHECK(ampify::win::SetDisplayVersion(kTestKey, L"1.1.0"));
    CHECK(ReadDisplayVersion() == L"1.1.0");
    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\AmpifyTests");
}
