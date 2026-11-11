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
  it: { label: "Pannello del brano", lyrics: "Testi", noLyrics: "Ancora nessun testo per questo brano",
        artist: "Info sull'artista", credits: "Crediti", next: "Prossimi in coda", nothing: "Non è in riproduzione nulla",
        performer: "Eseguito da", composer: "Scritto da", albumArtist: "Artista dell'album",
        inLibrary: "{count} brani nella tua libreria", plays: "{count} riproduzioni", top: "Il più ascoltato: {title}",
        empty: "Nulla dopo questo brano" },
  ko: { label: "곡 패널", lyrics: "가사", noLyrics: "이 곡의 가사가 아직 없습니다",
        artist: "아티스트 정보", credits: "크레딧", next: "다음 곡", nothing: "재생 중인 곡이 없습니다",
        performer: "연주", composer: "작곡", albumArtist: "앨범 아티스트",
        inLibrary: "보관함에 곡 {count}개", plays: "{count}회 재생", top: "가장 많이 들은 곡: {title}",
        empty: "이 곡 다음에는 아무것도 없습니다" },
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
const GRADIENT_TOP = 0.85;        // the song's colour at the top of the panel, darkened this much
const GRADIENT_BOTTOM = 0.4;      // and at its foot
const LEAST_BRIGHT = 0x90;        // the panel's colour is raised to this brightest channel at least (dark covers)

// "#RRGGBB" as its three channels, nothing for any other text.
function channels(color) {
  const match = /^#([0-9A-Fa-f]{6})$/.exec(color || "");
  return match ? [0, 2, 4].map((i) => parseInt(match[1].slice(i, i + 2), 16)) : null;
}

const NEARLY_BLACK = 0x30;        // a dominant colour whose brightest channel is below this reads as black
const WHITE_CONTRAST = 4.5;       // white text keeps at least this contrast on the panel's brightest colour

const hex = (rgb) => "#" + rgb.map((value) => Math.min(255, Math.round(value)).toString(16).padStart(2, "0")).join("");

// The cover's dominant colour, as before; only a cover dark enough to draw an almost black panel takes its lyrics'
// colour instead, raised (lifted) so the gradient shows.
function panelColor(dominant, lyrics) {
  const rgb = channels(dominant);
  if (rgb && Math.max(...rgb) >= NEARLY_BLACK) return dominant;
  return lifted(lyrics || dominant);
}

// `color` darkened by `factor`, then darker still if white text would read on it below WHITE_CONTRAST (WCAG).
function readableShade(color, factor) {
  const rgb = (channels(color) || [0x2A, 0x2A, 0x2E]).map((value) => value * factor);
  const linear = (value) => {
    const c = value / 255;
    return c <= 0.03928 ? c / 12.92 : ((c + 0.055) / 1.055) ** 2.4;
  };
  const luminance = (parts) => 0.2126 * linear(parts[0]) + 0.7152 * linear(parts[1]) + 0.0722 * linear(parts[2]);
  const most = 1.05 / WHITE_CONTRAST - 0.05;
  let shade = rgb;
  for (let step = 0; step < 20 && luminance(shade) > most; step++) shade = shade.map((value) => value * 0.95);
  return hex(shade);
}

// The colour with its hue kept and raised until its brightest channel reaches LEAST_BRIGHT, so the gradient shows.
function lifted(color) {
  const found = channels(color);
  if (!found) return color;
  const rgb = Math.max(...found) === 0 ? [0x70, 0x70, 0x78] : found;  // a black cover: a neutral slate
  const top = Math.max(...rgb);
  if (top >= LEAST_BRIGHT) return color;
  const scale = LEAST_BRIGHT / top;
  return hex(rgb.map((value) => value * scale));
}

const SEE_THROUGH = "B3";        // 70 %: under a theme, its glass and veil show through the song's colour

// The song's colour, darkened so white text stays readable on it (contrast above 4.5:1 on the darkest cards). Under a
// theme the column is the theme's glass, with its own veil: the colour lets it show through, like the pages around it.
function tint(color, factor) {
  const match = /^#([0-9A-Fa-f]{6})$/.exec(color || "");
  const rgb = match ? match[1] : "2A2A2E";
  const part = (i) => Math.round(parseInt(rgb.slice(i, i + 2), 16) * factor).toString(16).padStart(2, "0");
  const alpha = Ampify.activeTheme() ? SEE_THROUGH : "FF";
  return "#" + alpha + part(0) + part(2) + part(4);
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

// The sections the settings keep (a change in the settings restarts the extension, so it reads them again).
const shows = (key) => Ampify.config.get(key) !== false;
const THEME_CARD = "#1FFFFFFF";  // with the theme's background, a light veil sets the cards apart

function SongPanel() {
  const player = Ampify.hooks.usePlayer();
  const track = player && player.track;
  const { song, artistSongs } = useLibrary(track);
  const trackKey = track ? track.id + track.queueItemId : "";
  const queue = useQueue(trackKey);
  const colors = useCoverColors(trackKey);
  if (!track) return h("text", { text: t("nothing"), size: 14, color: SOFT });
  // The song's colour over the whole card (Ampify gives a panel's root background to its card), from bright at the
  // top to deep at the bottom.
  const songColor = panelColor(colors ? colors.DARK_VIBRANT : track.artworkColor,
                               colors ? colors.VIBRANT_NON_ALARMING : null);
  const base = "linear-gradient(" + tint(readableShade(songColor, GRADIENT_TOP), 1) + ", " +
               tint(readableShade(songColor, GRADIENT_BOTTOM), 1) + ")";
  // "Background: the theme's" leaves the column to the theme (its glass or its page colour), the cards on a veil.
  const themeBackground = Ampify.config.get("colour") === "Theme";
  const lyricsTint = themeBackground ? THEME_CARD : tint(colors ? colors.VIBRANT_NON_ALARMING : track.artworkColor, 0.5);
  return h("stack", { background: themeBackground ? undefined : base, cornerRadius: 12, padding: 12, gap: 12 },
    h("image", { src: "artwork", fit: "cover", cornerRadius: 8, width: SIDE - 24, height: SIDE - 24,
                 label: track.album || track.title }),
    h(Header, { track, song }),
    shows("lyrics") ? h("stack", { background: lyricsTint, cornerRadius: 12, padding: 16, gap: 8, height: LYRICS_HEIGHT },
      h("text", { text: t("lyrics"), size: 15, weight: 700, color: TEXT }),
      // A line clicked: the song goes there.
      h("lyrics", { grow: true, size: 22, color: TEXT, placeholder: t("noLyrics"),
                    onChange: (event) => Ampify.player.seek(Number(event.value)) })) : null,
    shows("artist") ? h(ArtistCard, { track, artistSongs }) : null,
    shows("credits") ? h(CreditsCard, { track, song }) : null,
    shows("queue") ? h(QueueCard, { items: queue }) : null);
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
