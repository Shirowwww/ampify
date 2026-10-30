// The frame of the prelude: what runs before the modules and after them. The prelude is the JavaScript side of the
// AMK object. It runs in every extension's runtime, before main.js, and it is part of the kit: its closure holds the
// only reference to `native`, the bridge to the C++ kernel, so an extension can use the API but cannot reach the
// bridge itself. Each permission check happens in C++ (core/natives), so this code can be read without trusting it.
// core/api/modules.cpp assembles kFrameHead, one block per module, then kFrameTail. API contract:
// docs/extensions/API.md.

namespace amk::prelude {

extern const char kFrameHead[];
const char kFrameHead[] = R"AMKJS((function (native) {
  "use strict";

  // ---- Shared by every module. Each module runs in its own block, so its own constants stay private to it. ---

  const text = (value) => (typeof value === "string" ? value : String(value));

  function describe(value) {
    if (value instanceof Error) return value.name + ": " + value.message;
    if (typeof value !== "object" || value === null) return String(value);
    try {
      return JSON.stringify(value);
    } catch (error) {
      return String(value);
    }
  }

  // A callback that throws is logged and does not stop the others.
  function runGuarded(where, fn, args) {
    try {
      return fn(...args);
    } catch (error) {
      native.log("[erreur] " + where + " : " + describe(error));
    }
    return undefined;
  }

  // The API object that the modules fill in, and the entry points that the kernel calls by name
  // (ScriptExtension::CallDispatch). Each entry point runs under the runtime's time budget.
  const AMK = {};
  const dispatch = {};
)AMKJS";

extern const char kFrameTail[];
const char kFrameTail[] = R"AMKJS(
  // ---- Freeze the API, then publish it as a global ------------------------------------------------------------

  // The object members are frozen here, once every module ran, so that a module may still add to another one's.
  for (const part of Object.values(AMK)) {
    if (typeof part === "object" && part !== null) Object.freeze(part);
  }
  Object.freeze(AMK);

  globalThis.AMK = AMK;

  return dispatch;
}))AMKJS";

}  // namespace amk::prelude
