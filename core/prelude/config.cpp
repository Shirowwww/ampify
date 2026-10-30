// Module config: AMK.config, the element's settings. Natives: core/natives/config.cpp.

namespace amk::prelude {

extern const char kConfig[];
const char kConfig[] = R"AMKJS(
  const TRUE_WORDS = ["true", "on", "yes", "1"];

  AMK.config = {
    get(key, fallback) {
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
      else throw new TypeError("config.set : valeur booléenne, numérique ou texte seulement");
      native.configSet(text(key), raw);
    },
  };
)AMKJS";

}  // namespace amk::prelude
