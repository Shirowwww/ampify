// Module config: Ampify.config, the element's settings. Natives: src/core/natives/config.cpp.

namespace ampify::prelude {

extern const char kConfig[];
const char kConfig[] = R"AMPIFYJS(
  const TRUE_WORDS = ["true", "on", "yes", "1"];
  const FALSE_WORDS = ["false", "off", "no", "0"];

  Ampify.config = {
    get(key, fallback) {
      // A setting the manifest declares has its default there, with its type.
      if (fallback === undefined) fallback = native.configDeclared(text(key));
      const raw = native.configGet(text(key));
      if (raw === undefined) return fallback;
      // A value edited by hand into something else reads as the default, not as false or 0.
      if (typeof fallback === "boolean") {
        const word = raw.toLowerCase();
        return TRUE_WORDS.includes(word) ? true : FALSE_WORDS.includes(word) ? false : fallback;
      }
      if (typeof fallback === "number") {
        const number = raw.trim() === "" ? NaN : Number(raw);
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
