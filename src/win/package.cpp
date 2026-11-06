#include "package.hpp"

#include <appmodel.h>
#include <shobjidl.h>

#include <string>
#include <vector>

#include "../core/apple_music.hpp"
#include "../core/text_util.hpp"

namespace ampify::launcher {


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
        std::string version = ampify::AppleMusicVersion(std::string(candidate.begin(), candidate.end()));
        if (fullName.empty() || ampify::CompareDottedVersions(version, newestVersion) > 0) {
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

HRESULT ActivateApp(const wchar_t* appUserModelId, DWORD* processId, const std::wstring& arguments) {
    IApplicationActivationManager* manager = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_ApplicationActivationManager, nullptr, CLSCTX_LOCAL_SERVER,
                                  IID_PPV_ARGS(&manager));
    if (FAILED(hr)) return hr;

    hr = manager->ActivateApplication(appUserModelId, arguments.empty() ? nullptr : arguments.c_str(), AO_NONE,
                                      processId);
    manager->Release();
    return hr;
}

}  // namespace ampify::launcher
