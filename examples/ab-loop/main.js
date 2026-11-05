// A-B loop, like Spicetify's Loopy Loop: the playback bar button marks where a passage starts, then where it ends, and
// the song goes back to the start each time it reaches the end. A third press, or another song, ends the loop.
Ampify.i18n.define({
  en: { start: "Loop: mark the start", end: "Loop from {start}: mark the end", clear: "Looping {start} to {end}: stop" },
  fr: { start: "Boucle : marquer le début", end: "Boucle depuis {start} : marquer la fin", clear: "Boucle de {start} à {end} : arrêter" },
  de: { start: "Schleife: Anfang setzen", end: "Schleife ab {start}: Ende setzen", clear: "Schleife {start} bis {end}: beenden" },
  es: { start: "Bucle: marcar el inicio", end: "Bucle desde {start}: marcar el final", clear: "Bucle de {start} a {end}: detener" },
  pt: { start: "Loop: marcar o início", end: "Loop desde {start}: marcar o fim", clear: "Loop de {start} a {end}: parar" },
  ru: { start: "Повтор: отметить начало", end: "Повтор с {start}: отметить конец", clear: "Повтор {start}–{end}: остановить" },
  ja: { start: "ループ: 開始位置を設定", end: "{start} からループ: 終了位置を設定", clear: "{start}〜{end} をループ中: 停止" },
  zh: { start: "循环：标记起点", end: "从 {start} 循环：标记终点", clear: "正在循环 {start} 至 {end}：停止" },
});
const t = (key, values) => Ampify.i18n.t(key, values);

const CHECK_MS = 200;     // how often the position is looked at while a loop runs
const MIN_LOOP_MS = 1000; // a shorter passage would only stutter

// The loop: the song it belongs to (its queue entry), its start and its end in ms; -1 while not marked.
let loop = { song: "", startMs: -1, endMs: -1 };
let timer = null;

const songOf = (track) => (track ? String(track.queueItemId || track.id || "") : "");

function clock(ms) {
  const seconds = Math.floor(ms / 1000);
  return Math.floor(seconds / 60) + ":" + String(seconds % 60).padStart(2, "0");
}

function labelOf() {
  if (loop.startMs < 0) return t("start");
  if (loop.endMs < 0) return t("end", { start: clock(loop.startMs) });
  return t("clear", { start: clock(loop.startMs), end: clock(loop.endMs) });
}

function show() {
  Ampify.ui.setLabel(button, labelOf());
  Ampify.ui.setActive(button, loop.startMs >= 0);
}

function stopTimer() {
  if (timer !== null) clearInterval(timer);
  timer = null;
}

function clear() {
  stopTimer();
  loop = { song: "", startMs: -1, endMs: -1 };
  show();
}

// Back to the start once the end is reached. The loop ends with the song.
function check() {
  const state = Ampify.player.state();
  if (songOf(state.track) !== loop.song) {
    clear();
    return;
  }
  if (state.playing && state.positionMs >= loop.endMs) Ampify.player.seek(loop.startMs);
}

function press() {
  const state = Ampify.player.state();
  const song = songOf(state.track);
  const at = Math.max(0, Math.round(state.positionMs || 0));
  if (loop.endMs >= 0) {
    clear();
    return;
  }
  if (!song) return;
  if (loop.startMs < 0 || song !== loop.song) {
    loop = { song, startMs: at, endMs: -1 };
  } else if (Math.abs(at - loop.startMs) >= MIN_LOOP_MS) {
    // Marked backwards, the two points swap: the passage is the same.
    loop = { song, startMs: Math.min(at, loop.startMs), endMs: Math.max(at, loop.startMs) };
    stopTimer();
    timer = setInterval(check, CHECK_MS);
    Ampify.log("looping " + clock(loop.startMs) + " to " + clock(loop.endMs));
  }
  show();
}

// An icon: the bar has no room for the sentence, which shows as its tooltip.
const button = Ampify.ui.playbar.addButton({ label: t("start"), icon: "", onClick: press });  // RepeatOne, in Segoe Fluent Icons
Ampify.player.on("trackchange", (state) => {
  if (loop.song && songOf(state.track) !== loop.song) clear();
});
Ampify.onStop(stopTimer);
