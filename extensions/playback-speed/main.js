// Playback speed: a button of the playback bar that opens a slider from half to double speed, with presets. The speed
// holds across pauses and songs until it is set back to normal, and comes back at the next start of Apple Music.

Ampify.i18n.define({
  en: { label: "Playback speed", speed: "Speed", normal: "Normal speed", old: "This needs Ampify 1.6 or later" },
  fr: { label: "Vitesse de lecture", speed: "Vitesse", normal: "Vitesse normale", old: "Il faut Ampify 1.6 ou plus récent" },
  de: { label: "Wiedergabegeschwindigkeit", speed: "Geschwindigkeit", normal: "Normale Geschwindigkeit", old: "Dafür ist Ampify 1.6 oder neuer nötig" },
  es: { label: "Velocidad de reproducción", speed: "Velocidad", normal: "Velocidad normal", old: "Hace falta Ampify 1.6 o posterior" },
  pt: { label: "Velocidade de reprodução", speed: "Velocidade", normal: "Velocidade normal", old: "É preciso o Ampify 1.6 ou mais recente" },
  ru: { label: "Скорость воспроизведения", speed: "Скорость", normal: "Обычная скорость", old: "Нужен Ampify 1.6 или новее" },
  ja: { label: "再生速度", speed: "速度", normal: "通常の速度", old: "Ampify 1.6 以降が必要です" },
  zh: { label: "播放速度", speed: "速度", normal: "正常速度", old: "需要 Ampify 1.6 或更高版本" },
  it: { label: "Velocità di riproduzione", speed: "Velocità", normal: "Velocità normale", old: "Serve Ampify 1.6 o successivo" },
  ko: { label: "재생 속도", speed: "속도", normal: "보통 속도", old: "Ampify 1.6 이상이 필요합니다" },
});
const t = (key, values) => Ampify.i18n.t(key, values);
const { h } = Ampify.React;
const { Slider, Button, Text } = Ampify.components;

const POPUP = "speed";
const PRESETS = [0.75, 0.9, 1, 1.1, 1.25, 1.5];
const ICON = "";  // Speed high, in Segoe Fluent Icons
const canSetRate = typeof Ampify.player.setRate === "function";

// "1.25×", or "1,25×" in the languages that write a decimal comma.
const COMMA = ["fr", "de", "es", "pt", "ru", "it"];
const shown = (rate) => {
  const text = String(Math.round(rate * 100) / 100);
  return (COMMA.includes(Ampify.i18n.language) ? text.replace(".", ",") : text) + "×";
};

let rate = Number(Ampify.storage.get("rate")) || 1;

function choose(value) {
  rate = Math.round(Math.min(2, Math.max(0.5, Number(value))) * 100) / 100;
  Ampify.storage.set("rate", rate);
  if (canSetRate) Ampify.player.setRate(rate);
  Ampify.ui.setActive(button, rate !== 1);
  Ampify.ui.setLabel(button, rate === 1 ? t("label") : t("label") + " · " + shown(rate));
  render();
}

function SpeedPopup() {
  if (!canSetRate) return h(Text, { text: t("old"), wrap: true });
  return h("stack", { gap: 12, padding: 4 },
    h(Slider, { label: t("speed"), min: 0.5, max: 2, step: 0.05, value: rate, unit: "×", onChange: choose }),
    h("row", { gap: 6 }, PRESETS.map((value) => h(Button, {
      key: String(value), label: shown(value), variant: value === rate ? "primary" : "secondary",
      onClick: () => choose(value) }))),
    h(Button, { label: t("normal"), variant: "tertiary", disabled: rate === 1, onClick: () => choose(1) }));
}

const root = Ampify.ReactDOM.createRoot("popup:" + POPUP);
const render = () => root.render(h(SpeedPopup));
const button = Ampify.ui.playbar.addButton({ label: t("label"), icon: ICON, active: false, onClick: render });

if (rate !== 1) choose(rate);  // the speed chosen last time

Ampify.onStop(() => {
  root.unmount();
  if (canSetRate && rate !== 1) Ampify.player.setRate(1);  // the element off: Apple's player at normal speed
});
