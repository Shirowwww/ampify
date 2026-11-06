#pragma once

// The optional host service behind Ampify.appVersion and Ampify.activeTheme() (src/core/host.hpp says how a
// capability works). A host without it gives an empty version and no theme.

#include <string>
#include <string_view>

namespace ampify {

struct AppInfoHost {
    static constexpr std::string_view kCapabilityName = "app.info";
    virtual ~AppInfoHost() = default;

    // Apple Music's version, such as "1.1792.23042.0", or empty when its package is not known.
    virtual std::string AppVersion() = 0;
    // The id of the theme in use (theme.active), or empty for none.
    virtual std::string ActiveTheme() = 0;
};

}  // namespace ampify
