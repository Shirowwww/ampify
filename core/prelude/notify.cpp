// Module notify: AMK.notify, a notification from the element. Natives: core/natives/notify.cpp.

namespace amk::prelude {

extern const char kNotify[];
const char kNotify[] = R"AMKJS(
  AMK.notify = (options) => {
    const note = options || {};
    native.notify(note.title === undefined ? "" : text(note.title), note.text === undefined ? "" : text(note.text));
  };
)AMKJS";

}  // namespace amk::prelude
