#pragma once

// The optional host service behind AMK.remote. The Windows host runs the local remote control (setting
// remote.enabled, 127.0.0.1 only, token required, win/remote_server.*). An element may add its own state to the
// answer of GET /status, under "extensions": {"<id>": ...}. It cannot receive requests.

#include <string>
#include <string_view>

namespace amk {

inline constexpr size_t kMaxRemoteStateBytes = 4096;

struct RemoteHost {
    static constexpr std::string_view kCapabilityName = "remote";
    virtual ~RemoteHost() = default;

    // `json` is an object, at most kMaxRemoteStateBytes. False when the remote control is off.
    virtual bool PublishRemoteState(const std::string& elementId, const std::string& json) = 0;
    virtual void RemoveRemoteState(const std::string& elementId) = 0;
};

}  // namespace amk
