#pragma once

// The optional host service behind Ampify.keyboard.unbind (src/core/host.hpp says how a capability works). A host
// without it keeps the shortcut registered until the element stops; the element's handler goes all the same.

#include <string>
#include <string_view>

namespace ampify {

struct KeyUnbindHost {
    static constexpr std::string_view kCapabilityName = "keyboard.unbind";
    virtual ~KeyUnbindHost() = default;

    // Gives back one shortcut the element registered, in its normalized form. False when it had not registered it.
    virtual bool UnregisterKey(const std::string& elementId, const std::string& combo) = 0;
};

}  // namespace ampify
