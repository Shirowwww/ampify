// Module player: Ampify.player, the state of the player, its events, its commands and its queue. Natives:
// src/core/natives/player.cpp.

namespace ampify::prelude {

extern const char kPlayer[];
const char kPlayer[] = R"AMPIFYJS(
  const trackListeners = { trackchange: [], statechange: [], lyricschange: [] };
  const queueId = (value, what) => {
    if (typeof value !== "string" && typeof value !== "number") throw new TypeError(what);
    return String(value);
  };

  Ampify.player = {
    state() {
      return JSON.parse(native.playerState());
    },
    on(event, fn) {
      const listeners = trackListeners[event];
      if (!listeners) throw new RangeError("unknown event: " + event + " (trackchange, statechange or lyricschange)");
      if (typeof fn !== "function") throw new TypeError("player.on expects a function");
      native.require("player.read");
      listeners.push(fn);
    },
    play: () => native.playerCommand("play", 0),
    pause: () => native.playerCommand("pause", 0),
    next: () => native.playerCommand("next", 0),
    previous: () => native.playerCommand("previous", 0),
    seek(ms) {
      const position = Number(ms);
      if (!Number.isFinite(position) || position < 0) throw new RangeError("seek expects a positive number of milliseconds");
      return native.playerCommand("seek", Math.floor(position));
    },
    toggle: () => native.playerCommand("toggle", 0),
    lyrics() {
      return JSON.parse(native.playerLyrics());
    },
    // The cover of what plays now, files of one's own included: { type: "image/jpeg", base64 }, or null.
    artwork() {
      return dispatch.request(() => native.playerArtwork()).then((answer) =>
        answer.status === 200 ? JSON.parse(answer.text) : null);
    },
    queue: Object.freeze({
      list() {
        return JSON.parse(native.queueList());
      },
      add(trackId, options) {
        const position = options && options.position !== undefined ? String(options.position) : "";
        return native.queueAdd(queueId(trackId, "queue.add expects a track id"), position);
      },
      remove(itemId) {
        return native.queueRemove(queueId(itemId, "queue.remove expects the id of a queue entry"));
      },
    }),
  };

  // The same lyrics as a promise, the shape Spicetify's lyrics extensions wait for. Without player.read it rejects.
  Ampify.lyrics = Object.freeze({
    current: () => Promise.resolve().then(() => Ampify.player.lyrics()),
  });

  dispatch.player = (event, stateJson) => {
    for (const fn of trackListeners[event]) runGuarded(event, fn, [JSON.parse(stateJson)]);
  };
)AMPIFYJS";

}  // namespace ampify::prelude
