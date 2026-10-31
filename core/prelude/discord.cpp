// Module discord: Ampify.discord, the Rich Presence shown in the Discord client. Natives: core/natives/discord.cpp.

namespace ampify::prelude {

extern const char kDiscord[];
const char kDiscord[] = R"AMPIFYJS(
  Ampify.discord = {
    // activity: { clientId, details, state, largeImage, largeText, smallImage, smallText, startMs, endMs }.
    // Returns false when Discord Rich Presence is off (setting discord.enabled) or not offered by this host.
    setActivity(activity) {
      if (typeof activity !== "object" || activity === null) throw new TypeError("discord.setActivity expects an object");
      return native.discordSet(JSON.stringify(activity));
    },
    clear() {
      native.discordClear();
    },
  };
)AMPIFYJS";

}  // namespace ampify::prelude
