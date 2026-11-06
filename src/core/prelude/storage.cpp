// Module storage: Ampify.storage, JSON values kept per element. Natives: src/core/natives/storage.cpp.

namespace ampify::prelude {

extern const char kStorage[];
const char kStorage[] = R"AMPIFYJS(
  Ampify.storage = {
    get(key, fallback) {
      const raw = native.storageGet(text(key));
      return raw === undefined ? fallback : JSON.parse(raw);
    },
    set(key, value) {
      const raw = JSON.stringify(value);
      if (raw === undefined) throw new TypeError("storage.set: the value is not JSON");
      native.storageSet(text(key), raw);
    },
    remove(key) {
      native.storageRemove(text(key));
    },
    // Every value of this element, gone.
    clear() {
      native.storageClear();
    },
  };
)AMPIFYJS";

}  // namespace ampify::prelude
