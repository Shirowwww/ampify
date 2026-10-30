// Module i18n: AMK.i18n, the texts of an element in French and in English. AMK.i18n.language is the kit language
// ("fr" or "en"), from the setting `language`; AMK.locale stays the app locale. Natives: core/natives/i18n.cpp.

namespace amk::prelude {

extern const char kI18n[];
const char kI18n[] = R"AMKJS(
  const i18nTables = { fr: new Map(), en: new Map() };
  const i18nLanguage = native.i18nLanguage === "en" ? "en" : "fr";

  // "{name}" is replaced by vars.name. A placeholder without a value stays visible.
  const fillText = (pattern, vars) => {
    if (!vars || typeof vars !== "object") return pattern;
    return pattern.replace(/\{(\w+)\}/g, (whole, name) =>
      Object.prototype.hasOwnProperty.call(vars, name) ? text(vars[name]) : whole);
  };

  AMK.i18n = {
    language: i18nLanguage,
    // define({ fr: { clé: "texte" }, en: { clé: "text" } }). May be called again: later texts replace earlier ones.
    define(messages) {
      if (!messages || typeof messages !== "object") throw new TypeError("i18n.define attend un objet { fr, en }");
      for (const language of ["fr", "en"]) {
        const table = messages[language];
        if (table === undefined) continue;
        if (!table || typeof table !== "object") throw new TypeError("i18n.define : " + language + " doit être un objet");
        for (const key of Object.keys(table)) i18nTables[language].set(key, text(table[key]));
      }
    },
    // The text of `key` in the kit language, then in French, then the kit's own texts ("kit.tray.next"), then the
    // key itself.
    t(key, vars) {
      const name = text(key);
      let pattern = i18nTables[i18nLanguage].get(name);
      if (pattern === undefined) pattern = i18nTables.fr.get(name);
      if (pattern === undefined && name.startsWith("kit.")) pattern = native.i18nKit(name.slice(4));
      if (pattern === undefined) pattern = name;
      return fillText(pattern, vars);
    },
  };
)AMKJS";

}  // namespace amk::prelude
