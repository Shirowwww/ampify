// Module storage: AMK.storage, JSON values kept per element. Natives: core/natives/storage.cpp.

namespace amk::prelude {

extern const char kStorage[];
const char kStorage[] = R"AMKJS(
  AMK.storage = {
    get(key, fallback) {
      const raw = native.storageGet(text(key));
      return raw === undefined ? fallback : JSON.parse(raw);
    },
    set(key, value) {
      const raw = JSON.stringify(value);
      if (raw === undefined) throw new TypeError("storage.set : la valeur n'est pas du JSON");
      native.storageSet(text(key), raw);
    },
    remove(key) {
      native.storageRemove(text(key));
    },
  };
)AMKJS";

}  // namespace amk::prelude
