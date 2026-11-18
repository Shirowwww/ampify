// Sing: a karaoke mode, like Apple Music Sing on the iPhone. A microphone button on the playback bar opens a small
// popup: a switch, karaoke or the singer alone, how much of the voice (or the music) stays, and the quality (Light, or
// Studio on the graphics card, which holds the sound back a moment so the lyrics stay in step, with its standard model
// or the heavier precise one). The sound itself is processed by Ampify in the audio process (Ampify.audio.sing); this
// element only asks for it and says how far it is.

Ampify.i18n.define({
  en: {
    label: "Sing", karaoke: "Karaoke", solo: "Vocals only", music: "Music", voice: "Singer's voice", light: "Light", studio: "Studio", quality: "Quality",
    lightNote: "Instant, worked out on the processor; the voice and the music stay a little mixed.",
    studioNote: "The graphics card separates the voice from the music almost completely.",
    model: "Model", standard: "Standard", precise: "Precise",
    preciseNote: "Separates the voice from the music much better. Recommended: a graphics card from the last few years (GeForce RTX 2060 or RTX 3050, Radeon RX 6600 or better).",
    firstDownload: "First use downloads {size} MB.",
    off: "Off", downloading: "Downloading {percent} %", getting: "Getting ready", running: "On · {quality}",
    noGpu: "Studio needs a graphics card", downloadFailed: "The download failed", modelFailed: "Studio could not start",
    failed: "Sing could not start", atmos: "Not available for Dolby Atmos songs", automix: "Paused while AutoMix mixes",
  },
  fr: {
    label: "Chanter", karaoke: "Karaoké", solo: "Voix seule", music: "Musique", voice: "Voix du chanteur", light: "Léger", studio: "Studio", quality: "Qualité",
    lightNote: "Instantané, calculé par le processeur ; la voix et la musique restent un peu mêlées.",
    studioNote: "La carte graphique sépare presque entièrement la voix de la musique.",
    model: "Modèle", standard: "Standard", precise: "Précis",
    preciseNote: "Sépare bien mieux la voix de la musique. Recommandé : une carte graphique de ces dernières années (GeForce RTX 2060 ou RTX 3050, Radeon RX 6600 ou mieux).",
    firstDownload: "Premier usage : téléchargement de {size} Mo.",
    off: "Désactivé", downloading: "Téléchargement {percent} %", getting: "Préparation", running: "En marche · {quality}",
    noGpu: "Studio demande une carte graphique", downloadFailed: "Le téléchargement a échoué",
    modelFailed: "Studio n'a pas pu démarrer", failed: "Chanter n'a pas pu démarrer",
    atmos: "Indisponible pour les morceaux Dolby Atmos", automix: "En pause pendant un mix AutoMix",
  },
  de: {
    label: "Singen", karaoke: "Karaoke", solo: "Nur Gesang", music: "Musik", voice: "Stimme des Sängers", light: "Leicht", studio: "Studio", quality: "Qualität",
    lightNote: "Sofort, vom Prozessor berechnet; Stimme und Musik bleiben etwas vermischt.",
    studioNote: "Die Grafikkarte trennt Stimme und Musik fast vollständig.",
    model: "Modell", standard: "Standard", precise: "Präzise",
    preciseNote: "Trennt Stimme und Musik viel besser. Empfohlen: eine Grafikkarte der letzten Jahre (GeForce RTX 2060 oder RTX 3050, Radeon RX 6600 oder besser).",
    firstDownload: "Beim ersten Mal werden {size} MB geladen.",
    off: "Aus", downloading: "Download {percent} %", getting: "Wird vorbereitet", running: "An · {quality}",
    noGpu: "Studio braucht eine Grafikkarte", downloadFailed: "Der Download ist fehlgeschlagen",
    modelFailed: "Studio konnte nicht starten", failed: "Singen konnte nicht starten",
    atmos: "Für Dolby-Atmos-Titel nicht verfügbar", automix: "Pausiert, während AutoMix mischt",
  },
  es: {
    label: "Cantar", karaoke: "Karaoke", solo: "Solo voz", music: "Música", voice: "Voz del cantante", light: "Ligero", studio: "Estudio", quality: "Calidad",
    lightNote: "Instantáneo, calculado por el procesador; la voz y la música quedan algo mezcladas.",
    studioNote: "La tarjeta gráfica separa casi por completo la voz de la música.",
    model: "Modelo", standard: "Estándar", precise: "Preciso",
    preciseNote: "Separa la voz de la música mucho mejor. Recomendado: una tarjeta gráfica de los últimos años (GeForce RTX 2060 o RTX 3050, Radeon RX 6600 o superior).",
    firstDownload: "La primera vez se descargan {size} MB.",
    off: "Desactivado", downloading: "Descargando {percent} %", getting: "Preparando", running: "Activado · {quality}",
    noGpu: "Estudio necesita una tarjeta gráfica", downloadFailed: "La descarga falló",
    modelFailed: "Estudio no pudo iniciarse", failed: "Cantar no pudo iniciarse",
    atmos: "No disponible para canciones Dolby Atmos", automix: "En pausa mientras AutoMix mezcla",
  },
  pt: {
    label: "Cantar", karaoke: "Karaokê", solo: "Só a voz", music: "Música", voice: "Voz do cantor", light: "Leve", studio: "Estúdio", quality: "Qualidade",
    lightNote: "Instantâneo, calculado pelo processador; a voz e a música ficam um pouco misturadas.",
    studioNote: "A placa de vídeo separa quase totalmente a voz da música.",
    model: "Modelo", standard: "Padrão", precise: "Preciso",
    preciseNote: "Separa a voz da música muito melhor. Recomendado: uma placa de vídeo dos últimos anos (GeForce RTX 2060 ou RTX 3050, Radeon RX 6600 ou melhor).",
    firstDownload: "No primeiro uso, baixa {size} MB.",
    off: "Desligado", downloading: "Baixando {percent} %", getting: "Preparando", running: "Ligado · {quality}",
    noGpu: "O Estúdio precisa de uma placa de vídeo", downloadFailed: "O download falhou",
    modelFailed: "O Estúdio não pôde iniciar", failed: "Cantar não pôde iniciar",
    atmos: "Indisponível para músicas Dolby Atmos", automix: "Em pausa enquanto o AutoMix mistura",
  },
  ru: {
    label: "Петь", karaoke: "Караоке", solo: "Только голос", music: "Музыка", voice: "Голос певца", light: "Лёгкий", studio: "Студия", quality: "Качество",
    lightNote: "Мгновенно, на процессоре; голос и музыка остаются немного смешанными.",
    studioNote: "Видеокарта почти полностью отделяет голос от музыки.",
    model: "Модель", standard: "Обычная", precise: "Точная",
    preciseNote: "Отделяет голос от музыки намного лучше. Рекомендуется видеокарта последних лет (GeForce RTX 2060 или RTX 3050, Radeon RX 6600 или лучше).",
    firstDownload: "При первом запуске загружается {size} МБ.",
    off: "Выкл.", downloading: "Загрузка {percent} %", getting: "Подготовка", running: "Вкл. · {quality}",
    noGpu: "Для режима «Студия» нужна видеокарта", downloadFailed: "Не удалось загрузить",
    modelFailed: "Не удалось запустить «Студию»", failed: "Не удалось запустить режим пения",
    atmos: "Недоступно для песен Dolby Atmos", automix: "Пауза, пока AutoMix сводит песни",
  },
  ja: {
    label: "歌う", karaoke: "カラオケ", solo: "ボーカルのみ", music: "音楽", voice: "歌手の声", light: "ライト", studio: "スタジオ", quality: "品質",
    lightNote: "すぐに使え、プロセッサーで処理します。声と音楽が少し混ざったままになります。",
    studioNote: "グラフィックスカードが声と音楽をほぼ完全に分けます。",
    model: "モデル", standard: "標準", precise: "高精度",
    preciseNote: "声と音楽をずっときれいに分けます。推奨：ここ数年のグラフィックスカード（GeForce RTX 2060 または RTX 3050、Radeon RX 6600 以上）。",
    firstDownload: "初回は {size} MB をダウンロードします。",
    off: "オフ", downloading: "ダウンロード中 {percent} %", getting: "準備中", running: "オン · {quality}",
    noGpu: "スタジオにはグラフィックスカードが必要です", downloadFailed: "ダウンロードに失敗しました",
    modelFailed: "スタジオを開始できませんでした", failed: "歌うモードを開始できませんでした",
    atmos: "Dolby Atmos の曲では使えません", automix: "AutoMix のミックス中は一時停止",
  },
  zh: {
    label: "演唱", karaoke: "卡拉 OK", solo: "仅人声", music: "音乐", voice: "歌手人声", light: "轻量", studio: "录音室", quality: "质量",
    lightNote: "即时生效，由处理器计算；人声和音乐仍会有些混在一起。",
    studioNote: "显卡几乎完全分开人声和音乐。",
    model: "模型", standard: "标准", precise: "精确",
    preciseNote: "分离人声和音乐的效果好得多。推荐：近几年的显卡（GeForce RTX 2060 或 RTX 3050、Radeon RX 6600 或更好）。",
    firstDownload: "首次使用需下载 {size} MB。",
    off: "关闭", downloading: "正在下载 {percent} %", getting: "正在准备", running: "已开启 · {quality}",
    noGpu: "录音室需要显卡", downloadFailed: "下载失败",
    modelFailed: "录音室无法启动", failed: "演唱模式无法启动",
    atmos: "杜比全景声歌曲不可用", automix: "AutoMix 混音时暂停",
  },
  it: {
    label: "Canta", karaoke: "Karaoke", solo: "Solo voce", music: "Musica", voice: "Voce del cantante", light: "Leggero", studio: "Studio", quality: "Qualità",
    lightNote: "Immediato, calcolato dal processore; voce e musica restano un po' mescolate.",
    studioNote: "La scheda grafica separa quasi del tutto la voce dalla musica.",
    model: "Modello", standard: "Standard", precise: "Preciso",
    preciseNote: "Separa la voce dalla musica molto meglio. Consigliata: una scheda grafica degli ultimi anni (GeForce RTX 2060 o RTX 3050, Radeon RX 6600 o superiore).",
    firstDownload: "Al primo uso scarica {size} MB.",
    off: "Disattivato", downloading: "Download {percent} %", getting: "Preparazione", running: "Attivo · {quality}",
    noGpu: "Studio richiede una scheda grafica", downloadFailed: "Download non riuscito",
    modelFailed: "Studio non è partito", failed: "Canta non è partito",
    atmos: "Non disponibile per i brani Dolby Atmos", automix: "In pausa durante un mix AutoMix",
  },
  ko: {
    label: "노래하기", karaoke: "노래방", solo: "보컬만", music: "음악", voice: "가수 목소리", light: "라이트", studio: "스튜디오", quality: "품질",
    lightNote: "바로 적용되며 프로세서가 처리합니다. 목소리와 음악이 조금 섞여 남습니다.",
    studioNote: "그래픽 카드가 목소리와 음악을 거의 완전히 분리합니다.",
    model: "모델", standard: "표준", precise: "정밀",
    preciseNote: "목소리와 음악을 훨씬 잘 분리합니다. 권장: 최근 몇 년 사이의 그래픽 카드(GeForce RTX 2060 또는 RTX 3050, Radeon RX 6600 이상).",
    firstDownload: "처음 사용할 때 {size} MB를 내려받습니다.",
    off: "끔", downloading: "다운로드 중 {percent} %", getting: "준비 중", running: "켬 · {quality}",
    noGpu: "스튜디오에는 그래픽 카드가 필요합니다", downloadFailed: "다운로드하지 못했습니다",
    modelFailed: "스튜디오를 시작하지 못했습니다", failed: "노래하기를 시작하지 못했습니다",
    atmos: "Dolby Atmos 곡에서는 사용할 수 없습니다", automix: "AutoMix가 믹스하는 동안 일시 중지",
  },
});
const t = (key, values) => Ampify.i18n.t(key, values);

const { h, useState, useEffect } = Ampify.React;
const { Toggle, Slider, Tabs, Text, Spinner, ProgressBar, Divider } = Ampify.components;

const DEFAULT_VOICE = 0.1;
const DEFAULT_MUSIC = 0;  // the singer alone
const POPUP = "sing";
const MIC = "";  // Microphone, in Segoe Fluent Icons

const clampLevel = (value, fallback) =>
  (Number.isFinite(Number(value)) ? Math.min(1, Math.max(0, Number(value))) : fallback);
const clampVoice = (value) => clampLevel(value, DEFAULT_VOICE);
const clampMusic = (value) => clampLevel(value, DEFAULT_MUSIC);

// What the user chose, kept between runs: whether Sing is on, the quality they picked (null: not chosen yet), Studio's
// model, whether the voice or the music goes (solo: the singer alone), and how much of it stays.
const saved = Ampify.storage.get("sing", {}) || {};
const choice = {
  enabled: saved.enabled === true,
  mode: saved.mode === "light" || saved.mode === "studio" ? saved.mode : null,
  model: saved.model === "precise" ? "precise" : "standard",
  voice: clampVoice(saved.voice ?? DEFAULT_VOICE),
  solo: saved.solo === true,
  music: clampMusic(saved.music ?? DEFAULT_MUSIC),
};
const remembered = () => ({ enabled: choice.enabled, mode: choice.mode, model: choice.model, voice: choice.voice,
                            solo: choice.solo, music: choice.music });
// Ampify before 1.5.6 has no music level: the singer alone is not offered there.
const CAN_SOLO = "music" in Ampify.audio.sing.state();
if (!CAN_SOLO) choice.solo = false;
// What Sing is asked for: in karaoke the voice turned down, for the singer alone the music.
const levels = () => (choice.solo ? { voice: 1, music: choice.music } : { voice: choice.voice, music: 1 });

// Studio when this PC can run it (or is not known yet), Light when it cannot.
function qualityFor(state) {
  if (choice.mode) return choice.mode;
  return state.available === false ? "light" : "studio";
}

// Saved once the slider rests: each of its steps would write the file again.
const SAVE_AFTER_MS = 400;
let saving = null;
function save() {
  if (saving !== null) clearTimeout(saving);
  saving = setTimeout(() => {
    saving = null;
    Ampify.storage.set("sing", remembered());
  }, SAVE_AFTER_MS);
}

function apply(state) {
  const mode = choice.enabled ? qualityFor(state) : "off";
  save();
  Ampify.audio.sing.set({ mode, ...levels(), model: choice.model });
}

// The status line's tones: the dot's colour, and the text's when something stands in the way. Darker than Apple's
// system colours so that the text stays readable on the light theme too.
const TONES = { on: "#30A14E", off: "#8E8E93", warn: "#C26A00", error: "#D93025" };
const REASONS = {
  "no-graphics-card": "noGpu", "download-failed": "downloadFailed", "model-failed": "modelFailed",
  "dolby-atmos": "atmos", "automix-mixing": "automix",
};

// The line under the switch, in plain words, and how it looks. A reason beats the phase: it says why Sing is not on.
function status(state) {
  if (state.phase === "downloading") {
    return { text: t("downloading", { percent: Math.round(state.progress * 100) }), tone: "wait" };
  }
  if (REASONS[state.reason]) return { text: t(REASONS[state.reason]), tone: state.phase === "failed" ? "error" : "warn" };
  if (state.phase === "failed") return { text: t("failed"), tone: "error" };
  if (state.phase === "loading" || state.phase === "buffering") return { text: t("getting"), tone: "wait" };
  if (state.phase === "on") return { text: t("running", { quality: t(state.mode === "light" ? "light" : "studio") }), tone: "on" };
  if (state.mode === "studio" && state.available === false) return { text: t("noGpu"), tone: "warn" };
  return { text: t("off"), tone: "off" };
}

// A ring while Sing gets ready, a coloured dot otherwise; the text takes the colour only for a reason or a failure.
// Nothing while Sing is simply off: the switch says it already.
function StatusLine({ state }) {
  const { text, tone } = status(state);
  if (tone === "off") return null;
  const loud = tone === "warn" || tone === "error";
  return h("stack", { gap: 8 },
    h("row", { gap: 8 },
      tone === "wait"
        ? h(Spinner, { size: 16 })
        : h(Text, { text: "●", size: 12, color: TONES[tone], valign: "center" }),
      h(Text, { key: "status", text, variant: "caption", valign: "center", align: "left", grow: true, wrap: true,
                ...(loud ? { color: TONES[tone], weight: 600 } : {}) })),
    state.phase === "downloading" ? h(ProgressBar, { value: Math.round(state.progress * 100) }) : null);
}

// What the quality chosen does, what the precise model asks of the PC, and the download Studio needs the first time.
function qualityNotes(quality, state) {
  if (quality === "light") return [t("lightNote")];
  const notes = [t(choice.model === "precise" ? "preciseNote" : "studioNote")];
  if (state.installed === false && state.phase !== "downloading" && state.downloadSize > 0) {
    notes.push(t("firstDownload", { size: Math.round(state.downloadSize / 1e6) }));
  }
  return notes;
}

const DIMMED = 0.55;  // the settings while Sing is off: still there to prepare, but quieter

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
  return h("stack", { gap: 14, padding: 16, minWidth: 300, maxWidth: 340 },
    h("stack", { gap: 6 },
      h(Toggle, { label: t("label"), checked: choice.enabled, onChange: (on) => change({ enabled: on === true }) }),
      h(StatusLine, { state })),
    h(Divider),
    h("stack", { gap: 14, opacity: choice.enabled ? 1 : DIMMED, transition: "opacity 200ms" },
      !CAN_SOLO ? null : h(Tabs, { key: "part", value: choice.solo ? "solo" : "karaoke",
                options: [{ value: "karaoke", label: t("karaoke") }, { value: "solo", label: t("solo") }],
                onChange: (part) => change({ solo: part === "solo" }) }),
      choice.solo
        ? h(Slider, { key: "music", label: t("music"), unit: " %", min: 0, max: 100, step: 1,
                      value: Math.round(choice.music * 100), onChange: (value) => change({ music: clampMusic(Number(value) / 100) }) })
        : h(Slider, { key: "voice", label: t("voice"), unit: " %", min: 0, max: 100, step: 1,
                      value: Math.round(choice.voice * 100), onChange: (value) => change({ voice: clampVoice(Number(value) / 100) }) }),
      h("stack", { gap: 8 },
        h(Text, { text: t("quality"), variant: "caption", weight: 600 }),
        h(Tabs, { value: quality, options: [{ value: "light", label: t("light") }, { value: "studio", label: t("studio") }],
                  onChange: (mode) => change({ mode }) }),
        quality === "studio"
          ? h(Tabs, { key: "model", value: choice.model,
                      options: [{ value: "standard", label: t("standard") }, { value: "precise", label: t("precise") }],
                      onChange: (model) => change({ model: model === "precise" ? "precise" : "standard" }) })
          : null,
        ...qualityNotes(quality, state).map((note, index) =>
          h(Text, { key: "note" + index, text: note, variant: "caption", wrap: true })))));
}

const root = Ampify.ReactDOM.createRoot("popup:" + POPUP);
const button = Ampify.ui.playbar.addButton({
  label: t("label"),
  icon: MIC,
  active: false,
  player: true,  // in Apple's full-screen player too, before its lyrics button
  onClick: () => root.render(h(SingPopup)),
});

// The button is lit while Sing is asked for, whoever asked (this popup, or another element).
const light = (state) => Ampify.ui.setActive(button, state.mode !== "off");
light(Ampify.audio.sing.state());
const stopWatching = Ampify.audio.sing.onChange(light);

if (choice.enabled) apply(Ampify.audio.sing.state());

Ampify.onStop(() => {
  if (saving !== null) {  // a change still waiting to be saved
    clearTimeout(saving);
    saving = null;
    Ampify.storage.set("sing", remembered());
  }
  stopWatching();
  root.unmount();
  if (choice.enabled) Ampify.audio.sing.set({ mode: "off", voice: choice.voice, music: 1, model: choice.model });  // only what this switch turned on
});
