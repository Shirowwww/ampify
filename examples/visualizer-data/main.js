// Données d'un visualiseur : le noyau analyse le son décodé (en mémoire, rien n'est enregistré) et envoie environ
// 20 images par seconde. Chaque image a 16 bandes de 0 à 1 (des graves aux aigus), le niveau RMS et la crête.
// Cet exemple écrit une ligne dans le journal toutes les 5 secondes au plus, pour ne pas le remplir.

const bars = " ▁▂▃▄▅▆▇█";
let lastLine = -Infinity;

function bargraph(bands) {
  return bands.map((value) => bars[Math.min(bars.length - 1, Math.round(value * (bars.length - 1)))]).join("");
}

const listening = AMK.audio.onFrame((frame) => {
  if (frame.time - lastLine < 5) return;
  lastLine = frame.time;

  const result = AMK.audio.analyze();
  const tempo = result.tempo === null ? "inconnu" : Math.round(result.tempo) + " BPM";
  const lufs = result.loudness.momentary === null ? "inconnue" : result.loudness.momentary.toFixed(1) + " LUFS";
  const volume = AMK.audio.volume();
  AMK.log(
    "spectre " + bargraph(frame.bands) +
      " | crête " + frame.peak.toFixed(2) +
      " | tempo " + tempo +
      " | loudness " + lufs +
      " | volume " + (volume === null ? "inconnu" : Math.round(volume * 100) + " %"),
  );
});

AMK.log(listening ? "visualiseur à l'écoute" : "pas de son à analyser dans ce processus");
