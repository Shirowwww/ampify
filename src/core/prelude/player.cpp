// Module player: Ampify.player, the state of the player, its events, its commands and its queue. Natives:
// src/core/natives/player.cpp.

namespace ampify::prelude {

extern const char kPlayer[];
const char kPlayer[] = R"AMPIFYJS(
  const trackListeners = { trackchange: [], statechange: [], lyricschange: [], progress: [] };
  // Spicetify's names for three of the events, so that ported extensions keep working.
  const eventAliases = { songchange: "trackchange", onplaypause: "statechange", onprogress: "progress" };
  const hasOwn = (object, key) => Object.prototype.hasOwnProperty.call(object, key);
  const eventKey = (event) => {
    const key = hasOwn(eventAliases, event) ? eventAliases[event] : event;
    if (!hasOwn(trackListeners, key)) {
      throw new RangeError("unknown event: " + event +
        " (trackchange, statechange, lyricschange, progress, songchange, onplaypause or onprogress)");
    }
    return key;
  };
  // Apple's volume slider is in decibels: its position p (0 to 1) gives 30 x (p - 1) dB (src/core/player_modes.hpp).
  // Volume steps and mute work on the slider's position, as the user sees it, not on the gain.
  const SLIDER_RANGE_DB = 30;
  const sliderOf = (gain) => (gain > 0 ? Math.min(1, Math.max(0, 1 + 20 * Math.log10(gain) / SLIDER_RANGE_DB)) : 0);
  const gainOf = (slider) => (slider <= 0 ? 0 : Math.pow(10, SLIDER_RANGE_DB * (Math.min(1, slider) - 1) / 20));
  const MUTED_BELOW = 0.001;   // slider position: at its bottom
  const UNMUTE_SLIDER = 0.5;   // where unmute goes when the level before the mute is not known
  const DEFAULT_SKIP_MS = 15000;
  let volumeBeforeMute = null;
  const sliderNow = () => {
    const gain = native.audioVolume();
    return typeof gain === "number" ? sliderOf(gain) : null;
  };
  const volumeStep = (step, sign) => {
    const amount = step === undefined ? 0.1 : Number(step);
    if (!Number.isFinite(amount) || amount <= 0 || amount > 1) throw new RangeError("the volume step is a number above 0, up to 1");
    const slider = sliderNow();
    return slider === null ? false : Ampify.player.setVolume(gainOf(Math.max(0, slider + sign * amount)));
  };
  const skip = (ms, sign) => {
    const amount = ms === undefined ? DEFAULT_SKIP_MS : Number(ms);
    if (!Number.isFinite(amount) || amount < 0) throw new RangeError("skip expects a positive number of milliseconds");
    const state = Ampify.player.state();
    if (!state.track) return false;
    const target = state.positionMs + sign * amount;
    const end = state.durationMs > 0 ? state.durationMs : target;
    return Ampify.player.seek(Math.max(0, Math.min(target, end)));
  };
  const formatTime = (ms) => {
    const value = Number(ms);
    if (!Number.isFinite(value)) throw new RangeError("formatTime expects a number of milliseconds");
    const total = Math.floor(Math.abs(value) / 1000);
    const seconds = String(total % 60).padStart(2, "0");
    const minutes = Math.floor(total / 60) % 60;
    const hours = Math.floor(total / 3600);
    const sign = value < 0 && total > 0 ? "-" : "";
    return sign + (hours > 0 ? hours + ":" + String(minutes).padStart(2, "0") : String(minutes)) + ":" + seconds;
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
    skipForward: (ms) => skip(ms, 1),
    skipBack: (ms) => skip(ms, -1),
    // 0 to 1, how far the song has gone; 0 while its length is not known.
    progress() {
      const state = Ampify.player.state();
      return state.durationMs > 0 ? Math.min(1, state.positionMs / state.durationMs) : 0;
    },
    formatTime,
    // Mute is Apple's slider at its bottom; unmuting puts back the level this element muted from.
    muted() {
      const slider = sliderNow();
      return slider === null ? null : slider < MUTED_BELOW;
    },
    setMute(on) {
      const slider = sliderNow();
      if (slider === null) return false;
      if (on) {
        if (slider >= MUTED_BELOW) volumeBeforeMute = gainOf(slider);
        return Ampify.player.setVolume(0);
      }
      if (slider >= MUTED_BELOW) return true;
      const back = volumeBeforeMute === null ? gainOf(UNMUTE_SLIDER) : volumeBeforeMute;
      volumeBeforeMute = null;
      return Ampify.player.setVolume(back);
    },
    toggleMute() {
      const muted = Ampify.player.muted();
      return muted === null ? false : Ampify.player.setMute(!muted);
    },
    increaseVolume: (step) => volumeStep(step, 1),
    decreaseVolume: (step) => volumeStep(step, -1),
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
