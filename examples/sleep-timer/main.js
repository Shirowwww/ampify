// Pauses playback after the delay set by the "minutes" setting (30 by default), then lets you know. It says so when it
// starts too: a pause nobody expects looks like a bug.
Ampify.i18n.define({
  en: { title: "Sleep timer", armed: "The music pauses in {minutes} min. Change the delay in Settings, under Ampify.",
        paused: "Playback is paused." },
  fr: { title: "Minuterie de veille", armed: "La musique se met en pause dans {minutes} min. Le délai se règle dans les Réglages, sous Ampify.",
        paused: "La lecture est en pause." },
  de: { title: "Einschlaf-Timer", armed: "Die Musik wird in {minutes} Min. pausiert. Die Zeit lässt sich in den Einstellungen unter Ampify ändern.",
        paused: "Die Wiedergabe ist pausiert." },
  es: { title: "Temporizador de sueño", armed: "La música se pausará en {minutes} min. El tiempo se cambia en los Ajustes, en Ampify.",
        paused: "La reproducción está en pausa." },
  pt: { title: "Timer para dormir", armed: "A música será pausada em {minutes} min. O tempo pode ser alterado nos Ajustes, em Ampify.",
        paused: "A reprodução está pausada." },
  ru: { title: "Таймер сна", armed: "Музыка встанет на паузу через {minutes} мин. Время можно изменить в настройках, в разделе Ampify.",
        paused: "Воспроизведение приостановлено." },
  ja: { title: "スリープタイマー", armed: "音楽は {minutes} 分後に一時停止します。時間は設定の「Ampify」で変更できます。",
        paused: "再生を一時停止しました。" },
  zh: { title: "睡眠定时器", armed: "音乐将在 {minutes} 分钟后暂停。可在“设置”的 Ampify 中修改时间。",
        paused: "播放已暂停。" },
  it: { title: "Timer di spegnimento", armed: "La musica va in pausa tra {minutes} min. Cambia il ritardo nelle Impostazioni, sotto Ampify.",
        paused: "La riproduzione è in pausa." },
  ko: { title: "취침 타이머", armed: "{minutes}분 후에 음악이 일시정지됩니다. 시간은 설정의 Ampify에서 바꿀 수 있습니다.",
        paused: "재생이 일시정지되었습니다." },
});

const minutes = Ampify.config.get("minutes", 30);
Ampify.log("pause in " + minutes + " min");
Ampify.notify({ title: Ampify.i18n.t("title"), text: Ampify.i18n.t("armed", { minutes }) });

setTimeout(() => {
  Ampify.player.pause();
  Ampify.notify({ title: Ampify.i18n.t("title"), text: Ampify.i18n.t("paused") });
}, Math.max(1, minutes) * 60 * 1000);
