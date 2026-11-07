#include "permissions.hpp"

#include <algorithm>

namespace ampify {

namespace {

bool IsHostName(std::string_view host) {
    constexpr size_t kMaxHost = 253;
    if (host.empty() || host.size() > kMaxHost) return false;
    return std::all_of(host.begin(), host.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '-';
    });
}

}  // namespace

const PermissionInfo* FindPermission(std::string_view name) {
    for (const PermissionInfo& permission : kPermissions) {
        if (permission.name == name) return &permission;
    }
    return nullptr;
}

bool IsKnownPermission(std::string_view permission) {
    if (FindPermission(permission)) return true;
    return permission.substr(0, kNetworkPermissionPrefix.size()) == kNetworkPermissionPrefix &&
           IsHostName(permission.substr(kNetworkPermissionPrefix.size()));
}

const char* ServiceSettingOf(std::string_view permission) {
    const PermissionInfo* found = FindPermission(permission);
    return found && !found->serviceSetting.empty() ? found->serviceSetting.data() : nullptr;
}

}  // namespace ampify
