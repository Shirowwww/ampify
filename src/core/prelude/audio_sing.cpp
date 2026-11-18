// Module audioSing: adds Ampify.audio.sing, the karaoke mode: the interface or the engine asks for it, the engine
// does the work and says how far it is. Natives: src/core/natives/sing.cpp. Works from both processes.

namespace ampify::prelude {

extern const char kAudioSing[];
const char kAudioSing[] = R"AMPIFYJS(
  const SING_POLL_MS = 250;
  const singListeners = [];
  let singTimer = null;
  let singLast = "";

  const singState = () => JSON.parse(native.singState());

  // One poll: the listeners hear the state only when it moved.
  function singPoll() {
    const json = native.singState();
    if (json === singLast) return;
    singLast = json;
    const state = JSON.parse(json);
    for (const fn of singListeners.slice()) runGuarded("audio.sing.onChange", fn, [state]);
  }

  const sing = {
    set(options) {
      const asked = options === null || typeof options !== "object" ? {} : options;
      const missing = asked.voice === undefined || asked.model === undefined || asked.music === undefined;
      const now = missing ? singState() : null;
      const voice = asked.voice === undefined ? now.voice : Number(asked.voice);
      const model = asked.model === undefined ? now.model : String(asked.model);
      const music = asked.music === undefined ? now.music : Number(asked.music);
      if (typeof asked.mode !== "string") throw new TypeError("sing.set expects { mode: \"off\" | \"light\" | \"studio\", voice: 0..1, music: 0..1, model: \"standard\" | \"precise\" }");
      return native.singSet(asked.mode, voice, model, music);
    },
    state() {
      return singState();
    },
    // Returns the function that stops listening.
    onChange(fn) {
      if (typeof fn !== "function") throw new TypeError("audio.sing.onChange expects a function");
      singState();  // throws PermissionDenied here rather than from the timer
      singListeners.push(fn);
      if (singTimer === null) singTimer = setInterval(singPoll, SING_POLL_MS);
      return () => {
        const at = singListeners.indexOf(fn);
        if (at >= 0) singListeners.splice(at, 1);
        if (singListeners.length === 0 && singTimer !== null) {
          clearInterval(singTimer);
          singTimer = null;
        }
      };
    },
  };
  Ampify.audio.sing = Object.freeze(sing);
)AMPIFYJS";

}  // namespace ampify::prelude
