// Module remote: Ampify.remote, the element's part of the local remote control's GET /status. Natives:
// src/core/natives/remote.cpp.

namespace ampify::prelude {

extern const char kRemote[];
const char kRemote[] = R"AMPIFYJS(
  Ampify.remote = {
    // state: a plain object, 4 KB at most once in JSON. Shown under extensions.<id> in GET /status.
    // Returns false when the remote control is off (setting remote.enabled) or not offered by this host.
    setState(state) {
      if (typeof state !== "object" || state === null || Array.isArray(state)) {
        throw new TypeError("remote.setState expects an object");
      }
      return native.remoteState(JSON.stringify(state));
    },
  };
)AMPIFYJS";

}  // namespace ampify::prelude
