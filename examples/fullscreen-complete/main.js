// Full screen, complete: a full-screen view of the playing song with its lyrics, its Up Next list and the transport
// buttons. Ctrl+Alt+F opens and closes it; inside the view F closes it, L and Q show its lyrics and Up Next columns.
// The controls fade after 3 s without pointer movement and come back on movement. Escape closes the view itself.

Ampify.i18n.define({
  en: { full: "Full screen", lyrics: "Lyrics", queue: "Up next", nothing: "Nothing playing", noLyrics: "No lyrics yet", noQueue: "Open Apple Music's Up Next once to see it here", queueEmpty: "Nothing after this song", play: "Play", pause: "Pause", next: "Next", previous: "Previous", hint: "F or Ctrl+Alt+F: close · L: lyrics · Q: up next" },
  fr: { full: "Plein écran", lyrics: "Paroles", queue: "À suivre", nothing: "Rien en lecture", noLyrics: "Pas encore de paroles", noQueue: "Ouvrez une fois À suivre d'Apple Music pour le voir ici", queueEmpty: "Rien après ce morceau", play: "Lecture", pause: "Pause", next: "Suivant", previous: "Précédent", hint: "F ou Ctrl+Alt+F : fermer · L : paroles · Q : à suivre" },
  de: { full: "Vollbild", lyrics: "Songtext", queue: "Als Nächstes", nothing: "Nichts läuft", noLyrics: "Noch kein Songtext", noQueue: "Öffne „Als Nächstes“ in Apple Music einmal, um es hier zu sehen", queueEmpty: "Danach kommt nichts", play: "Abspielen", pause: "Pause", next: "Weiter", previous: "Zurück", hint: "F oder Strg+Alt+F: schließen · L: Songtext · Q: Als Nächstes" },
  es: { full: "Pantalla completa", lyrics: "Letra", queue: "A continuación", nothing: "No suena nada", noLyrics: "Aún no hay letra", noQueue: "Abre una vez A continuación en Apple Music para verlo aquí", queueEmpty: "Nada después de esta canción", play: "Reproducir", pause: "Pausa", next: "Siguiente", previous: "Anterior", hint: "F o Ctrl+Alt+F: cerrar · L: letra · Q: a continuación" },
  pt: { full: "Tela cheia", lyrics: "Letra", queue: "A seguir", nothing: "Nada a tocar", noLyrics: "Ainda sem letra", noQueue: "Abra uma vez A seguir no Apple Music para vê-lo aqui", queueEmpty: "Nada depois desta música", play: "Tocar", pause: "Pausa", next: "Seguinte", previous: "Anterior", hint: "F ou Ctrl+Alt+F: fechar · L: letra · Q: a seguir" },
  ru: { full: "Во весь экран", lyrics: "Текст", queue: "Далее", nothing: "Ничего не играет", noLyrics: "Текста пока нет", noQueue: "Откройте «Далее» в Apple Music один раз, чтобы увидеть список здесь", queueEmpty: "После этого трека ничего нет", play: "Играть", pause: "Пауза", next: "Следующий", previous: "Предыдущий", hint: "F или Ctrl+Alt+F: закрыть · L: текст · Q: далее" },
  ja: { full: "全画面", lyrics: "歌詞", queue: "次に再生", nothing: "再生していません", noLyrics: "歌詞はまだありません", noQueue: "Apple Music の「次に再生」を一度開くと、ここに表示されます", queueEmpty: "この曲の後はありません", play: "再生", pause: "一時停止", next: "次へ", previous: "前へ", hint: "F / Ctrl+Alt+F: 閉じる · L: 歌詞 · Q: 次に再生" },
  zh: { full: "全屏", lyrics: "歌词", queue: "接下来", nothing: "没有正在播放", noLyrics: "暂无歌词", noQueue: "在 Apple Music 中打开一次“接下来”，即可在此显示", queueEmpty: "这首之后没有歌曲", play: "播放", pause: "暂停", next: "下一首", previous: "上一首", hint: "F 或 Ctrl+Alt+F：关闭 · L：歌词 · Q：接下来" },
});

// Sized for the smallest window Apple Music allows (about 1090 x 680 at 125 %): the song column with Up Next under
// the title fits its height, and the three columns side by side fit its width, so nothing is cut at either edge.
const COVER = 220;           // the song's cover
const SONG_WIDTH = 300;      // the song column: wider than the cover, so a title and the key hint keep to one line
const ENTRY_COVER = 48;      // the cover of a song of Up Next
const UNDER_TITLE = 3;       // the songs of Up Next under the title
const QUEUE_LINES = 8;       // the songs of the Up Next column
const LYRICS_WIDTH = 400;    // each side column, its gap from the column before included
const QUEUE_WIDTH = 320;
const COLUMN_GAP = 48;
const IDLE_MS = 3000;
const QUEUE_POLL_MS = 2000;  // Up Next changes without an event: it is looked at again this often while the view is open
const UPCOMING = new Set(["next-added", "next"]);
// view.last is the tree last sent: an identical tree is not sent again, so nothing is rebuilt when nothing changed.
const view = { open: false, lyrics: false, queue: false, idle: false, last: "", timer: null, poll: null };

// The songs still to come, in the order Apple Music will play them: never the current song or the ones played.
function upcoming(queue) {
  if (!queue || !Array.isArray(queue.items)) return [];
  return queue.items.filter((item) => UPCOMING.has(item.section));
}

function textNode(text, variant) {
  return { type: "text", text, variant, wrap: true };
}

// An empty square of the same size without a cover, so the column keeps its shape.
function coverNode(src, size) {
  if (src) return { type: "image", src, width: size, height: size, cornerRadius: size > ENTRY_COVER ? 12 : 6, fit: "cover" };
  return { type: "stack", width: size, height: size, children: [] };
}

// One song of Up Next: its cover, then its title over its artist.
function entryNode(item) {
  const words = {
    type: "stack",
    gap: 2,
    maxWidth: QUEUE_WIDTH - COLUMN_GAP - ENTRY_COVER - 12,
    children: [{ type: "text", text: item.title, variant: "body", wrap: false }, { type: "text", text: item.artist || "", variant: "caption", wrap: false }],
  };
  return { type: "row", key: "entry-" + item.id, gap: 12, align: "start", children: [coverNode(item.artwork, ENTRY_COVER), words] };
}

// Up Next is not loaded until Apple Music has built it, which it does the first time its own Up Next pane opens.
function upNextEmptyText(queue) {
  if (!queue || !Array.isArray(queue.items) || queue.complete === false) return Ampify.i18n.t("noQueue");
  return Ampify.i18n.t("queueEmpty");
}

function entriesOf(queue, count) {
  const items = upcoming(queue).slice(0, count);
  return items.length ? items.map(entryNode) : [textNode(upNextEmptyText(queue), "body")];
}

function upNextNode(queue) {
  return { type: "stack", key: "upnext", gap: 8, children: [textNode(Ampify.i18n.t("queue"), "caption"), ...entriesOf(queue, UNDER_TITLE)] };
}

function transportNode(state) {
  const label = state.playing ? Ampify.i18n.t("pause") : Ampify.i18n.t("play");
  return {
    type: "row",
    gap: 8,
    children: [
      { type: "button", label: Ampify.i18n.t("previous"), variant: "tertiary", onClick: () => Ampify.player.previous() },
      { type: "button", label, variant: "primary", onClick: () => Ampify.player.toggle() },
      { type: "button", label: Ampify.i18n.t("next"), variant: "tertiary", onClick: () => Ampify.player.next() },
    ],
  };
}

// The buttons and the key hint fade out after IDLE_MS without pointer movement, and come back with it. Faded, they
// are inert: a click there reaches the view, not an invisible button.
function controlsNode(state) {
  return {
    type: "stack",
    key: "controls",
    gap: 8,
    opacity: view.idle ? 0 : 1,
    inert: view.idle,
    transition: "opacity 400ms",
    children: [transportNode(state), textNode(Ampify.i18n.t("hint"), "caption")],
  };
}

// The cover is "artwork": Apple's own image of the song playing, which follows each song and fades from the previous
// one. Up Next sits under the title only while its own column is closed, so it is never shown twice.
function songNode(state, queue) {
  const track = state.track;
  const children = [
    coverNode(track ? "artwork" : "", COVER),
    textNode(track ? track.title : Ampify.i18n.t("nothing"), "title"),
    textNode(track ? track.artist : "", "subtitle"),
  ];
  if (!view.queue) children.push(upNextNode(queue));
  children.push(controlsNode(state));
  return { type: "stack", key: "song", gap: 12, width: SONG_WIDTH, valign: "center", children };
}

// A closed column is an empty stack: the view always has three columns, so opening one never rebuilds the others.
function closedColumn(key) {
  return { type: "stack", key, children: [] };
}

// The lyrics node lights Apple's lyrics word by word and scrolls by itself within the height it is given.
function lyricsColumn() {
  if (!view.lyrics) return closedColumn("lyrics");
  return {
    type: "stack",
    key: "lyrics",
    gap: 12,
    width: LYRICS_WIDTH,
    padding: `0,0,0,${COLUMN_GAP}`,
    valign: "stretch",
    children: [textNode(Ampify.i18n.t("lyrics"), "subtitle"), { type: "lyrics", grow: true, size: 22, placeholder: Ampify.i18n.t("noLyrics") }],
  };
}

function queueColumn(queue) {
  if (!view.queue) return closedColumn("queue");
  return {
    type: "stack",
    key: "queue",
    gap: 10,
    width: QUEUE_WIDTH,
    padding: `0,0,0,${COLUMN_GAP}`,
    valign: "center",
    children: [textNode(Ampify.i18n.t("queue"), "subtitle"), ...entriesOf(queue, QUEUE_LINES)],
  };
}

// The view as a tree of nodes, centred in the window and as tall as it, from what the player holds now.
function buildView(state, queue) {
  return {
    type: "row",
    gap: 0,
    padding: "0,24,16,24",
    align: "center",
    valign: "stretch",
    children: [songNode(state, queue), lyricsColumn(), queueColumn(queue)],
  };
}

function refresh() {
  if (!view.open) return;
  const tree = buildView(Ampify.player.state(), Ampify.player.queue.list());
  const text = JSON.stringify(tree);
  if (text === view.last) return;
  view.last = text;
  Ampify.ui.fullscreen.open(tree);
}

function clearTimers() {
  if (view.timer !== null) clearTimeout(view.timer);
  if (view.poll !== null) clearTimeout(view.poll);
  view.timer = null;
  view.poll = null;
}

function armIdle() {
  if (view.timer !== null) clearTimeout(view.timer);
  view.timer = setTimeout(() => {
    view.timer = null;
    view.idle = true;
    refresh();
  }, IDLE_MS);
}

function armPoll() {
  view.poll = setTimeout(() => {
    view.poll = null;
    if (!view.open) return;
    refresh();
    armPoll();
  }, QUEUE_POLL_MS);
}

// Each pointer move (passed at most every 250 ms by the kernel) brings the controls back and restarts the 3 s wait.
function pointerMoved() {
  if (!view.open) return;
  if (view.idle) {
    view.idle = false;
    refresh();
  }
  armIdle();
}

function show(on) {
  view.open = on;
  view.last = "";
  view.idle = false;
  clearTimers();
  if (on) {
    refresh();
    armIdle();
    armPoll();
  } else {
    Ampify.ui.fullscreen.close();
  }
}

function toggleFullScreen() {
  show(!view.open);
}

function togglePane(name) {
  view[name] = !view[name];
  if (!view.open) show(true);
  else refresh();
}

// F, L and Q, while the view has the keyboard.
function keyPressed(key) {
  if (!view.open) return;
  if (key === "F") return toggleFullScreen();
  // A key counts as activity, like a pointer move: the controls come back and the idle wait restarts.
  pointerMoved();
  if (key === "L") togglePane("lyrics");
  else if (key === "Q") togglePane("queue");
}

Ampify.ui.fullscreen.onClose(() => {
  view.open = false;
  view.last = "";
  clearTimers();
});
Ampify.ui.fullscreen.onKey(keyPressed);
Ampify.ui.fullscreen.onPointer(pointerMoved);
Ampify.keyboard.bind("Ctrl+Alt+F", toggleFullScreen);
Ampify.keyboard.bind("Ctrl+Alt+L", () => togglePane("lyrics"));
Ampify.keyboard.bind("Ctrl+Alt+Q", () => togglePane("queue"));
Ampify.player.on("trackchange", refresh);
Ampify.player.on("statechange", refresh);
