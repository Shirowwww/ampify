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
