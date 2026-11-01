// Module crypto: Ampify.crypto.md5, for the services that sign their calls with it (Last.fm). Natives:
// src/core/natives/crypto.cpp.

namespace ampify::prelude {

extern const char kCrypto[];
const char kCrypto[] = R"AMPIFYJS(
  Ampify.crypto = {
    md5: (value) => native.md5(text(value)),
  };
)AMPIFYJS";

}  // namespace ampify::prelude
