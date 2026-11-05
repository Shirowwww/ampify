// Module player: Ampify.player, the state of the player, its events, its commands and its queue. Natives:
// src/core/natives/player.cpp.

namespace ampify::prelude {

extern const char kPlayer[];
const char kPlayer[] = R"AMPIFYJS(
  const trackListeners = { trackchange: [], statechange: [], lyricschange: [] };
  // Spicetify's names for two of the events, so that ported extensions keep working.
  const eventAliases = { songchange: "trackchange", onplaypause: "statechange" };
  const hasOwn = (object, key) => Object.prototype.hasOwnProperty.call(object, key);
  const eventKey = (event) => {
    const key = hasOwn(eventAliases, event) ? eventAliases[event] : event;
    if (!hasOwn(trackListeners, key)) {
      throw new RangeError("unknown event: " + event + " (trackchange, statechange, lyricschange, songchange or onplaypause)");
    }
    return key;
  };
  const queueId = (value, what) => {
    if (typeof value !== "string" && typeof value !== "number") throw new TypeError(what);
    return String(value);
  };

  Ampify.player = {
    state() {
      return JSON.parse(native.playerState());
    },
    on(event, fn) {
      const listeners = trackListeners[eventKey(event)];
      if (typeof fn !== "function") throw new TypeError("player.on expects a function");
      native.require("player.read");
      listeners.push(fn);
    },
    // Removes a listener added with on, the same function. Nothing happens when it was not added.
    off(event, fn) {
      const listeners = trackListeners[eventKey(event)];
      const at = listeners.indexOf(fn);
      if (at >= 0) listeners.splice(at, 1);
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
    // Apple's shuffle and repeat buttons (interface process): true | false | null, and "off" | "all" | "one" | null.
    shuffle: () => JSON.parse(native.playerModes()).shuffle,
    repeat: () => JSON.parse(native.playerModes()).repeat,
    setShuffle: (on) => native.playerSetShuffle(on ? 1 : 0),
    toggleShuffle() {
      const on = Ampify.player.shuffle();
      return on === null ? false : native.playerSetShuffle(on ? 0 : 1);
    },
    setRepeat(mode) {
      if (mode !== "off" && mode !== "all" && mode !== "one") {
        throw new RangeError("setRepeat expects \"off\", \"all\" or \"one\"");
      }
      return native.playerSetRepeat(mode);
    },
    // off, then all, then one, then off again, as Apple's own button.
    toggleRepeat() {
      const mode = Ampify.player.repeat();
      return mode === null ? false : native.playerSetRepeat(mode === "off" ? "all" : mode === "all" ? "one" : "off");
    },
    // Apple's player volume, 0 to 1, once a song has started. From the interface process it moves Apple's own slider.
    setVolume(volume) {
      const value = Number(volume);
      if (!Number.isFinite(value) || value < 0 || value > 1) throw new RangeError("setVolume expects a number from 0 to 1");
      return native.playerSetVolume(value);
    },
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
    // A copy: a listener may remove itself or another while the event is delivered.
    for (const fn of trackListeners[event].slice()) runGuarded(event, fn, [JSON.parse(stateJson)]);
  };
)AMPIFYJS";

}  // namespace ampify::prelude
