#pragma once

// The optional host service behind Ampify.discord (permission ipc.discord, setting discord.enabled). The Windows host
// talks to the Discord client through \\.\pipe\discord-ipc-<n> (win/discord_ipc.*).

#include <string>
#include <string_view>

#include "../discord_frame.hpp"

namespace ampify {

struct DiscordHost {
    static constexpr std::string_view kCapabilityName = "discord";
    virtual ~DiscordHost() = default;

    // Shows the activity of an element, under the Discord application `clientId` (17 to 20 digits). The call
    // returns at once: the connection, and a new try when Discord is closed, happen on the host's own thread. The
    // last element to call wins. False when the host cannot take it.
    virtual bool SetActivity(const std::string& elementId, const std::string& clientId,
                             const discord::Activity& activity) = 0;

    // Clears the presence if this element set it. Called by Ampify.discord.clear and when the element stops.
    virtual void ClearActivity(const std::string& elementId) = 0;
};

}  // namespace ampify
