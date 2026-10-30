// Module discord: AMK.discord, the Rich Presence shown in the Discord client. Natives: core/natives/discord.cpp.

namespace amk::prelude {

extern const char kDiscord[];
const char kDiscord[] = R"AMKJS(
  AMK.discord = {
    // activity: { clientId, details, state, largeImage, largeText, smallImage, smallText, startMs, endMs }.
    // Returns false when Discord Rich Presence is off (setting discord.enabled) or not offered by this host.
    setActivity(activity) {
      if (typeof activity !== "object" || activity === null) throw new TypeError("discord.setActivity attend un objet");
      return native.discordSet(JSON.stringify(activity));
    },
    clear() {
      native.discordClear();
    },
  };
)AMKJS";

}  // namespace amk::prelude
