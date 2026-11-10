// Sends the tracks you listen to to Last.fm ("scrobbling"), with Last.fm's rules: a track longer than
// 30 seconds counts once it has been played for half its length, or for 4 minutes.
//
// Connecting: with "Use my Last.fm account" on (the default), the extension opens Last.fm's page in the browser
// and, once you allow Ampify there, keeps the session in its storage. Turning the setting off signs out; turning it
// on again opens the page again. A play that could not be sent (network down) waits in storage and is sent later.
// Another Last.fm application can be used: ampify config extensions.scrobbler.apiKey <key> (and apiSecret).

const API = "https://ws.audioscrobbler.com/2.0/";
const AUTH_PAGE = "https://www.last.fm/api/auth/";
// Ampify's Last.fm application. Its secret only signs requests: Last.fm's desktop sign-in works this way.
const AMPIFY_KEY = "3dcec5a7a5f89cc4b567ca855ce247fc";
const AMPIFY_SECRET = "af71824024b619932e15d96ca43190e5";
const MAX_PENDING = 200;
const ASK_EVERY_MS = 24 * 3600 * 1000;  // the page opens by itself once a day at most
const POLL_MS = 5000;
const POLL_FOR_MS = 10 * 60 * 1000;

Ampify.i18n.define({
  en: {
    allow: "Allow Ampify on the Last.fm page that just opened.",
    connected: "Connected to Last.fm as {name}.",
    late: "Last.fm was not allowed in time. Turn \"Use my Last.fm account\" off and on to try again.",
    blocked: "Last.fm's page could not open. Turn \"Use my Last.fm account\" off and on in a few seconds.",
    waiting: "Last.fm is not connected yet. Turn \"Use my Last.fm account\" off and on to open its page again.",
    noKey: "Last.fm: this copy has no application key. Set one: ampify config extensions.scrobbler.apiKey <key>",
    signedOut: "Signed out of Last.fm.",
  },
  fr: {
    allow: "Autorisez Ampify sur la page Last.fm qui vient de s'ouvrir.",
    connected: "Connecté à Last.fm en tant que {name}.",
    late: "Last.fm n'a pas été autorisé à temps. Désactivez puis réactivez « Utiliser mon compte Last.fm ».",
    blocked: "La page Last.fm n'a pas pu s'ouvrir. Désactivez puis réactivez « Utiliser mon compte Last.fm » dans quelques secondes.",
    waiting: "Last.fm n'est pas encore connecté. Désactivez puis réactivez « Utiliser mon compte Last.fm » pour rouvrir sa page.",
    noKey: "Last.fm : cette copie n'a pas de clé d'application. Indiquez-en une : ampify config extensions.scrobbler.apiKey <clé>",
    signedOut: "Déconnecté de Last.fm.",
  },
  de: {
    allow: "Erlauben Sie Ampify auf der Last.fm-Seite, die sich gerade geöffnet hat.",
    connected: "Mit Last.fm verbunden als {name}.",
    late: "Last.fm wurde nicht rechtzeitig erlaubt. Schalten Sie „Mein Last.fm-Konto verwenden“ aus und wieder ein.",
    blocked: "Die Last.fm-Seite konnte nicht geöffnet werden. Schalten Sie „Mein Last.fm-Konto verwenden“ in ein paar Sekunden aus und wieder ein.",
    waiting: "Last.fm ist noch nicht verbunden. Schalten Sie „Mein Last.fm-Konto verwenden“ aus und wieder ein, um die Seite erneut zu öffnen.",
    noKey: "Last.fm: Diese Kopie hat keinen Anwendungsschlüssel. Geben Sie einen an: ampify config extensions.scrobbler.apiKey <Schlüssel>",
    signedOut: "Von Last.fm abgemeldet.",
  },
  es: {
    allow: "Permite Ampify en la página de Last.fm que se acaba de abrir.",
    connected: "Conectado a Last.fm como {name}.",
    late: "No se permitió Last.fm a tiempo. Desactiva y vuelve a activar «Usar mi cuenta de Last.fm».",
    blocked: "No se pudo abrir la página de Last.fm. Desactiva y vuelve a activar «Usar mi cuenta de Last.fm» dentro de unos segundos.",
    waiting: "Last.fm todavía no está conectado. Desactiva y vuelve a activar «Usar mi cuenta de Last.fm» para abrir su página otra vez.",
    noKey: "Last.fm: esta copia no tiene clave de aplicación. Indica una: ampify config extensions.scrobbler.apiKey <clave>",
    signedOut: "Sesión cerrada en Last.fm.",
  },
  pt: {
    allow: "Autorize o Ampify na página do Last.fm que acabou de abrir.",
    connected: "Conectado ao Last.fm como {name}.",
    late: "O Last.fm não foi autorizado a tempo. Desative e ative novamente “Usar minha conta do Last.fm”.",
    blocked: "Não foi possível abrir a página do Last.fm. Desative e ative novamente “Usar minha conta do Last.fm” em alguns segundos.",
    waiting: "O Last.fm ainda não está conectado. Desative e ative novamente “Usar minha conta do Last.fm” para abrir a página outra vez.",
    noKey: "Last.fm: esta cópia não tem chave de aplicativo. Informe uma: ampify config extensions.scrobbler.apiKey <chave>",
    signedOut: "Desconectado do Last.fm.",
  },
  ru: {
    allow: "Разрешите доступ Ampify на только что открывшейся странице Last.fm.",
    connected: "Подключено к Last.fm как {name}.",
    late: "Доступ к Last.fm не был разрешён вовремя. Выключите и снова включите «Использовать мой аккаунт Last.fm».",
    blocked: "Не удалось открыть страницу Last.fm. Выключите и снова включите «Использовать мой аккаунт Last.fm» через несколько секунд.",
    waiting: "Last.fm ещё не подключён. Выключите и снова включите «Использовать мой аккаунт Last.fm», чтобы открыть страницу заново.",
    noKey: "Last.fm: у этой копии нет ключа приложения. Укажите его: ampify config extensions.scrobbler.apiKey <ключ>",
    signedOut: "Выход из Last.fm выполнен.",
  },
  ja: {
    allow: "いま開いた Last.fm のページで Ampify を許可してください。",
    connected: "Last.fm に {name} として接続しました。",
    late: "時間内に Last.fm が許可されませんでした。「自分の Last.fm アカウントを使う」をオフにして、もう一度オンにしてください。",
    blocked: "Last.fm のページを開けませんでした。数秒待ってから「自分の Last.fm アカウントを使う」をオフにして、もう一度オンにしてください。",
    waiting: "Last.fm にはまだ接続されていません。「自分の Last.fm アカウントを使う」をオフにして、もう一度オンにすると、ページがもう一度開きます。",
    noKey: "Last.fm： このコピーにはアプリケーションキーがありません。次のコマンドで設定してください： ampify config extensions.scrobbler.apiKey <キー>",
    signedOut: "Last.fm からサインアウトしました。",
  },
  zh: {
    allow: "请在刚刚打开的 Last.fm 页面中允许 Ampify。",
    connected: "已以 {name} 的身份连接到 Last.fm。",
    late: "没有及时允许 Last.fm。请先关闭再重新开启“使用我的 Last.fm 账号”。",
    blocked: "无法打开 Last.fm 页面。请几秒钟后先关闭再重新开启“使用我的 Last.fm 账号”。",
    waiting: "尚未连接到 Last.fm。先关闭再重新开启“使用我的 Last.fm 账号”，即可重新打开它的页面。",
    noKey: "Last.fm：此副本没有应用密钥。请设置一个：ampify config extensions.scrobbler.apiKey <密钥>",
    signedOut: "已退出 Last.fm。",
  },
  it: {
    allow: "Consenti Ampify nella pagina di Last.fm appena aperta.",
    connected: "Connesso a Last.fm come {name}.",
    late: "Last.fm non è stato consentito in tempo. Disattiva e riattiva «Usa il mio account Last.fm» per riprovare.",
    blocked: "Non è stato possibile aprire la pagina di Last.fm. Disattiva e riattiva «Usa il mio account Last.fm» tra qualche secondo.",
    waiting: "Last.fm non è ancora connesso. Disattiva e riattiva «Usa il mio account Last.fm» per riaprire la sua pagina.",
    noKey: "Last.fm: questa copia non ha una chiave dell'applicazione. Impostane una: ampify config extensions.scrobbler.apiKey <chiave>",
    signedOut: "Disconnesso da Last.fm.",
  },
  ko: {
    allow: "방금 열린 Last.fm 페이지에서 Ampify를 허용하세요.",
    connected: "{name} 계정으로 Last.fm에 연결되었습니다.",
    late: "제때 Last.fm을 허용하지 않았습니다. \"내 Last.fm 계정 사용\"을 껐다가 다시 켜서 다시 시도하세요.",
    blocked: "Last.fm 페이지를 열 수 없었습니다. 몇 초 후에 \"내 Last.fm 계정 사용\"을 껐다가 다시 켜세요.",
    waiting: "아직 Last.fm에 연결되지 않았습니다. \"내 Last.fm 계정 사용\"을 껐다가 다시 켜면 해당 페이지가 다시 열립니다.",
    noKey: "Last.fm: 이 사본에는 애플리케이션 키가 없습니다. 키를 설정하세요: ampify config extensions.scrobbler.apiKey <키>",
    signedOut: "Last.fm에서 로그아웃되었습니다.",
  },
});
const t = (key, values) => Ampify.i18n.t(key, values);

const apiKey = String(Ampify.config.get("apiKey", "") || AMPIFY_KEY).trim();
const apiSecret = String(Ampify.config.get("apiSecret", "") || AMPIFY_SECRET).trim();
const wantsAccount = Ampify.config.get("connect", true) !== false;
let sessionKey = wantsAccount
  ? String(Ampify.config.get("sessionKey", "") || Ampify.storage.get("session", {}).key || "").trim()
  : "";
const ready = () => apiKey !== "" && apiSecret !== "" && sessionKey !== "";

// Last.fm's api_sig: the parameters sorted by name, joined as "namevalue", followed by the secret, then MD5.
// "format" is not part of it.
function signature(params) {
  const names = Object.keys(params).filter((name) => name !== "format").sort();
  return Ampify.crypto.md5(names.map((name) => name + params[name]).join("") + apiSecret);
}

// Signs and sends a Last.fm call. The sign-in calls go without the session.
function call(method, values, withSession = true) {
  const params = { method, api_key: apiKey };
  if (withSession) params.sk = sessionKey;
  for (const name of Object.keys(values)) {
    if (values[name] !== "" && values[name] !== undefined) params[name] = String(values[name]);
  }
  params.api_sig = signature(params);
  params.format = "json";
  const body = Object.keys(params)
    .map((name) => encodeURIComponent(name) + "=" + encodeURIComponent(params[name]))
    .join("&");
  return Ampify.http.post(API, body);
}

function parsed(answer) {
  try {
    return JSON.parse(answer.text) || {};
  } catch {
    return {};
  }
}

// Last.fm's desktop sign-in: a token, the page where the user allows it, then the session that token gives.
function connect() {
  if (typeof Ampify.http.open !== "function") {
    Ampify.log("This version of Ampify cannot open Last.fm's page: update Ampify");
    return;
  }
  call("auth.getToken", {}, false)
    .then((answer) => {
      const token = parsed(answer).token;
      if (!token) {
        Ampify.log("Last.fm gave no sign-in token (code " + answer.status + ")");
        return;
      }
      // open() answers false when a page was opened less than 10 s ago.
      if (!Ampify.http.open(AUTH_PAGE + "?api_key=" + encodeURIComponent(apiKey) + "&token=" + encodeURIComponent(token))) {
        Ampify.notify({ title: "Last.fm", text: t("blocked") });
        return;
      }
      Ampify.storage.set("askedAt", Date.now());
      Ampify.notify({ title: "Last.fm", text: t("allow") });
      waitForSession(token);
    })
    .catch((error) => Ampify.log("Last.fm sign-in failed: " + error));
}

function waitForSession(token) {
  const until = Date.now() + POLL_FOR_MS;
  let asking = false;
  const timer = setInterval(() => {
    if (asking) return;
    if (Date.now() > until) {
      clearInterval(timer);
      Ampify.notify({ title: "Last.fm", text: t("late") });
      return;
    }
    asking = true;
    call("auth.getSession", { token }, false)
      .then((answer) => {
        const session = parsed(answer).session;
        if (!session || !session.key) return;  // not allowed yet
        clearInterval(timer);
        sessionKey = session.key;
        Ampify.storage.set("session", { name: session.name, key: session.key });
        Ampify.notify({ title: "Last.fm", text: t("connected", { name: session.name }) });
        flush();
      })
      .catch(() => {})  // asked again at the next tick
      .finally(() => { asking = false; });
  }, POLL_MS);
}

let current = null;  // the current track: title, length, time played
let sending = false;

function listenedMs(now) {
  return current.playedMs + (current.since === null ? 0 : now - current.since);
}

function check(now) {
  if (!current || current.done || current.durationMs <= 30000) return;
  if (listenedMs(now) < Math.min(current.durationMs / 2, 240000)) return;
  current.done = true;
  const pending = Ampify.storage.get("pending", []);
  pending.push({
    artist: current.artist,
    track: current.title,
    album: current.album,
    timestamp: Math.floor(current.startedAt / 1000),
    duration: Math.round(current.durationMs / 1000),
  });
  while (pending.length > MAX_PENDING) pending.shift();
  Ampify.storage.set("pending", pending);
  Ampify.storage.set("total", Ampify.storage.get("total", 0) + 1);
  flush();
}

// Sends the waiting plays one by one, in order.
function flush() {
  if (!ready() || sending) return;
  const pending = Ampify.storage.get("pending", []);
  if (pending.length === 0) return;
  sending = true;
  call("track.scrobble", pending[0]).then(
    (answer) => {
      sending = false;
      if (answer.status !== 200) {
        Ampify.log("Last.fm did not accept the play (code " + answer.status + "), trying again in 5 minutes");
        return;
      }
      const rest = Ampify.storage.get("pending", []);
      rest.shift();
      Ampify.storage.set("pending", rest);
      flush();
    },
    // A request Ampify refused (too many at once, a refused address) must not stop the later sends.
    (error) => {
      sending = false;
      Ampify.log("the play was not sent (" + error.message + "), trying again in 5 minutes");
    },
  );
}

function onTrack(state) {
  const now = Date.now();
  if (current) check(now);
  const track = state && state.track;
  if (!track) {
    current = null;
    return;
  }
  if (current && current.id === track.id) return;
  current = {
    id: track.id,
    title: track.title,
    artist: track.artist,
    album: track.album,
    durationMs: state.durationMs,
    startedAt: now,
    playedMs: 0,
    since: state.playing ? now : null,
    done: false,
  };
  if (ready() && state.playing) call("track.updateNowPlaying", { artist: track.artist, track: track.title, album: track.album });
}

function onState(state) {
  if (!current || !state.track || state.track.id !== current.id) {
    onTrack(state);
    return;
  }
  const now = Date.now();
  if (state.playing && current.since === null) current.since = now;
  if (!state.playing && current.since !== null) {
    current.playedMs += now - current.since;
    current.since = null;
  }
  check(now);
}

if (apiKey === "" || apiSecret === "") {
  Ampify.log(t("noKey"));
} else if (!wantsAccount) {
  Ampify.storage.remove("session");
  Ampify.storage.remove("askedAt");
  Ampify.log(t("signedOut"));
} else if (sessionKey === "") {
  if (Date.now() - Ampify.storage.get("askedAt", 0) >= ASK_EVERY_MS) connect();
  else Ampify.log(t("waiting"));
}
Ampify.player.on("trackchange", onTrack);
Ampify.player.on("statechange", onState);
onTrack(Ampify.player.state());
setInterval(() => check(Date.now()), 15000);
setInterval(flush, 5 * 60 * 1000);
flush();
