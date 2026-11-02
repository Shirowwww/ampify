// Playback guard. With Lossless on, Apple Music 1.1792 often freezes the first song played after it opens, at about
// 15 s: it says it plays, but the position stops and nothing is heard (its loader gave up). Pause, play and a jump do
// nothing then. This restarts the song ("previous" past 3 s goes back to its start) and, once it plays again, jumps back
// to where it froze through Apple's player. If the restart does not take, it reloads the song: next, then previous
// once the next song plays (Apple ignores "previous" while it still loads one, and disables it on autoplay songs).

const CHECK_EVERY_MS = 1000;
const FROZEN_AFTER_MS = 3000;      // a playing song whose position has not moved for this long
const RESTART_WAIT_MS = 6000;      // time the restart gets before the song is reloaded instead
const BACK_TRIES = 2;              // "previous" again when Apple ignored the first one
const BACK_AGAIN_AFTER_MS = 1000;  // time Apple gets to come back before "previous" is sent again
const REPAIRS_PER_SONG = 2;        // a song that freezes again after that is left alone, rather than loop
const GIVE_UP_AFTER_MS = 20000;    // a reload that never plays again ends the repair

let watched = "";    // the song being watched
let lastMs = -1;
let stillMs = 0;
const repairs = new Map();
let repair = null;   // { song, title, frozenMs, sinceMs, reloading, backTries, lastBackMs }

// The song, not its queue entry: Apple may give the reloaded song a new entry.
function songOf(state) {
  return state.track.id;
}

function startRepair(state, song) {
  const count = (repairs.get(song) ?? 0) + 1;
  repairs.set(song, count);
  repair = { song, title: state.track.title, frozenMs: state.positionMs, sinceMs: 0, reloading: false, backTries: 0,
             lastBackMs: 0 };
  Ampify.log(`"${state.track.title}" frozen at ${Math.round(state.positionMs / 1000)} s while playing: restarting it (${count}/${REPAIRS_PER_SONG})`);
  Ampify.player.previous();
}

function reload() {
  repair.reloading = true;
  repair.sinceMs = 0;
  Ampify.log(`"${repair.title}" did not restart: reloading it`);
  Ampify.player.next();
}

function followRepair(state, song) {
  repair.sinceMs += CHECK_EVERY_MS;
  // Past 3 s, "previous" restarts the song instead of going back: no try after that.
  const otherPlays = song !== repair.song && state.playing && state.positionMs > 0 && state.positionMs < 2800;
  // The song plays again from its start: back to where it froze.
  if (song === repair.song && state.playing && state.positionMs > 0 && state.positionMs < repair.frozenMs) {
    const done = Ampify.player.seek(repair.frozenMs);
    Ampify.log(`"${repair.title}" plays again: back to ${Math.round(repair.frozenMs / 1000)} s (${done ? "sent" : "refused"})`);
    repair = null;
  } else if (!repair.reloading) {
    if (repair.sinceMs >= RESTART_WAIT_MS) reload();
  } else if (repair.sinceMs >= GIVE_UP_AFTER_MS) {
    const where = song !== repair.song && state.playing ? "Apple did not go back, the next song plays" : "left as it is";
    Ampify.log(`"${repair.title}" did not play again after the reload: ${where}`);
    repair = null;
  } else if (otherPlays && repair.backTries < BACK_TRIES &&
             (repair.backTries === 0 || repair.sinceMs - repair.lastBackMs >= BACK_AGAIN_AFTER_MS)) {
    repair.backTries += 1;
    repair.lastBackMs = repair.sinceMs;
    Ampify.player.previous();
  }
}

function check() {
  const state = Ampify.player.state();
  if (!state.track) return;
  const song = songOf(state);
  if (song !== watched) {
    watched = song;
    lastMs = -1;
    stillMs = 0;
  }
  // Position 0 is a song still loading, which may take a while on a slow network.
  stillMs = state.playing && state.positionMs > 0 && state.positionMs === lastMs ? stillMs + CHECK_EVERY_MS : 0;
  lastMs = state.positionMs;

  if (repair) {
    followRepair(state, song);
  } else if (stillMs >= FROZEN_AFTER_MS && (repairs.get(song) ?? 0) < REPAIRS_PER_SONG) {
    stillMs = 0;
    startRepair(state, song);
  }
}

setInterval(check, CHECK_EVERY_MS);
