// Song panel: a button of the playback bar that slides in a column about the song playing, as Spotify's Now Playing
// view: the cover, the lyrics lit word by word, the artist, the credits and what comes next. The album, year, genre
// and composer come from Apple Music's own library file; the lyrics and the queue from Apple's player.
const { h, useState, useEffect } = Ampify.React;

Ampify.i18n.define({
  en: { label: "Song panel", lyrics: "Lyrics", noLyrics: "No lyrics for this song yet", artist: "About the artist",
        credits: "Credits", next: "Next in queue", nothing: "Nothing is playing", performer: "Performed by",
        composer: "Written by", albumArtist: "Album artist", inLibrary: "{count} songs in your library",
        plays: "{count} plays", top: "Most played: {title}", empty: "Nothing after this song" },
  fr: { label: "Panneau du morceau", lyrics: "Paroles", noLyrics: "Pas encore de paroles pour ce morceau",
        artist: "À propos de l'artiste", credits: "Crédits", next: "À suivre", nothing: "Rien en lecture",
        performer: "Interprété par", composer: "Écrit par", albumArtist: "Artiste de l'album",
        inLibrary: "{count} morceaux dans votre bibliothèque", plays: "{count} écoutes", top: "Le plus écouté : {title}",
        empty: "Rien après ce morceau" },
  de: { label: "Titelleiste", lyrics: "Songtext", noLyrics: "Noch kein Songtext für diesen Titel",
        artist: "Über den Künstler", credits: "Mitwirkende", next: "Als Nächstes", nothing: "Nichts wird abgespielt",
        performer: "Gespielt von", composer: "Geschrieben von", albumArtist: "Album-Künstler",
        inLibrary: "{count} Titel in deiner Mediathek", plays: "{count}-mal gehört", top: "Am meisten gehört: {title}", empty: "Nichts nach diesem Titel" },
  es: { label: "Panel de la canción", lyrics: "Letra", noLyrics: "Aún no hay letra para esta canción",
        artist: "Sobre el artista", credits: "Créditos", next: "A continuación", nothing: "No se reproduce nada",
        performer: "Interpretada por", composer: "Escrita por", albumArtist: "Artista del álbum",
        inLibrary: "{count} canciones en tu biblioteca", plays: "{count} reproducciones", top: "La más escuchada: {title}",
        empty: "Nada después de esta canción" },
  pt: { label: "Painel da música", lyrics: "Letra", noLyrics: "Ainda não há letra para esta música",
        artist: "Sobre o artista", credits: "Créditos", next: "A seguir", nothing: "Nada está tocando",
        performer: "Interpretada por", composer: "Escrita por", albumArtist: "Artista do álbum",
        inLibrary: "{count} músicas na sua biblioteca", plays: "{count} reproduções", top: "A mais ouvida: {title}", empty: "Nada depois desta música" },
  ru: { label: "Панель песни", lyrics: "Текст", noLyrics: "Для этой песни пока нет текста", artist: "Об исполнителе",
        credits: "Авторы", next: "Далее", nothing: "Ничего не играет", performer: "Исполняет", composer: "Автор",
        albumArtist: "Исполнитель альбома", inLibrary: "Песен в медиатеке: {count}", plays: "Прослушиваний: {count}", top: "Чаще всего: {title}",
        empty: "После этой песни ничего нет" },
  ja: { label: "曲パネル", lyrics: "歌詞", noLyrics: "この曲の歌詞はまだありません", artist: "アーティストについて",
        credits: "クレジット", next: "次に再生", nothing: "再生していません", performer: "演奏", composer: "作詞・作曲",
        albumArtist: "アルバムアーティスト", inLibrary: "ライブラリに {count} 曲", plays: "{count} 回再生", top: "最も再生：{title}",
        empty: "この曲の後には何もありません" },
  zh: { label: "歌曲面板", lyrics: "歌词", noLyrics: "这首歌暂时没有歌词", artist: "关于艺人", credits: "制作人员",
        next: "接下来播放", nothing: "没有在播放", performer: "演唱", composer: "词曲", albumArtist: "专辑艺人",
        inLibrary: "资料库中有 {count} 首歌", plays: "播放 {count} 次", top: "最常播放：{title}", empty: "这首歌之后没有内容" },
});
const t = (key, values) => Ampify.i18n.t(key, values);

const PANEL = "song";
const SIDE = 348;                 // the column is 380 wide, its frame keeps 16 on each side
const LYRICS_HEIGHT = 380;
const QUEUE_SHOWN = 4;
const QUEUE_EVERY_MS = 5000;      // Apple's queue changes without an event: read again now and then
const COLORS_AGAIN_MS = 2000;
const SECTIONS_AFTER =["next-added", "next", "autoplay"];
const TEXT = "#FFFFFF";
const SOFT = "#BFFFFFFF";         // secondary text, white at 75 %
const FAINT = "#80FFFFFF";
const CARD = "#40000000";         // a dark card over the tinted column

// The song's colour, darkened so white text stays readable on it (contrast above 4.5:1 on the darkest cards).
function tint(color, factor) {
  const match = /^#([0-9A-Fa-f]{6})$/.exec(color || "");
  const rgb = match ? match[1] : "2A2A2E";
  const part = (i) => Math.round(parseInt(rgb.slice(i, i + 2), 16) * factor).toString(16).padStart(2, "0");
  return "#FF" + part(0) + part(2) + part(4);
}

const same = (a, b) => String(a || "").trim().toLowerCase() === String(b || "").trim().toLowerCase();

// The library's entries for the song and its artist: the file Apple Music keeps, read once per song.
function useLibrary(track) {
  const [found, setFound] = useState({ song: null, artistSongs: [] });
  const key = track ? track.title + "|" + track.artist : "";
  useEffect(() => {
    if (!track) return undefined;
    let live = true;
    setFound({ song: null, artistSongs: [] });  // the previous song's year and credits would show under the new title
    Ampify.library.tracks().then((tracks) => {
      if (!live) return;
      const song = tracks.find((x) => same(x.title, track.title) && same(x.artist, track.artist)) ||
                   tracks.find((x) => same(x.title, track.title) && same(x.album, track.album)) || null;
      const artistSongs = tracks.filter((x) => same(x.artist, track.artist) || same(x.albumArtist, track.artist));
      setFound({ song, artistSongs });
    }).catch((error) => Ampify.log("library not read: " + error));
    return () => { live = false; };
  }, [key]);
  return found;
}

// The cover's own colours (Apple gives none for the songs of the library). Windows may still hold the previous cover
// right after a song changes, so they are read again a moment later.
function useCoverColors(trackKey) {
  const [colors, setColors] = useState(null);
  useEffect(() => {
    let live = true;
    const read = () => Ampify.color.playing()
      .then((found) => { if (live) setColors(found); })
      .catch((error) => Ampify.log("cover colours not read: " + error));
    read();
    const again = setTimeout(read, COLORS_AGAIN_MS);
    return () => { live = false; clearTimeout(again); };
  }, [trackKey]);
  return colors;
}

function useQueue(trackId) {
  const [items, setItems] = useState([]);
  useEffect(() => {
    const read = () => {
      const queue = Ampify.player.queue.list();
      setItems((queue.items || []).filter((item) => SECTIONS_AFTER.includes(item.section)).slice(0, QUEUE_SHOWN));
    };
    read();
    const timer = setInterval(read, QUEUE_EVERY_MS);
    return () => clearInterval(timer);
  }, [trackId]);
  return items;
}

function Card({ title, children, background }) {
  return h("stack", { background: background || CARD, cornerRadius: 12, padding: 16, gap: 10 },
    h("text", { text: title, size: 15, weight: 700, color: TEXT }),
    ...[].concat(children));
}

function Fact({ label, value }) {
  return h("stack", { gap: 2 },
    h("text", { text: value, size: 15, weight: 600, color: TEXT, wrap: true }),
    h("text", { text: label, size: 12, color: FAINT }));
}

function Header({ track, song }) {
  const details = [track.album, song && song.year ? String(song.year) : "", song && song.genre].filter(Boolean);
  return h("stack", { gap: 4 },
    h("text", { text: track.title, size: 22, weight: 800, color: TEXT, maxLines: 2 }),
    h("text", { text: track.artist, size: 15, weight: 600, color: SOFT, wrap: false }),
    details.length ? h("text", { text: details.join(" · "), size: 13, color: FAINT, wrap: false }) : null);
}

function ArtistCard({ track, artistSongs }) {
  const plays = artistSongs.reduce((sum, x) => sum + (x.playCount || 0), 0);
  const top = artistSongs.slice().sort((a, b) => (b.playCount || 0) - (a.playCount || 0))[0];
  return h(Card, { title: t("artist") },
    h("text", { text: track.artist, size: 20, weight: 800, color: TEXT, wrap: true }),
    artistSongs.length ? h("text", { text: t("inLibrary", { count: artistSongs.length }) + " · " +
                                           t("plays", { count: plays }), size: 13, color: SOFT }) : null,
    top && top.playCount ? h("text", { text: t("top", { title: top.title }), size: 13, color: FAINT, wrap: false }) : null);
}

function CreditsCard({ track, song }) {
  const facts = [h(Fact, { key: "p", label: t("performer"), value: track.artist })];
  if (song && song.composer) facts.push(h(Fact, { key: "c", label: t("composer"), value: song.composer }));
  if (song && song.albumArtist && !same(song.albumArtist, track.artist)) {
    facts.push(h(Fact, { key: "a", label: t("albumArtist"), value: song.albumArtist }));
  }
  return h(Card, { title: t("credits") }, facts);
}

function QueueCard({ items }) {
  const rows = items.length === 0
    ? [h("text", { key: "none", text: t("empty"), size: 13, color: FAINT })]
    : items.map((item, i) => h("stack", { key: "q" + i + item.id, gap: 1 },
        h("text", { text: item.title, size: 14, weight: 600, color: TEXT, wrap: false }),
        h("text", { text: item.artist, size: 12, color: FAINT, wrap: false })));
  return h(Card, { title: t("next") }, rows);
}

function SongPanel() {
  const player = Ampify.hooks.usePlayer();
  const track = player && player.track;
  const { song, artistSongs } = useLibrary(track);
  const trackKey = track ? track.id + track.queueItemId : "";
  const queue = useQueue(trackKey);
  const colors = useCoverColors(trackKey);
  if (!track) return h("text", { text: t("nothing"), size: 14, color: SOFT });
  const base = tint(colors ? colors.DARK_VIBRANT : track.artworkColor, 0.7);
  const lyricsTint = tint(colors ? colors.VIBRANT_NON_ALARMING : track.artworkColor, 0.5);
  return h("stack", { background: base, cornerRadius: 12, padding: 12, gap: 12 },
    h("image", { src: "artwork", fit: "cover", cornerRadius: 8, width: SIDE - 24, height: SIDE - 24,
                 label: track.album || track.title }),
    h(Header, { track, song }),
    h("stack", { background: lyricsTint, cornerRadius: 12, padding: 16, gap: 8, height: LYRICS_HEIGHT },
      h("text", { text: t("lyrics"), size: 15, weight: 700, color: TEXT }),
      // A line clicked: the song goes there.
      h("lyrics", { grow: true, size: 22, color: TEXT, placeholder: t("noLyrics"),
                    onChange: (event) => Ampify.player.seek(Number(event.value)) })),
    h(ArtistCard, { track, artistSongs }),
    h(CreditsCard, { track, song }),
    h(QueueCard, { items: queue }));
}

// The button opens and closes the panel; the panel slides in from the right and back out.
let open = Ampify.storage.get("open") === true;
const root = Ampify.ReactDOM.createRoot("panel:" + PANEL);
Ampify.ui.title.set(t("label"));
const button = Ampify.ui.playbar.addButton({
  label: t("label"),
  icon: "",  // OpenPane, in Segoe Fluent Icons
  active: open,
  onClick: () => setOpen(!open),
});

function setOpen(on) {
  open = on;
  Ampify.storage.set("open", on);
  Ampify.ui.setActive(button, on);
  if (on) root.render(h(SongPanel));
  else root.unmount();
}

// Closed with its own Close button: the button goes off, so one click brings the panel back.
Ampify.ui.panel.onClose(PANEL, () => {
  open = false;
  Ampify.storage.set("open", false);
  Ampify.ui.setActive(button, false);
});

if (open) root.render(h(SongPanel));
