// Module library: Ampify.library.tracks, the tracks of the user's Apple Music library with their play counts, read
// from Apple Music's library file. Natives: src/core/natives/library.cpp.

namespace ampify::prelude {

extern const char kLibrary[];
const char kLibrary[] = R"AMPIFYJS(
  Ampify.library = {
    tracks() {
      return dispatch.request(() => native.libraryTracks()).then((answer) => {
        if (answer.status !== 200) throw new Error("library.tracks: " + answer.text);
        return JSON.parse(answer.text);
      });
    },
  };
)AMPIFYJS";

}  // namespace ampify::prelude
