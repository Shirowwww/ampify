// Writes the current track to %USERPROFILE%\Ampify\output\now-playing-file\ :
//   now-playing.txt   one line, following the "format" setting ({title}, {artist}, {album}), for an OBS text source
//   now-playing.json  title, artist, album, playing state, for a widget
// Optional settings: format ("{artist} - {title}" by default), clearOnPause (1 to clear the text when paused).
// In OBS: add a "Text (GDI+)" source, tick "Read from file", then pick now-playing.txt.

const format = String(Ampify.config.get("format", "{artist} - {title}"));
const clearOnPause = Ampify.config.get("clearOnPause", false);
let lastText = null;

function line(track) {
  return format.replace(/\{(title|artist|album)\}/g, (whole, name) => track[name] || "");
}

function write(state) {
  const track = state && state.track;
  const text = !track || (!state.playing && clearOnPause) ? "" : line(track);
  if (text === lastText) return;  // OBS rereads the file: only write it when it changes
  lastText = text;
  Ampify.fs.writeText("now-playing.txt", text);
  Ampify.fs.writeText("now-playing.json", JSON.stringify({
    playing: !!(state && state.playing),
    title: track ? track.title : "",
    artist: track ? track.artist : "",
    album: track ? track.album : "",
  }));
}

Ampify.player.on("trackchange", write);
Ampify.player.on("statechange", write);
write(Ampify.player.state());
Ampify.onStop(() => Ampify.fs.writeText("now-playing.txt", ""));
