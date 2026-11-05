#include "package_debug.hpp"

#include <objbase.h>
#include <shobjidl.h>

#include <cwctype>
#include <optional>
#include <string_view>
#include <vector>

#include "../core/apple_music.hpp"
#include "package.hpp"
#include "win_common.hpp"

namespace ampify::win {

namespace {

// COM for the length of one call, in whatever apartment the thread already has. Uninitialised only if initialised
// here: an unbalanced CoUninitialize tears COM down under the caller.
class ComScope {
public:
    ComScope() : m_hr(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)) {}
    ~ComScope() {
        if (SUCCEEDED(m_hr)) CoUninitialize();
    }
    ComScope(const ComScope&) = delete;
    ComScope& operator=(const ComScope&) = delete;

private:
    HRESULT m_hr;
};

template <typename Call>
HRESULT WithSettings(Call call) {
    ComScope com;
    IPackageDebugSettings* settings = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_PackageDebugSettings, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&settings));
    if (FAILED(hr)) return hr;
    hr = call(*settings);
    settings->Release();
    return hr;
}

std::wstring Lower(std::wstring_view text) {
    std::wstring lower(text);
    for (wchar_t& c : lower) c = static_cast<wchar_t>(std::towlower(c));
    return lower;
}

// True when a DebugPath value under `key` (or a sub key, a few levels down) names `needle`.
bool FindDebugPath(HKEY key, const std::wstring& needle, int depth) {
    DWORD size = 0;
    if (RegGetValueW(key, nullptr, L"DebugPath", RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND, nullptr, nullptr,
                     &size) == ERROR_SUCCESS &&
        size > 0) {
        std::wstring value(size / sizeof(wchar_t), L'\0');
        if (RegGetValueW(key, nullptr, L"DebugPath", RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND, nullptr,
                         value.data(), &size) == ERROR_SUCCESS &&
            Lower(value).find(needle) != std::wstring::npos) {
            return true;
        }
    }
    if (depth == 0) return false;
    for (DWORD index = 0;; index++) {
        wchar_t name[256];
        DWORD length = static_cast<DWORD>(std::size(name));
        if (RegEnumKeyExW(key, index, name, &length, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
        HKEY child = nullptr;
        if (RegOpenKeyExW(key, name, 0, KEY_READ, &child) != ERROR_SUCCESS) continue;
        bool found = FindDebugPath(child, needle, depth - 1);
        RegCloseKey(child);
        if (found) return true;
    }
    return false;
}

}  // namespace

HRESULT EnableAttach(const std::wstring& fullName, const std::filesystem::path& launcher) {
    std::wstring command = L"\"" + launcher.wstring() + L"\" " + kAttachArgument;
    return WithSettings([&](IPackageDebugSettings& settings) {
        return settings.EnableDebugging(fullName.c_str(), command.c_str(), nullptr);
    });
}

HRESULT DisableAttach(const std::wstring& fullName) {
    return WithSettings([&](IPackageDebugSettings& settings) { return settings.DisableDebugging(fullName.c_str()); });
}

bool AttachRegistered(const std::wstring& fullName, const std::filesystem::path& launcher) {
    if (fullName.empty()) return false;
    std::wstring path = L"Software\\Classes\\PackagesDebug\\" + fullName;
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, KEY_READ, &key) != ERROR_SUCCESS) return false;
    constexpr int kDepth = 3;  // <full name>\<app id>\<image>\DebugPath, as seen on Windows 11 24H2
    bool found = FindDebugPath(key, Lower(launcher.wstring()), kDepth);
    RegCloseKey(key);
    return found;
}

std::wstring AppleMusicFullName() {
    std::wstring family = FromUtf8(ampify::kPackageFamilyName);
    std::optional<ampify::launcher::InstalledPackage> package = ampify::launcher::FindInstalledPackage(family.c_str());
    return package ? package->fullName : std::wstring();
}

bool ParseAttachArguments(int argc, wchar_t** argv, DWORD& processId, DWORD& threadId) {
    processId = 0;
    threadId = 0;
    for (int i = 1; argv && i + 1 < argc; i++) {
        std::wstring_view name = argv[i];
        DWORD value = wcstoul(argv[i + 1], nullptr, 10);
        if (name == L"-p") processId = value;
        if (name == L"-tid") threadId = value;
    }
    return processId != 0 && threadId != 0;
}

}  // namespace ampify::win
