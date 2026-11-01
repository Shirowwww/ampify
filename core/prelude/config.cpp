// Module config: Ampify.config, the element's settings. Natives: core/natives/config.cpp.

namespace ampify::prelude {

extern const char kConfig[];
const char kConfig[] = R"AMPIFYJS(
  const TRUE_WORDS = ["true", "on", "yes", "1"];

  Ampify.config = {
    get(key, fallback) {
      // A setting the manifest declares has its default there, with its type.
      if (fallback === undefined) fallback = native.configDeclared(text(key));
      const raw = native.configGet(text(key));
      if (raw === undefined) return fallback;
      if (typeof fallback === "boolean") return TRUE_WORDS.includes(raw.toLowerCase());
      if (typeof fallback === "number") {
        const number = Number(raw);
        return Number.isFinite(number) ? number : fallback;
      }
      return raw;
    },
    set(key, value) {
      let raw;
      if (typeof value === "boolean") raw = value ? "true" : "false";
      else if (typeof value === "number" && Number.isFinite(value)) raw = String(value);
      else if (typeof value === "string") raw = value;
      else throw new TypeError("config.set: boolean, number or text values only");
      native.configSet(text(key), raw);
    },
  };
)AMPIFYJS";

}  // namespace ampify::prelude
