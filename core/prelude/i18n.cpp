// Module i18n: Ampify.i18n, the texts of an element in several languages. Ampify.i18n.language is the Windows display
// language as two letters ("en", "fr"...), from the host locale; Ampify.locale stays the full app locale.
// Natives: core/natives/i18n.cpp.

namespace ampify::prelude {

extern const char kI18n[];
const char kI18n[] = R"AMPIFYJS(
  const i18nTables = new Map();
  const i18nLanguage = text(native.i18nLanguage) || "en";

  // "{name}" is replaced by vars.name. A placeholder without a value stays visible.
  const fillText = (pattern, vars) => {
    if (!vars || typeof vars !== "object") return pattern;
    return pattern.replace(/\{(\w+)\}/g, (whole, name) =>
      Object.prototype.hasOwnProperty.call(vars, name) ? text(vars[name]) : whole);
  };

  Ampify.i18n = {
    language: i18nLanguage,
    // define({ en: { key: "text" }, fr: { key: "texte" } }). May be called again: later texts replace earlier ones.
    define(messages) {
      if (!messages || typeof messages !== "object") throw new TypeError("i18n.define expects an object { en, fr, ... }");
      for (const language of Object.keys(messages)) {
        const table = messages[language];
        if (!table || typeof table !== "object") throw new TypeError("i18n.define: " + language + " must be an object");
        if (!i18nTables.has(language)) i18nTables.set(language, new Map());
        for (const key of Object.keys(table)) i18nTables.get(language).set(key, text(table[key]));
      }
    },
    // The text of `key` in the display language, then in English, then Ampify's own English texts ("kit.tray.next"),
    // then the key itself.
    t(key, vars) {
      const name = text(key);
      let pattern = i18nTables.get(i18nLanguage)?.get(name);
      if (pattern === undefined) pattern = i18nTables.get("en")?.get(name);
      if (pattern === undefined && name.startsWith("kit.")) pattern = native.i18nKit(name.slice(4));
      if (pattern === undefined) pattern = name;
      return fillText(pattern, vars);
    },
  };
)AMPIFYJS";

}  // namespace ampify::prelude
