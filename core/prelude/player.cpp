// Module player: AMK.player, the state of the player, its events and its commands. Natives: core/natives/player.cpp.

namespace amk::prelude {

extern const char kPlayer[];
const char kPlayer[] = R"AMKJS(
  const trackListeners = { trackchange: [], statechange: [] };

  AMK.player = {
    state() {
      return JSON.parse(native.playerState());
    },
    on(event, fn) {
      const listeners = trackListeners[event];
      if (!listeners) throw new RangeError("événement inconnu : " + event + " (trackchange ou statechange)");
      if (typeof fn !== "function") throw new TypeError("player.on attend une fonction");
      native.require("player.read");
      listeners.push(fn);
    },
    play: () => native.playerCommand("play", 0),
    pause: () => native.playerCommand("pause", 0),
    next: () => native.playerCommand("next", 0),
    previous: () => native.playerCommand("previous", 0),
    seek(ms) {
      const position = Number(ms);
      if (!Number.isFinite(position) || position < 0) throw new RangeError("seek attend un nombre de millisecondes positif");
      return native.playerCommand("seek", Math.floor(position));
    },
  };

  dispatch.player = (event, stateJson) => {
    for (const fn of trackListeners[event]) runGuarded(event, fn, [JSON.parse(stateJson)]);
  };
)AMKJS";

}  // namespace amk::prelude
