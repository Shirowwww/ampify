// Met la lecture en pause après le délai du réglage « minutes » (30 par défaut), puis prévient.
const minutes = AMK.config.get("minutes", 30);
AMK.log("pause dans " + minutes + " min");

setTimeout(() => {
  AMK.player.pause();
  AMK.notify({ title: "Minuterie", text: "La lecture est en pause." });
}, Math.max(1, minutes) * 60 * 1000);
