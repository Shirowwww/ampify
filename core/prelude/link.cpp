// Module link: AMK.link, the message channel between an app and the extension that has the same id (Spicetify's
// subfiles_extension). Natives: core/natives/link.cpp. Messages are JSON, delivered on the JavaScript thread at the
// other element's next turn, through dispatch.link.

namespace amk::prelude {

extern const char kLink[];
const char kLink[] = R"AMKJS(
  const linkListeners = [];

  AMK.link = {
    // Sends a JSON value to the linked element. True when it is running and the message is queued for it.
    post(message) {
      const json = JSON.stringify(message);
      if (json === undefined) throw new TypeError("AMK.link.post attend une valeur JSON");
      return native.linkPost(json);
    },
    // fn(message) receives each message of the linked element. Returns a function that removes fn.
    on(fn) {
      if (typeof fn !== "function") throw new TypeError("AMK.link.on attend une fonction");
      linkListeners.push(fn);
      return () => {
        const index = linkListeners.indexOf(fn);
        if (index >= 0) linkListeners.splice(index, 1);
      };
    },
    // True while the linked element runs.
    connected: () => native.linkConnected(),
  };

  dispatch.link = (json) => {
    const message = JSON.parse(json);
    for (const fn of linkListeners.slice()) runGuarded("AMK.link.on", fn, [message]);
  };
)AMKJS";

}  // namespace amk::prelude
