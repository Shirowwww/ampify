// Module timers: the globals setTimeout, setInterval, clearTimeout, clearInterval and queueMicrotask.
// Natives: core/natives/timers.cpp. ScriptExtension::Pump fires the timers that are due.

namespace ampify::prelude {

extern const char kTimers[];
const char kTimers[] = R"AMPIFYJS(
  const timers = new Map();
  let nextTimerId = 1;

  function schedule(fn, ms, args, repeat) {
    if (typeof fn !== "function") throw new TypeError("function expected");
    const delay = Math.max(0, Math.min(Number(ms) || 0, 86400000));
    const id = nextTimerId++;
    timers.set(id, { fn, args, delay, repeat });
    native.schedule(id, delay);
    return id;
  }

  function cancel(id) {
    timers.delete(id);
    native.cancel(id);
  }

  globalThis.setTimeout = (fn, ms, ...args) => schedule(fn, ms, args, false);
  globalThis.setInterval = (fn, ms, ...args) => schedule(fn, ms, args, true);
  globalThis.clearTimeout = cancel;
  globalThis.clearInterval = cancel;
  globalThis.queueMicrotask = (fn) => {
    Promise.resolve().then(() => runGuarded("microtask", fn, []));
  };

  dispatch.timer = (id) => {
    const entry = timers.get(id);
    if (!entry) return;
    if (entry.repeat) {
      native.schedule(id, entry.delay);  // before the call, so that clearInterval inside it cancels it
    } else {
      timers.delete(id);
    }
    runGuarded("timer", entry.fn, entry.args);
  };
)AMPIFYJS";

}  // namespace ampify::prelude
