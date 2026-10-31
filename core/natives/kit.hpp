#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace ampify {

// Runs the kit's own command line (ampify.exe, beside the kernel) for Ampify.kit.run: the way the Marketplace page installs,
// removes and chooses, through the same checks as in a console. Offered by the Windows host, absent elsewhere.
struct KitCommandHost {
    static constexpr std::string_view kCapabilityName = "kit.command";
    using Done = std::function<void(int code, const std::string& out, const std::string& err)>;

    virtual ~KitCommandHost() = default;

    // Runs ampify with `arguments` on a thread of its own, and calls `done` once from that thread, unless Forget(owner)
    // ran first. A run that lasts more than five minutes is stopped and reports code -1.
    virtual void RunKitCommand(const std::string& owner, const std::vector<std::string>& arguments, Done done) = 0;

    // No `done` of `owner` runs once this returns: the element is stopping.
    virtual void Forget(const std::string& owner) = 0;
};

// The commands Ampify.kit.run may start, whatever the element asks: the Marketplace's own, nothing that touches the
// kit's files outside the elements (update, restore, export...).
bool IsKitCommandAllowed(const std::vector<std::string>& arguments);

}  // namespace ampify
