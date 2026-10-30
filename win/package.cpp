#include "package.hpp"

#include <appmodel.h>
#include <shobjidl.h>

#include <string>
#include <vector>

#include "../core/apple_music.hpp"

namespace amk::launcher {

namespace {

// MinGW does not ship packagedebugsettings.h, so the interface is declared here. The Windows SDK lists
// EnableDebugging and DisableDebugging first after IUnknown; only DisableDebugging is called.
constexpr GUID kClsidPackageDebugSettings = {0xB1AEC16F, 0x2383, 0x4852, {0xB0, 0xE9, 0x8F, 0x0B, 0x1D, 0xC6, 0x6B, 0x4D}};
constexpr GUID kIidPackageDebugSettings = {0xF27C3930, 0x8029, 0x4AD1, {0x94, 0xE3, 0x3D, 0xBA, 0x41, 0x78, 0x10, 0xC1}};

struct IPackageDebugSettingsLite : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE EnableDebugging(PCWSTR packageFullName, PWSTR debuggerCommandLine,
                                                      PVOID environment) = 0;
    virtual HRESULT STDMETHODCALLTYPE DisableDebugging(PCWSTR packageFullName) = 0;
};

}  // namespace

std::optional<InstalledPackage> FindInstalledPackage(const wchar_t* familyName) {
    UINT32 count = 0;
    UINT32 bufferLength = 0;
    if (GetPackagesByPackageFamily(familyName, &count, nullptr, &bufferLength, nullptr) != ERROR_INSUFFICIENT_BUFFER ||
        count == 0) {
        return std::nullopt;
    }

    std::vector<PWSTR> names(count);
    std::vector<wchar_t> buffer(bufferLength);
    if (GetPackagesByPackageFamily(familyName, &count, names.data(), &bufferLength, buffer.data()) != ERROR_SUCCESS ||
        count == 0) {
        return std::nullopt;
    }

    // During an update the old and the new version can both be listed. The newest one is the one this launch uses.
    std::wstring fullName;
    std::string newestVersion;
    for (UINT32 i = 0; i < count; ++i) {
        std::wstring candidate = names[i];
        std::string version = amk::AppleMusicVersion(std::string(candidate.begin(), candidate.end()));
        if (fullName.empty() || amk::CompareVersions(version, newestVersion) > 0) {
            fullName = candidate;
            newestVersion = version;
        }
    }
    InstalledPackage package;
    package.fullName = fullName;

    UINT32 pathLength = 0;
    if (GetPackagePathByFullName(package.fullName.c_str(), &pathLength, nullptr) != ERROR_INSUFFICIENT_BUFFER) {
        return std::nullopt;
    }
    std::vector<wchar_t> path(pathLength);
    if (GetPackagePathByFullName(package.fullName.c_str(), &pathLength, path.data()) != ERROR_SUCCESS) {
        return std::nullopt;
    }
    package.directory = path.data();
    return package;
}

HRESULT ActivateApp(const wchar_t* appUserModelId, DWORD* processId) {
    IApplicationActivationManager* manager = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_ApplicationActivationManager, nullptr, CLSCTX_LOCAL_SERVER,
                                  IID_PPV_ARGS(&manager));
    if (FAILED(hr)) return hr;

    hr = manager->ActivateApplication(appUserModelId, nullptr, AO_NONE, processId);
    manager->Release();
    return hr;
}

HRESULT DisablePackageDebugging(const std::wstring& packageFullName) {
    IPackageDebugSettingsLite* settings = nullptr;
    HRESULT hr = CoCreateInstance(kClsidPackageDebugSettings, nullptr, CLSCTX_ALL, kIidPackageDebugSettings,
                                  reinterpret_cast<void**>(&settings));
    if (FAILED(hr)) return hr;

    hr = settings->DisableDebugging(packageFullName.c_str());
    settings->Release();
    return hr;
}

}  // namespace amk::launcher
