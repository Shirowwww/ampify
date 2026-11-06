// Module core: Ampify.version, kitApi, process, locale, appVersion, activeTheme, log and onStop. Natives: src/core/natives/core.cpp.

namespace ampify::prelude {

extern const char kCore[];
const char kCore[] = R"AMPIFYJS(
  const stopHandlers = [];

  Object.assign(Ampify, {
    version: native.version,
    kitApi: native.kitApi,
    process: native.process,
    locale: native.locale,
    // Apple Music's version, "" when unknown, and the id of the theme in use, null for none.
    appVersion: native.appVersion,
    activeTheme: () => native.activeTheme() || null,
    log: (message) => native.log(text(message)),
    onStop(fn) {
      if (typeof fn !== "function") throw new TypeError("onStop expects a function");
      stopHandlers.push(fn);
    },
  });

  dispatch.stop = () => {
    for (let i = stopHandlers.length - 1; i >= 0; i--) runGuarded("stop", stopHandlers[i], []);
  };
)AMPIFYJS";

}  // namespace ampify::prelude
