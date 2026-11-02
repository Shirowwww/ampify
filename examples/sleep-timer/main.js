// Pauses playback after the delay set by the "minutes" setting (30 by default), then lets you know. It says so when it
// starts too: a pause nobody expects looks like a bug.
Ampify.i18n.define({
  en: { title: "Sleep timer", armed: "The music pauses in {minutes} min. Change the delay in Settings, under Ampify.",
        paused: "Playback is paused." },
  fr: { title: "Minuterie de veille", armed: "La musique se met en pause dans {minutes} min. Le délai se règle dans les Réglages, sous Ampify.",
        paused: "La lecture est en pause." },
});

const minutes = Ampify.config.get("minutes", 30);
Ampify.log("pause in " + minutes + " min");
Ampify.notify({ title: Ampify.i18n.t("title"), text: Ampify.i18n.t("armed", { minutes }) });

setTimeout(() => {
  Ampify.player.pause();
  Ampify.notify({ title: Ampify.i18n.t("title"), text: Ampify.i18n.t("paused") });
}, Math.max(1, minutes) * 60 * 1000);
