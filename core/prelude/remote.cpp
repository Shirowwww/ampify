// Module remote: AMK.remote, the element's part of the local remote control's GET /status. Natives:
// core/natives/remote.cpp.

namespace amk::prelude {

extern const char kRemote[];
const char kRemote[] = R"AMKJS(
  AMK.remote = {
    // state: a plain object, 4 KB at most once in JSON. Shown under extensions.<id> in GET /status.
    // Returns false when the remote control is off (setting remote.enabled) or not offered by this host.
    setState(state) {
      if (typeof state !== "object" || state === null || Array.isArray(state)) {
        throw new TypeError("remote.setState attend un objet");
      }
      return native.remoteState(JSON.stringify(state));
    },
  };
)AMKJS";

}  // namespace amk::prelude
