// Module fs: AMK.fs.writeText, a text file in the element's output folder (<kit>\output\<id>\), for programs such as
// OBS. Nothing else on disk can be reached. Natives: core/natives/fs.cpp.

namespace amk::prelude {

extern const char kFs[];
const char kFs[] = R"AMKJS(
  AMK.fs = {
    writeText(name, content) {
      return native.fsWrite(text(name), content === undefined || content === null ? "" : text(content));
    },
  };
)AMKJS";

}  // namespace amk::prelude
