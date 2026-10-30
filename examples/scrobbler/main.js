// Envoie à Last.fm les morceaux écoutés (« scrobbling »), avec les règles de Last.fm : un morceau de plus de
// 30 secondes compte quand il a été écouté pendant la moitié de sa durée, ou pendant 4 minutes.
//
// À régler une fois (clé API sur https://www.last.fm/api/account/create, puis une clé de session) :
//   amk config extensions.scrobbler.apiKey <clé API>
//   amk config extensions.scrobbler.apiSecret <secret partagé>
//   amk config extensions.scrobbler.sessionKey <clé de session>
// Une écoute qui n'a pas pu partir (réseau coupé) attend dans le stockage et repart plus tard.

const API = "https://ws.audioscrobbler.com/2.0/";
const MAX_PENDING = 200;
const apiKey = String(AMK.config.get("apiKey", "")).trim();
const apiSecret = String(AMK.config.get("apiSecret", "")).trim();
const sessionKey = String(AMK.config.get("sessionKey", "")).trim();
const ready = apiKey !== "" && apiSecret !== "" && sessionKey !== "";

// api_sig de Last.fm : les paramètres triés par nom, collés en « nomvaleur », suivis du secret, puis en MD5.
// « format » n'en fait pas partie.
function signature(params) {
  const names = Object.keys(params).filter((name) => name !== "format").sort();
  return AMK.crypto.md5(names.map((name) => name + params[name]).join("") + apiSecret);
}

function call(method, values) {
  const params = { method, api_key: apiKey, sk: sessionKey };
  for (const name of Object.keys(values)) {
    if (values[name] !== "" && values[name] !== undefined) params[name] = String(values[name]);
  }
  params.api_sig = signature(params);
  params.format = "json";
  const body = Object.keys(params)
    .map((name) => encodeURIComponent(name) + "=" + encodeURIComponent(params[name]))
    .join("&");
  return AMK.http.post(API, body);
}

let current = null;  // le morceau en cours : titre, durée, temps écouté
let sending = false;

function listenedMs(now) {
  return current.playedMs + (current.since === null ? 0 : now - current.since);
}

function check(now) {
  if (!current || current.done || current.durationMs <= 30000) return;
  if (listenedMs(now) < Math.min(current.durationMs / 2, 240000)) return;
  current.done = true;
  const pending = AMK.storage.get("pending", []);
  pending.push({
    artist: current.artist,
    track: current.title,
    album: current.album,
    timestamp: Math.floor(current.startedAt / 1000),
    duration: Math.round(current.durationMs / 1000),
  });
  while (pending.length > MAX_PENDING) pending.shift();
  AMK.storage.set("pending", pending);
  AMK.storage.set("total", AMK.storage.get("total", 0) + 1);
  flush();
}

// Envoie les écoutes en attente, une à une, dans l'ordre.
function flush() {
  if (!ready || sending) return;
  const pending = AMK.storage.get("pending", []);
  if (pending.length === 0) return;
  sending = true;
  call("track.scrobble", pending[0]).then((answer) => {
    sending = false;
    if (answer.status !== 200) {
      AMK.log("Last.fm n'a pas pris l'écoute (code " + answer.status + "), nouvel essai dans 5 minutes");
      return;
    }
    const rest = AMK.storage.get("pending", []);
    rest.shift();
    AMK.storage.set("pending", rest);
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
  if (ready && state.playing) call("track.updateNowPlaying", { artist: track.artist, track: track.title, album: track.album });
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

if (!ready) AMK.log("Last.fm n'est pas réglé : indique apiKey, apiSecret et sessionKey (voir main.js)");
AMK.player.on("trackchange", onTrack);
AMK.player.on("statechange", onState);
onTrack(AMK.player.state());
setInterval(() => check(Date.now()), 15000);
setInterval(flush, 5 * 60 * 1000);
flush();
