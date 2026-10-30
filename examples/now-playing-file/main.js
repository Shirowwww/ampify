// Écrit le morceau en cours dans %USERPROFILE%\AppleMusicModKit\output\now-playing-file\ :
//   now-playing.txt   une ligne, selon le réglage « format » ({title}, {artist}, {album}), pour une source texte OBS
//   now-playing.json  titre, artiste, album, lecture en cours, pour un widget
// Réglages facultatifs : format (« {artist} - {title} » par défaut), clearOnPause (1 pour vider le texte en pause).
// Dans OBS : Source « Texte (GDI+) », case « Lire à partir d'un fichier », puis choisis now-playing.txt.

const format = String(AMK.config.get("format", "{artist} - {title}"));
const clearOnPause = AMK.config.get("clearOnPause", false);
let lastText = null;

function line(track) {
  return format.replace(/\{(title|artist|album)\}/g, (whole, name) => track[name] || "");
}

function write(state) {
  const track = state && state.track;
  const text = !track || (!state.playing && clearOnPause) ? "" : line(track);
  if (text === lastText) return;  // OBS relit le fichier : on ne l'écrit que s'il change
  lastText = text;
  AMK.fs.writeText("now-playing.txt", text);
  AMK.fs.writeText("now-playing.json", JSON.stringify({
    playing: !!(state && state.playing),
    title: track ? track.title : "",
    artist: track ? track.artist : "",
    album: track ? track.album : "",
  }));
}

AMK.player.on("trackchange", write);
AMK.player.on("statechange", write);
write(AMK.player.state());
AMK.onStop(() => AMK.fs.writeText("now-playing.txt", ""));
