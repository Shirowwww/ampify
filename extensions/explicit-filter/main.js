// Explicit or clean: plays one version of the songs Apple has in both, explicit or clean, and skips the other one when
// it comes up (a clean version added by mistake, or the reverse). Songs Apple marks neither way always play. A song
// skipped once plays if the user goes back to it.

Ampify.i18n.define({
  en: { title: "Explicit or clean", skipped: "Skipped the {rating} version of \"{song}\"", explicit: "explicit", clean: "clean" },
  fr: { title: "Explicite ou censuré", skipped: "Version {rating} de « {song} » passée", explicit: "explicite", clean: "censurée" },
  de: { title: "Explizit oder bereinigt", skipped: "Die {rating} Version von „{song}“ übersprungen", explicit: "explizite", clean: "bereinigte" },
  es: { title: "Explícita o limpia", skipped: "Se saltó la versión {rating} de «{song}»", explicit: "explícita", clean: "limpia" },
  pt: { title: "Explícita ou limpa", skipped: "A versão {rating} de “{song}” foi pulada", explicit: "explícita", clean: "limpa" },
  ru: { title: "Откровенная или цензурная", skipped: "Пропущена {rating} версия «{song}»", explicit: "откровенная", clean: "цензурная" },
  ja: { title: "Explicit かクリーンか", skipped: "「{song}」の{rating}版をスキップしました", explicit: "Explicit", clean: "クリーン" },
  zh: { title: "露骨版或洁净版", skipped: "已跳过《{song}》的{rating}版", explicit: "露骨", clean: "洁净" },
  it: { title: "Esplicita o pulita", skipped: "Saltata la versione {rating} di «{song}»", explicit: "esplicita", clean: "pulita" },
  ko: { title: "Explicit 또는 클린", skipped: "\"{song}\"의 {rating} 버전을 건너뛰었습니다", explicit: "Explicit", clean: "클린" },
});
const t = (key, values) => Ampify.i18n.t(key, values);

// The setting says which version to keep; the other rating is the one skipped.
const keep = () => (Ampify.config.get("keep") === "Clean" ? "clean" : "explicit");
const unwanted = () => (keep() === "clean" ? "explicit" : "clean");

const skippedEntries = new Set();  // queue entries skipped once: going back to one plays it
let lastSeen = "";

function follow(state) {
  const track = state && state.track;
  if (!track) return;
  const entry = track.queueItemId || track.id;
  // The rating is part of the key: when it comes after the song, the same entry is looked at again.
  const seen = entry + "|" + (track.contentRating || "");
  if (seen === lastSeen) return;
  lastSeen = seen;
  if (track.contentRating !== unwanted() || skippedEntries.has(entry)) return;
  skippedEntries.add(entry);
  const message = t("skipped", { rating: t(track.contentRating), song: track.title });
  Ampify.log(message);
  Ampify.notify({ title: t("title"), text: message });
  Ampify.player.next();
}

follow(Ampify.player.state());
Ampify.player.on("trackchange", follow);
// Apple says the rating with the song, but a song restored at start may come before it: a state change brings it.
Ampify.player.on("statechange", follow);
