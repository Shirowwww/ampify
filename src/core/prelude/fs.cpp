// Module fs: Ampify.fs.writeText, a text file in the element's output folder (<kit>\output\<id>\), for programs such as
// OBS. Nothing else on disk can be reached. Natives: src/core/natives/fs.cpp.

namespace ampify::prelude {

extern const char kFs[];
const char kFs[] = R"AMPIFYJS(
  Ampify.fs = {
    writeText(name, content) {
      return native.fsWrite(text(name), content === undefined || content === null ? "" : text(content));
    },
  };
)AMPIFYJS";

}  // namespace ampify::prelude
