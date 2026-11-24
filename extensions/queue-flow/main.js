// Queue flow: a button of the playback bar that sorts the songs to come so that each one leads into the next, from
// Apple's own analysis of every song (Ampify.music.analysis): tempos close enough to mix on the beat, keys that sit
// together, no jump in energy or loudness, no two voices meeting at the change. The whole order is planned at once,
// so the last songs get good neighbours too, then Apple's queue is rearranged entry by entry. A second click puts the
// queue back as it was.

Ampify.i18n.define({
  en: { sort: "Sort the queue for flow", restore: "Put the queue back", sorting: "Sorting the queue…",
        sorted: "Queue sorted for flow: {count} songs", restored: "Queue put back in its order",
        nothing: "Not enough songs to come to sort", failed: "The queue could not be sorted: {reason}",
        old: "This needs Ampify 1.6 or later" },
  fr: { sort: "Trier la file pour l'enchaînement", restore: "Remettre la file", sorting: "Tri de la file…",
        sorted: "File triée pour l'enchaînement : {count} morceaux", restored: "File remise dans son ordre",
        nothing: "Pas assez de morceaux à venir pour trier", failed: "La file n'a pas pu être triée : {reason}",
        old: "Il faut Ampify 1.6 ou plus récent" },
  de: { sort: "Warteschlange für Übergänge sortieren", restore: "Warteschlange zurücksetzen", sorting: "Warteschlange wird sortiert…",
        sorted: "Warteschlange sortiert: {count} Titel", restored: "Warteschlange in alter Reihenfolge",
        nothing: "Zu wenige kommende Titel zum Sortieren", failed: "Die Warteschlange ließ sich nicht sortieren: {reason}",
        old: "Dafür ist Ampify 1.6 oder neuer nötig" },
  es: { sort: "Ordenar la cola para enlazar", restore: "Restaurar la cola", sorting: "Ordenando la cola…",
        sorted: "Cola ordenada: {count} canciones", restored: "Cola restaurada en su orden",
        nothing: "No hay suficientes canciones por venir", failed: "No se pudo ordenar la cola: {reason}",
        old: "Hace falta Ampify 1.6 o posterior" },
  pt: { sort: "Ordenar a fila para encadear", restore: "Restaurar a fila", sorting: "Ordenando a fila…",
        sorted: "Fila ordenada: {count} músicas", restored: "Fila restaurada na ordem original",
        nothing: "Não há músicas suficientes a seguir", failed: "Não foi possível ordenar a fila: {reason}",
        old: "É preciso o Ampify 1.6 ou mais recente" },
  ru: { sort: "Упорядочить очередь для переходов", restore: "Вернуть очередь", sorting: "Очередь сортируется…",
        sorted: "Очередь упорядочена: {count} песен", restored: "Очередь возвращена в исходный порядок",
        nothing: "Слишком мало песен впереди", failed: "Не удалось упорядочить очередь: {reason}",
        old: "Нужен Ampify 1.6 или новее" },
  ja: { sort: "つながりよく再生キューを並べ替え", restore: "再生キューを元に戻す", sorting: "再生キューを並べ替え中…",
        sorted: "再生キューを並べ替えました：{count} 曲", restored: "再生キューを元の順に戻しました",
        nothing: "並べ替える曲が足りません", failed: "再生キューを並べ替えられませんでした：{reason}",
        old: "Ampify 1.6 以降が必要です" },
  zh: { sort: "按衔接排列播放队列", restore: "恢复播放队列", sorting: "正在排列播放队列…",
        sorted: "播放队列已排列：{count} 首歌", restored: "播放队列已恢复原顺序",
        nothing: "接下来的歌曲太少，无法排列", failed: "无法排列播放队列：{reason}", old: "需要 Ampify 1.6 或更高版本" },
  it: { sort: "Ordina la coda per gli stacchi", restore: "Ripristina la coda", sorting: "Ordinamento della coda…",
        sorted: "Coda ordinata: {count} brani", restored: "Coda ripristinata nel suo ordine",
        nothing: "Troppo pochi brani in arrivo da ordinare", failed: "Impossibile ordinare la coda: {reason}",
        old: "Serve Ampify 1.6 o successivo" },
  ko: { sort: "자연스럽게 이어지도록 대기열 정렬", restore: "대기열 되돌리기", sorting: "대기열 정렬 중…",
        sorted: "대기열 정렬됨: {count}곡", restored: "대기열을 원래 순서로 되돌림",
        nothing: "정렬할 다음 곡이 부족합니다", failed: "대기열을 정렬하지 못했습니다: {reason}", old: "Ampify 1.6 이상이 필요합니다" },
});
const t = (key, values) => Ampify.i18n.t(key, values);

const TO_COME = ["next-added", "next"];  // the songs the user queued or Apple queued from the playlist, not autoplay
const MOST = 50;                         // Apple's analysis answers 50 songs at once
const LEAST = 3;
const EDGE_MS = 20000;                   // the part of a song a change of song is heard over
const BEAT_GAP = 0.08;                   // tempos this close mix on the beat, as DJs glide them
const MISSING = 2;                       // the cost of a change of song without Apple's analysis of one side
const MOVE_RUN = 3;                      // the longest run of songs moved at once while improving the order
const ICON = "";                   // Sort, in Segoe Fluent Icons

// ---- What a song brings to a change of song, from Apple's analysis

// The tonic's semitone above C ("F#" -> 6), null for anything else.
function pitchClass(tonic) {
  const found = /^([A-Ga-g])([#♯b♭]?)$/.exec(String(tonic || "").trim());
  if (!found) return null;
  const base = { C: 0, D: 2, E: 4, F: 5, G: 7, A: 9, B: 11 }[found[1].toUpperCase()];
  const shift = found[2] === "#" || found[2] === "♯" ? 1 : found[2] ? -1 : 0;
  return (base + shift + 12) % 12;
}

// A key's place on the circle of fifths, as DJs number it (Camelot: 1 to 12, B major, A minor), null when unknown.
function camelot(key) {
  const pitch = key ? pitchClass(key.tonic) : null;
  if (pitch === null || (key.mode !== "major" && key.mode !== "minor")) return null;
  const major = key.mode === "major" ? pitch : (pitch + 3) % 12;  // a minor key sits with its relative major
  return { number: ((major * 7) % 12 + 7) % 12 + 1, minor: key.mode === "minor" };
}

// Steps between two keys on the wheel: 0 the same, 1 a neighbour or the relative key.
function keyDistance(a, b) {
  if (!a || !b) return null;
  const around = Math.abs(a.number - b.number);
  const steps = Math.min(around, 12 - around);
  return a.minor === b.minor ? steps : steps + 1;
}

// The gap between two tempos, a half or double tempo counting as the same.
function tempoGap(a, b) {
  if (!(a > 0) || !(b > 0)) return null;
  let ratio = b / a;
  while (ratio > 1.5) ratio /= 2;
  while (ratio < 0.67) ratio *= 2;
  return Math.max(Math.abs(ratio - 1), Math.abs(1 / ratio - 1));
}

const sung = (spans, from, to) => (spans || []).some((span) =>
  ["medium", "high", "very-high"].includes(span.strength) &&
  span.startInMilliseconds < to && span.endInMilliseconds > from);

// The two ends of a song: how it starts (head) and how it ends (tail).
function edges(analysis, durationMs) {
  if (!analysis) return null;
  const part = (value, name) => (value && typeof value === "object" ? value[name] : undefined);
  const end = durationMs > 0 ? durationMs : Infinity;
  const side = (name, from, to) => ({
    bpm: part(analysis.bpm, name),
    key: camelot(part(analysis.key, name)),
    energy: part(analysis.energy, name),
    loudness: part(part(analysis.loudness, name), "value"),
    vocals: sung(analysis.vocalActivity, from, to),
  });
  return { head: side("beginning", 0, EDGE_MS), tail: side("ending", end - EDGE_MS, end) };
}

// What changing from a song ending `tail` to one starting `head` costs: 0 for a change a DJ would love.
function changeCost(tail, head) {
  if (!tail || !head) return MISSING;
  let cost = 0;
  const gap = tempoGap(tail.bpm, head.bpm);
  cost += gap === null ? 1 : gap <= BEAT_GAP ? gap / BEAT_GAP : 1.5 + Math.min(gap, 0.5);
  const steps = keyDistance(tail.key, head.key);
  cost += steps === null ? 0.5 : [0, 0.2, 0.7][steps] ?? 1.2;
  if (typeof tail.energy === "number" && typeof head.energy === "number") cost += Math.abs(tail.energy - head.energy);
  if (typeof tail.loudness === "number" && typeof head.loudness === "number") {
    cost += Math.min(Math.abs(tail.loudness - head.loudness) / 12, 1) * 0.5;
  }
  if (tail.vocals && head.vocals) cost += 0.5;
  return cost;
}

// ---- The order

// The order of `songs` (their edges, or null) after the song playing (`current`, its edges or null) that costs least
// over every change of song: nearest neighbour first, then runs of up to three songs moved while that helps.
function plan(current, songs) {
  const count = songs.length;
  // Each change of song costs the same in every order: worked out once (row 0 is the song playing).
  const costs = [current && current.tail, ...songs.map((song) => song && song.tail)]
    .map((tail) => songs.map((song) => changeCost(tail, song && song.head)));
  const cost = (from, to) => costs[from === null ? 0 : from + 1][to];
  const total = (order) => order.reduce((sum, song, at) => sum + cost(at === 0 ? null : order[at - 1], song), 0);
  const left = [...Array(count).keys()];
  let order = [];
  let previous = null;
  while (left.length) {
    let best = 0;
    for (let i = 1; i < left.length; i++) if (cost(previous, left[i]) < cost(previous, left[best])) best = i;
    previous = left.splice(best, 1)[0];
    order.push(previous);
  }
  let score = total(order);
  for (let improved = true; improved;) {
    improved = false;
    for (let length = 1; length <= Math.min(MOVE_RUN, count - 1); length++) {
      for (let from = 0; from + length <= count; from++) {
        for (let to = 0; to + length <= count; to++) {
          if (to === from) continue;
          const candidate = order.slice();
          const run = candidate.splice(from, length);
          candidate.splice(to, 0, ...run);
          const value = total(candidate);
          if (value + 1e-9 < score) [order, score, improved] = [candidate, value, true];
        }
      }
    }
  }
  return order;
}

// ---- Apple's queue

function readQueue() {
  const queue = Ampify.player.queue.list();
  const items = queue.items || [];
  const current = items.find((item) => item.section === "current") || null;
  const toCome = items.filter((item) => TO_COME.includes(item.section)).slice(0, MOST);
  return { current, toCome };
}

// Lays the entries out in `ids` order: Apple's queue moves an entry right before another one, so each goes before
// the last, which stays where it is. Only ids are used: the queue read between two moves may still be the old one.
// Stops at the first refusal; says whether it went through and how many entries moved.
function arrange(ids) {
  const last = ids[ids.length - 1];
  let moved = 0;
  for (const id of ids.slice(0, -1)) {
    if (!Ampify.player.queue.move(id, last)) return { done: false, moved };
    moved++;
  }
  return { done: true, moved };
}

// What happened, in a Windows notification and in the log.
function tell(message) {
  Ampify.log(message);
  Ampify.notify({ title: t("sort"), text: message });
}

let busy = false;
let sortedFrom = null;  // the entries' order before the last sort, to put it back

async function sortQueue() {
  const { current, toCome } = readQueue();
  if (!current || toCome.length < LEAST) return t("nothing");
  const state = Ampify.player.state();
  const ids = toCome.map((item) => item.catalogId).concat(state.track ? [state.track.catalogId] : []);
  const analyses = await Ampify.music.analysis(ids.filter(Boolean));
  const songs = toCome.map((item) => edges(analyses[item.catalogId], item.durationMs));
  const playing = state.track ? edges(analyses[state.track.catalogId], state.durationMs) : null;
  const order = plan(playing, songs).map((index) => toCome[index].id);
  const before = { ids: toCome.map((item) => item.id) };
  const { done, moved } = arrange(order);
  if (moved > 0) sortedFrom = before;  // a sort stopped halfway can be put back too
  if (!done) return t("failed", { reason: "Apple Music refused a move" });
  return t("sorted", { count: order.length });
}

function restoreQueue() {
  const { toCome } = readQueue();
  const still = new Set(toCome.map((item) => item.id));
  const ids = sortedFrom.ids.filter((id) => still.has(id));
  sortedFrom = null;
  if (ids.length < 2 || !arrange(ids).done) return t("failed", { reason: "Apple Music refused a move" });
  return t("restored");
}

const button = Ampify.ui.playbar.addButton({ label: t("sort"), icon: ICON, onClick: () => {
  if (busy) return;
  if (!Ampify.music || typeof Ampify.player.queue.move !== "function") {
    tell(t("old"));
    return;
  }
  busy = true;
  Ampify.ui.setLabel(button, t("sorting"));
  const work = sortedFrom ? Promise.resolve().then(restoreQueue) : sortQueue();
  work.then(tell, (error) => tell(t("failed", { reason: String(error && error.message || error) })))
    .finally(() => {
      busy = false;
      Ampify.ui.setLabel(button, sortedFrom ? t("restore") : t("sort"));
      Ampify.ui.setActive(button, Boolean(sortedFrom));
    });
} });

// A new song or a queue changed by hand makes the saved order meaningless: the button sorts again.
Ampify.player.on("trackchange", () => {
  if (!sortedFrom || busy) return;
  sortedFrom = null;
  Ampify.ui.setLabel(button, t("sort"));
  Ampify.ui.setActive(button, false);
});
