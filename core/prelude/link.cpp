// Module link: Ampify.link, the message channel between an app and the extension that has the same id (Spicetify's
// subfiles_extension). Natives: core/natives/link.cpp. Messages are JSON, delivered on the JavaScript thread at the
// other element's next turn, through dispatch.link.

namespace ampify::prelude {

extern const char kLink[];
const char kLink[] = R"AMPIFYJS(
  const linkListeners = [];

  Ampify.link = {
    // Sends a JSON value to the linked element. True when it is running and the message is queued for it.
    post(message) {
      const json = JSON.stringify(message);
      if (json === undefined) throw new TypeError("Ampify.link.post expects a JSON value");
      return native.linkPost(json);
    },
    // fn(message) receives each message of the linked element. Returns a function that removes fn.
    on(fn) {
      if (typeof fn !== "function") throw new TypeError("Ampify.link.on expects a function");
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
    for (const fn of linkListeners.slice()) runGuarded("Ampify.link.on", fn, [message]);
  };
)AMPIFYJS";

}  // namespace ampify::prelude
