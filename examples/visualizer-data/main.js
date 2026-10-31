// Visualizer data: the kernel analyzes the decoded sound (in memory, nothing is recorded) and sends about
// 20 frames per second. Each frame has 16 bands from 0 to 1 (low to high), the RMS level and the peak.
// This example writes one line to the log at most every 5 seconds, so it does not fill up.

const bars = " ▁▂▃▄▅▆▇█";
let lastLine = -Infinity;

function bargraph(bands) {
  return bands.map((value) => bars[Math.min(bars.length - 1, Math.round(value * (bars.length - 1)))]).join("");
}

const listening = Ampify.audio.onFrame((frame) => {
  if (frame.time - lastLine < 5) return;
  lastLine = frame.time;

  const result = Ampify.audio.analyze();
  const tempo = result.tempo === null ? "unknown" : Math.round(result.tempo) + " BPM";
  const lufs = result.loudness.momentary === null ? "unknown" : result.loudness.momentary.toFixed(1) + " LUFS";
  const volume = Ampify.audio.volume();
  Ampify.log(
    "spectrum " + bargraph(frame.bands) +
      " | peak " + frame.peak.toFixed(2) +
      " | tempo " + tempo +
      " | loudness " + lufs +
      " | volume " + (volume === null ? "unknown" : Math.round(volume * 100) + " %"),
  );
});

Ampify.log(listening ? "visualizer is listening" : "no sound to analyze in this process");
