// Pauses playback after the delay set by the "minutes" setting (30 by default), then lets you know.
Ampify.i18n.define({
  en: { title: "Sleep timer", paused: "Playback is paused." },
  fr: { title: "Minuterie de veille", paused: "La lecture est en pause." },
});

const minutes = Ampify.config.get("minutes", 30);
Ampify.log("pause in " + minutes + " min");

setTimeout(() => {
  Ampify.player.pause();
  Ampify.notify({ title: Ampify.i18n.t("title"), text: Ampify.i18n.t("paused") });
}, Math.max(1, minutes) * 60 * 1000);
