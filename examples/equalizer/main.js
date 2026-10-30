// Égaliseur à 10 bandes (31 Hz à 16 kHz). Il agit sur les échantillons décodés, après le gain du noyau et avant
// son limiteur. Le curseur de volume d'Apple Music ne bouge pas.
//
// Réglages (amk config extensions.equalizer.<clé> <valeur>) :
//   preset  nom d'un préréglage, « flat » par défaut (aucun effet) ; la liste est écrite dans le journal
//   bands   gains en dB séparés par des virgules, ex. « 3,2,0,0,0,0,0,1,2,3 » ; remplace le préréglage
// À l'arrêt de l'extension, l'égaliseur revient à plat.

const preset = AMK.config.get("preset", "flat");
const bands = AMK.config.get("bands", "");

AMK.log("préréglages : " + AMK.audio.eq.presets().join(", "));

if (bands !== "") {
  const gains = String(bands).split(",").map(Number);
  gains.forEach((gain, index) => {
    if (index < 10 && Number.isFinite(gain)) AMK.audio.eq.setBand(index, gain);
  });
  AMK.log("bandes réglées à la main : " + gains.join(" / ") + " dB");
} else if (AMK.audio.eq.preset(preset)) {
  AMK.log("préréglage « " + preset + " » appliqué");
} else {
  AMK.log("égaliseur indisponible dans ce processus");
}

const summary = AMK.audio.eq
  .bands()
  .map((band) => (band.frequency >= 1000 ? band.frequency / 1000 + " kHz" : band.frequency + " Hz") + " " + band.gainDb + " dB")
  .join(", ");
AMK.log("égaliseur : " + summary);
