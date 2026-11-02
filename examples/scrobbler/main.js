// Sends the tracks you listen to to Last.fm ("scrobbling"), with Last.fm's rules: a track longer than
// 30 seconds counts once it has been played for half its length, or for 4 minutes.
//
// Connecting: with "Connect my Last.fm account" on (the default), the extension opens Last.fm's page in the browser
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
    late: "Last.fm was not allowed in time. Turn \"Connect my Last.fm account\" off and on to try again.",
    blocked: "Last.fm's page could not open. Turn \"Connect my Last.fm account\" off and on in a few seconds.",
    waiting: "Last.fm is not connected yet. Turn \"Connect my Last.fm account\" off and on to open its page again.",
    noKey: "Last.fm: this copy has no application key. Set one: ampify config extensions.scrobbler.apiKey <key>",
    signedOut: "Signed out of Last.fm.",
  },
  fr: {
    allow: "Autorisez Ampify sur la page Last.fm qui vient de s'ouvrir.",
    connected: "Connecté à Last.fm en tant que {name}.",
    late: "Last.fm n'a pas été autorisé à temps. Désactivez puis réactivez « Connecter mon compte Last.fm ».",
    blocked: "La page Last.fm n'a pas pu s'ouvrir. Désactivez puis réactivez « Connecter mon compte Last.fm » dans quelques secondes.",
    waiting: "Last.fm n'est pas encore connecté. Désactivez puis réactivez « Connecter mon compte Last.fm » pour rouvrir sa page.",
    noKey: "Last.fm : cette copie n'a pas de clé d'application. Indiquez-en une : ampify config extensions.scrobbler.apiKey <clé>",
    signedOut: "Déconnecté de Last.fm.",
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
  } catch (error) {
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
  call("track.scrobble", pending[0]).then((answer) => {
    sending = false;
    if (answer.status !== 200) {
      Ampify.log("Last.fm did not accept the play (code " + answer.status + "), trying again in 5 minutes");
      return;
    }
    const rest = Ampify.storage.get("pending", []);
    rest.shift();
    Ampify.storage.set("pending", rest);
    flush();
  });
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
