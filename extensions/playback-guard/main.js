// Playback guard. With Lossless on, Apple Music 1.1792 often freezes the first song played after it opens, at about
// 15 s: it says it plays, but the position stops and nothing is heard (its loader gave up). Pause, play and a jump do
// nothing then. This reloads the song (next, then previous: the next song has no time to be heard) and, once it plays
// again from its start, jumps back to where it froze through Apple's player.

const CHECK_EVERY_MS = 1000;
const FROZEN_AFTER_MS = 3000;      // a playing song whose position has not moved for this long
const BACK_AFTER_MS = 400;         // well under the 3 s after which "previous" restarts the song instead of going back
const REPAIRS_PER_SONG = 2;        // a song that freezes again after that is left alone, rather than loop
const GIVE_UP_AFTER_MS = 20000;    // a reload that never plays again ends the repair

let watched = "";    // the song being watched
let lastMs = -1;
let stillMs = 0;
const repairs = new Map();
let repair = null;   // { song, title, frozenMs, sinceMs }

// The song, not its queue entry: Apple may give the reloaded song a new entry.
function songOf(state) {
  return state.track.id;
}

function startRepair(state, song) {
  const count = (repairs.get(song) ?? 0) + 1;
  repairs.set(song, count);
  repair = { song, title: state.track.title, frozenMs: state.positionMs, sinceMs: 0 };
  Ampify.log(`"${state.track.title}" frozen at ${Math.round(state.positionMs / 1000)} s while playing: reloading it (${count}/${REPAIRS_PER_SONG})`);
  Ampify.player.next();
  setTimeout(() => Ampify.player.previous(), BACK_AFTER_MS);
}

function followRepair(state, song) {
  repair.sinceMs += CHECK_EVERY_MS;
  // Back on the song, playing again from its start: back to where it froze.
  if (song === repair.song && state.playing && state.positionMs > 0 && state.positionMs < repair.frozenMs) {
    const done = Ampify.player.seek(repair.frozenMs);
    Ampify.log(`"${repair.title}" plays again: back to ${Math.round(repair.frozenMs / 1000)} s (${done ? "sent" : "refused"})`);
    repair = null;
  } else if (repair.sinceMs >= GIVE_UP_AFTER_MS) {
    Ampify.log(`"${repair.title}" did not play again after the reload: left as it is`);
    repair = null;
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
