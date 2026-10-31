// Sends the tracks you listen to to Last.fm ("scrobbling"), with Last.fm's rules: a track longer than
// 30 seconds counts once it has been played for half its length, or for 4 minutes.
//
// Set up once (API key at https://www.last.fm/api/account/create, then a session key):
//   ampify config extensions.scrobbler.apiKey <API key>
//   ampify config extensions.scrobbler.apiSecret <shared secret>
//   ampify config extensions.scrobbler.sessionKey <session key>
// A play that could not be sent (network down) waits in storage and is sent later.

const API = "https://ws.audioscrobbler.com/2.0/";
const MAX_PENDING = 200;
const apiKey = String(Ampify.config.get("apiKey", "")).trim();
const apiSecret = String(Ampify.config.get("apiSecret", "")).trim();
const sessionKey = String(Ampify.config.get("sessionKey", "")).trim();
const ready = apiKey !== "" && apiSecret !== "" && sessionKey !== "";

// Last.fm's api_sig: the parameters sorted by name, joined as "namevalue", followed by the secret, then MD5.
// "format" is not part of it.
function signature(params) {
  const names = Object.keys(params).filter((name) => name !== "format").sort();
  return Ampify.crypto.md5(names.map((name) => name + params[name]).join("") + apiSecret);
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
  return Ampify.http.post(API, body);
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
  if (!ready || sending) return;
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

if (!ready) Ampify.log("Last.fm is not set up: set apiKey, apiSecret and sessionKey (see main.js)");
Ampify.player.on("trackchange", onTrack);
Ampify.player.on("statechange", onState);
onTrack(Ampify.player.state());
setInterval(() => check(Date.now()), 15000);
setInterval(flush, 5 * 60 * 1000);
flush();
