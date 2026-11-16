// Sing: a karaoke mode, like Apple Music Sing on the iPhone. A microphone button on the playback bar opens a small
// popup: a switch, how much of the singer's voice stays, and the quality (Light, or Studio on the graphics card, which
// holds the sound back a moment so the lyrics stay in step). The sound itself is processed by Ampify in the audio
// process (Ampify.audio.sing); this element only asks for it and says how far it is.

Ampify.i18n.define({
  en: {
    label: "Sing", voice: "Voice", light: "Light", studio: "Studio", quality: "Quality",
    studioNote: "Studio takes a moment: the sound is held back a little so the lyrics stay in step.",
    off: "Off", downloading: "Downloading {percent} %", getting: "Getting ready", on: "On",
    noGpu: "Studio needs a graphics card", downloadFailed: "The download failed", modelFailed: "Studio could not start",
    failed: "Sing could not start", atmos: "Not available for Dolby Atmos songs", automix: "Paused while AutoMix mixes",
  },
  fr: {
    label: "Chanter", voice: "Voix", light: "Léger", studio: "Studio", quality: "Qualité",
    studioNote: "Studio demande un instant : le son est retenu un peu pour que les paroles restent en phase.",
    off: "Désactivé", downloading: "Téléchargement {percent} %", getting: "Préparation", on: "Activé",
    noGpu: "Studio demande une carte graphique", downloadFailed: "Le téléchargement a échoué",
    modelFailed: "Studio n'a pas pu démarrer", failed: "Chanter n'a pas pu démarrer",
    atmos: "Indisponible pour les morceaux Dolby Atmos", automix: "En pause pendant un mix AutoMix",
  },
  de: {
    label: "Singen", voice: "Stimme", light: "Leicht", studio: "Studio", quality: "Qualität",
    studioNote: "Studio braucht einen Moment: Der Ton wird kurz zurückgehalten, damit der Text im Takt bleibt.",
    off: "Aus", downloading: "Download {percent} %", getting: "Wird vorbereitet", on: "An",
    noGpu: "Studio braucht eine Grafikkarte", downloadFailed: "Der Download ist fehlgeschlagen",
    modelFailed: "Studio konnte nicht starten", failed: "Singen konnte nicht starten",
    atmos: "Für Dolby-Atmos-Titel nicht verfügbar", automix: "Pausiert, während AutoMix mischt",
  },
  es: {
    label: "Cantar", voice: "Voz", light: "Ligero", studio: "Estudio", quality: "Calidad",
    studioNote: "Estudio tarda un momento: el sonido se retrasa un poco para que la letra siga a tiempo.",
    off: "Desactivado", downloading: "Descargando {percent} %", getting: "Preparando", on: "Activado",
    noGpu: "Estudio necesita una tarjeta gráfica", downloadFailed: "La descarga falló",
    modelFailed: "Estudio no pudo iniciarse", failed: "Cantar no pudo iniciarse",
    atmos: "No disponible para canciones Dolby Atmos", automix: "En pausa mientras AutoMix mezcla",
  },
  pt: {
    label: "Cantar", voice: "Voz", light: "Leve", studio: "Estúdio", quality: "Qualidade",
    studioNote: "O Estúdio leva um instante: o som é retido um pouco para a letra ficar em sincronia.",
    off: "Desligado", downloading: "Baixando {percent} %", getting: "Preparando", on: "Ligado",
    noGpu: "O Estúdio precisa de uma placa de vídeo", downloadFailed: "O download falhou",
    modelFailed: "O Estúdio não pôde iniciar", failed: "Cantar não pôde iniciar",
    atmos: "Indisponível para músicas Dolby Atmos", automix: "Em pausa enquanto o AutoMix mistura",
  },
  ru: {
    label: "Петь", voice: "Голос", light: "Лёгкий", studio: "Студия", quality: "Качество",
    studioNote: "Студии нужно мгновение: звук немного задерживается, чтобы текст шёл в такт.",
    off: "Выкл.", downloading: "Загрузка {percent} %", getting: "Подготовка", on: "Вкл.",
    noGpu: "Для режима «Студия» нужна видеокарта", downloadFailed: "Не удалось загрузить",
    modelFailed: "Не удалось запустить «Студию»", failed: "Не удалось запустить режим пения",
    atmos: "Недоступно для песен Dolby Atmos", automix: "Пауза, пока AutoMix сводит песни",
  },
  ja: {
    label: "歌う", voice: "ボーカル", light: "ライト", studio: "スタジオ", quality: "品質",
    studioNote: "スタジオは少し時間がかかります。歌詞が合うように音を少し遅らせます。",
    off: "オフ", downloading: "ダウンロード中 {percent} %", getting: "準備中", on: "オン",
    noGpu: "スタジオにはグラフィックスカードが必要です", downloadFailed: "ダウンロードに失敗しました",
    modelFailed: "スタジオを開始できませんでした", failed: "歌うモードを開始できませんでした",
    atmos: "Dolby Atmos の曲では使えません", automix: "AutoMix のミックス中は一時停止",
  },
  zh: {
    label: "演唱", voice: "人声", light: "轻量", studio: "录音室", quality: "质量",
    studioNote: "录音室需要片刻：声音会稍稍延后，让歌词保持同步。",
    off: "关闭", downloading: "正在下载 {percent} %", getting: "正在准备", on: "已开启",
    noGpu: "录音室需要显卡", downloadFailed: "下载失败",
    modelFailed: "录音室无法启动", failed: "演唱模式无法启动",
    atmos: "杜比全景声歌曲不可用", automix: "AutoMix 混音时暂停",
  },
  it: {
    label: "Canta", voice: "Voce", light: "Leggero", studio: "Studio", quality: "Qualità",
    studioNote: "Studio richiede un momento: il suono viene trattenuto un poco perché il testo resti a tempo.",
    off: "Disattivato", downloading: "Download {percent} %", getting: "Preparazione", on: "Attivo",
    noGpu: "Studio richiede una scheda grafica", downloadFailed: "Download non riuscito",
    modelFailed: "Studio non è partito", failed: "Canta non è partito",
    atmos: "Non disponibile per i brani Dolby Atmos", automix: "In pausa durante un mix AutoMix",
  },
  ko: {
    label: "노래하기", voice: "보컬", light: "라이트", studio: "스튜디오", quality: "품질",
    studioNote: "스튜디오는 잠시 걸립니다. 가사가 맞도록 소리를 조금 늦춥니다.",
    off: "끔", downloading: "다운로드 중 {percent} %", getting: "준비 중", on: "켬",
    noGpu: "스튜디오에는 그래픽 카드가 필요합니다", downloadFailed: "다운로드하지 못했습니다",
    modelFailed: "스튜디오를 시작하지 못했습니다", failed: "노래하기를 시작하지 못했습니다",
    atmos: "Dolby Atmos 곡에서는 사용할 수 없습니다", automix: "AutoMix가 믹스하는 동안 일시 중지",
  },
});
const t = (key, values) => Ampify.i18n.t(key, values);

const { h, useState, useEffect } = Ampify.React;
const { Toggle, Slider, Tabs, Text } = Ampify.components;

const DEFAULT_VOICE = 0.1;
const POPUP = "sing";
const MIC = "";  // Microphone, in Segoe Fluent Icons

const clampVoice = (value) => (Number.isFinite(Number(value)) ? Math.min(1, Math.max(0, Number(value))) : DEFAULT_VOICE);

// What the user chose, kept between runs: whether Sing is on, the quality they picked (null: not chosen yet) and the voice.
const saved = Ampify.storage.get("sing", {}) || {};
const choice = {
  enabled: saved.enabled === true,
  mode: saved.mode === "light" || saved.mode === "studio" ? saved.mode : null,
  voice: clampVoice(saved.voice ?? DEFAULT_VOICE),
};

// Studio when this PC can run it (or is not known yet), Light when it cannot.
function qualityFor(state) {
  if (choice.mode) return choice.mode;
  return state.available === false ? "light" : "studio";
}

function apply(state) {
  const mode = choice.enabled ? qualityFor(state) : "off";
  Ampify.storage.set("sing", { enabled: choice.enabled, mode: choice.mode, voice: choice.voice });
  Ampify.audio.sing.set({ mode, voice: choice.voice });
}

// The line under the switch, in plain words. A reason beats the phase: it says why Sing is not on.
function statusText(state) {
  if (state.phase === "downloading") return t("downloading", { percent: Math.round(state.progress * 100) });
  const reasons = {
    "no-graphics-card": "noGpu", "download-failed": "downloadFailed", "model-failed": "modelFailed",
    "dolby-atmos": "atmos", "automix-mixing": "automix",
  };
  if (reasons[state.reason]) return t(reasons[state.reason]);
  if (state.phase === "failed") return t("failed");
  if (state.phase === "loading" || state.phase === "buffering") return t("getting");
  if (state.phase === "on") return t("on");
  if (state.mode === "studio" && state.available === false) return t("noGpu");
  return t("off");
}

function SingPopup() {
  const [state, setState] = useState(Ampify.audio.sing.state());
  const [, redraw] = useState(0);
  useEffect(() => Ampify.audio.sing.onChange(setState), []);
  const quality = qualityFor(state);
  const change = (patch) => {
    Object.assign(choice, patch);
    apply(state);
    setState(Ampify.audio.sing.state());
    redraw((n) => n + 1);
  };
  return h("stack", { gap: 12, padding: 16, minWidth: 280 },
    h(Toggle, { label: t("label"), checked: choice.enabled, onChange: (on) => change({ enabled: on === true }) }),
    h(Text, { text: statusText(state), variant: "caption" }),
    h(Slider, { label: t("voice"), min: 0, max: 100, step: 1,  // Ampify shows the value after the label
                value: Math.round(choice.voice * 100), onChange: (value) => change({ voice: clampVoice(Number(value) / 100) }) }),
    h(Text, { text: t("quality"), variant: "caption" }),
    h(Tabs, { value: quality, options: [{ value: "light", label: t("light") }, { value: "studio", label: t("studio") }],
              onChange: (mode) => change({ mode }) }),
    quality === "studio" ? h(Text, { text: t("studioNote"), variant: "caption", wrap: true }) : null);
}

const root = Ampify.ReactDOM.createRoot("popup:" + POPUP);
const button = Ampify.ui.playbar.addButton({
  label: t("label"),
  icon: MIC,
  active: false,
  onClick: () => root.render(h(SingPopup)),
});

// The button is lit while Sing is asked for, whoever asked (this popup, or another element).
const light = (state) => Ampify.ui.setActive(button, state.mode !== "off");
light(Ampify.audio.sing.state());
const stopWatching = Ampify.audio.sing.onChange(light);

if (choice.enabled) apply(Ampify.audio.sing.state());

Ampify.onStop(() => {
  stopWatching();
  root.unmount();
  if (choice.enabled) Ampify.audio.sing.set({ mode: "off", voice: choice.voice });  // only what this switch turned on
});
