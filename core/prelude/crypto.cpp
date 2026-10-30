// Module crypto: AMK.crypto.md5, for the services that sign their calls with it (Last.fm). Natives:
// core/natives/crypto.cpp.

namespace amk::prelude {

extern const char kCrypto[];
const char kCrypto[] = R"AMKJS(
  AMK.crypto = {
    md5: (value) => native.md5(text(value)),
  };
)AMKJS";

}  // namespace amk::prelude
