// Module core: AMK.version, kitApi, process, locale, log and onStop. Natives: core/natives/core.cpp.

namespace amk::prelude {

extern const char kCore[];
const char kCore[] = R"AMKJS(
  const stopHandlers = [];

  Object.assign(AMK, {
    version: native.version,
    kitApi: native.kitApi,
    process: native.process,
    locale: native.locale,
    log: (message) => native.log(text(message)),
    onStop(fn) {
      if (typeof fn !== "function") throw new TypeError("onStop attend une fonction");
      stopHandlers.push(fn);
    },
  });

  dispatch.stop = () => {
    for (let i = stopHandlers.length - 1; i >= 0; i--) runGuarded("arrêt", stopHandlers[i], []);
  };
)AMKJS";

}  // namespace amk::prelude
