// Hi-Res Lossless badge: Apple Music for Windows names every lossless stream "Lossless", even a 24-bit 192 kHz one.
// This extension reads the rate of the stream playing and, above 48 kHz (Apple's own line for Hi-Res Lossless, whether
// or not the bit depth is known), has Apple's badge, its flyout and its screen reader name say "Hi-Res Lossless" in
// Apple's own words for each language, with "HR" after Apple's Lossless mark.

Ampify.i18n.define({
  en: { label: "Hi-Res Lossless" },
  fr: { label: "Lossless haute qualité" },
  de: { label: "Hi-Res Lossless" },
  es: { label: "Alta resolución sin pérdida" },
  pt: { label: "Hi-Res Lossless" },
  ru: { label: "Hi-Res Lossless" },
  ja: { label: "ハイレゾロスレス" },
  zh: { label: "高解析度无损" },
  it: { label: "Hi-Res Lossless" },
  ko: { label: "고해상도 무손실" },
});

const HI_RES_ABOVE_HZ = 48000;

const ICON =
  '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 15 12"><path d="M3.77 2.2L3.77 9.8L3.67 10.19L3.39 10.47L3 10.58L2.61 10.47L2.33 10.19L2.23 9.8L2.23 2.2L2.33 1.81L2.61 1.53L3 1.43L3.39 1.53L3.67 1.81Z M8.17 2.2L8.17 9.8L8.07 10.19L7.79 10.47L7.4 10.58L7.01 10.47L6.73 10.19L6.62 9.8L6.62 2.2L6.73 1.81L7.01 1.53L7.4 1.43L7.79 1.53L8.07 1.81Z M3 5.22L7.4 5.22L7.79 5.33L8.07 5.61L8.17 6L8.07 6.39L7.79 6.67L7.4 6.78L3 6.78L2.61 6.67L2.33 6.39L2.23 6L2.33 5.61L2.61 5.33Z"/><path d="M10.78 2.2L10.78 9.8L10.67 10.19L10.39 10.47L10 10.58L9.61 10.47L9.33 10.19L9.22 9.8L9.22 2.2L9.33 1.81L9.61 1.53L10 1.43L10.39 1.53L10.67 1.81Z M10 1.43L12.09 1.43L12.7 1.5L13.29 1.7L13.82 2.04L14.26 2.48L14.6 3.01L14.8 3.6L14.88 4.22L14.8 4.85L14.6 5.44L14.26 5.97L13.82 6.41L13.29 6.75L12.7 6.95L12.09 7.02L10 7.03L9.61 6.92L9.33 6.64L9.22 6.25L9.33 5.86L9.61 5.58L10 5.47L12.06 5.48L12.35 5.44L12.62 5.35L12.85 5.2L13.05 5L13.2 4.77L13.29 4.5L13.32 4.22L13.29 3.95L13.2 3.68L13.05 3.45L12.85 3.25L12.62 3.1L12.35 3.01L12.06 2.97L10 2.98L9.61 2.87L9.33 2.59L9.22 2.2L9.33 1.81L9.61 1.53Z M12.87 5.86L14.97 9.41L15.07 9.79L14.98 10.18L14.69 10.47L14.31 10.57L13.92 10.48L13.63 10.19L11.53 6.64L11.43 6.26L11.52 5.87L11.81 5.58L12.19 5.48L12.58 5.57Z"/></svg>';

// Hi-Res Lossless: a lossless stream above 48 kHz, as Apple defines it, or one Apple's player itself calls so.
function isHiRes(track) {
  if (!track || !track.lossless || track.atmos) return false;
  return track.variantId === "hi-res-lossless" || track.sampleRate > HI_RES_ABOVE_HZ;
}

let shown = false;

function follow(state) {
  const hiRes = isHiRes(state && state.track);
  if (hiRes === shown) return;
  shown = hiRes;
  Ampify.ui.playbar.audioBadge(hiRes ? { label: Ampify.i18n.t("label"), icon: ICON } : null);
}

follow(Ampify.player.state());
// Apple says the stream's rate a few seconds after the song starts, with no event of its own: the next second of
// progress brings it.
Ampify.player.on("trackchange", follow);
Ampify.player.on("statechange", follow);
Ampify.player.on("progress", follow);
