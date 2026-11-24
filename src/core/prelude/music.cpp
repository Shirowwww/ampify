// Module music: Ampify.music, what Apple knows about catalog songs. analysis gives Apple's own audio analysis (tempo,
// key, beat and bar grid, loudness, vocal activity, energy, valence...), mood reads a mood from it.
// Natives: src/core/natives/music.cpp. Permission player.read.

namespace ampify::prelude {

extern const char kMusic[];
const char kMusic[] = R"AMPIFYJS(
  const MOOD_LINE = 0.5;  // valence and energy run from 0 to 1: above the middle is bright, or lively

  // The storefront of the app's locale ("fr-FR" -> "fr"), or the one given.
  const storefrontOf = (options) => {
    const given = options && options.storefront;
    if (typeof given === "string" && /^[a-z]{2}$/i.test(given)) return given.toLowerCase();
    const region = /^[a-z]{2,3}[-_]([a-z]{2})\b/i.exec(text(Ampify.locale || ""));
    return region ? region[1].toLowerCase() : "us";
  };

  // A section value of Apple's analysis ({beginning, main, ending}), the main one by default.
  const section = (value, part) => (value && typeof value === "object" ? value[part || "main"] : undefined);

  // Valence (sad to happy) against energy (calm to lively), the two axes of the usual mood map.
  const moodOf = (analysis, part) => {
    if (!analysis || typeof analysis !== "object") return null;
    const valence = section(analysis.valence, part);
    const energy = section(analysis.energy, part);
    if (typeof valence !== "number" || typeof energy !== "number") return null;
    if (valence >= MOOD_LINE) return energy >= MOOD_LINE ? "happy" : "calm";
    return energy >= MOOD_LINE ? "tense" : "sad";
  };

  Ampify.music = Object.freeze({
    // ids: catalog ids (track.catalogId, a queue entry's catalogId), one or a list. Resolves with an object keyed by
    // id; a song Apple has not analysed is missing from it. Rejects with the reason when Apple cannot be reached.
    analysis(ids, options = {}) {
      const list = (Array.isArray(ids) ? ids : [ids]).map((id) => text(id)).filter((id) => /^\d{1,20}$/.test(id));
      if (list.length === 0) return Promise.resolve({});
      return dispatch.request(() => native.musicAnalysis(list.join(","), storefrontOf(options))).then((answer) => {
        if (answer.status !== 200) throw new Error("music.analysis: " + answer.text);
        const items = JSON.parse(answer.text).items || {};
        for (const id of Object.keys(items)) items[id].mood = moodOf(items[id]);
        return items;
      });
    },
    // "happy", "calm", "sad" or "tense", or null without valence and energy; part: "beginning", "main", "ending".
    mood: (analysis, part) => moodOf(analysis, part),
  });
)AMPIFYJS";

}  // namespace ampify::prelude
