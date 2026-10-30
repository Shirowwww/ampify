// Gain fixe, en décibels, sur les échantillons décodés. Le réglage « gainDb » vaut 3 par défaut. Le noyau le borne
// à son plafond (audio.maxGainDb). Le curseur de volume d'Apple Music ne bouge pas.
const applied = AMK.audio.setGainDb(AMK.config.get("gainDb", 3));
AMK.log("gain appliqué : " + applied + " dB");
