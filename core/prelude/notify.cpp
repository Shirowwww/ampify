// Module notify: Ampify.notify, a notification from the element. Natives: core/natives/notify.cpp.

namespace ampify::prelude {

extern const char kNotify[];
const char kNotify[] = R"AMPIFYJS(
  Ampify.notify = (options) => {
    const note = options || {};
    native.notify(note.title === undefined ? "" : text(note.title), note.text === undefined ? "" : text(note.text));
  };
)AMPIFYJS";

}  // namespace ampify::prelude
