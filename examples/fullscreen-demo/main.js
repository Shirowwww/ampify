// A "now playing" screen in the style of Spicetify's Beautiful Lyrics, made of primitives only: an artwork that fills
// the window, an acrylic layer that blurs it, a big centred title, and lines that fade and grow in turn. A button of
// the top bar opens it; its Close button or Escape closes it. See docs/extensions/api.md, section 5.5.

const ARTWORK =
  "https://upload.wikimedia.org/wikipedia/commons/thumb/a/a8/Tour_Eiffel_Wikimedia_Commons.jpg/960px-Tour_Eiffel_Wikimedia_Commons.jpg";
const LINES = [
  "The first line comes in softly",
  "then the next one takes its place",
  "each line grows while it is sung",
  "and fades when its turn is over",
];
const STEP_MS = 2000;
const FADE = "opacity 400ms, scale 300ms";

let current = 0;
let timer = null;

function line(text, index) {
  const active = index === current;
  return {
    type: "text",
    key: "line-" + index,  // the same node from one step to the next, so the change animates
    text,
    size: 34,
    weight: active ? 700 : 500,
    color: "#FFFFFFFF",
    align: "center",
    opacity: active ? 1 : 0.35,
    scale: active ? 1.08 : 1,
    transition: FADE,
  };
}

function view() {
  return {
    type: "layer",
    children: [
      { type: "image", src: ARTWORK, fit: "cover" },
      { type: "stack", background: "acrylic(#99101010, 0.55)" },
      {
        type: "stack",
        align: "center",
        valign: "center",
        maxWidth: 900,
        padding: "48,32,48,32",
        gap: 20,
        children: [
          { type: "text", text: "Fullscreen demo", size: 64, weight: 800, color: "#FFFFFFFF", align: "center" },
          { type: "text", text: "Lines that fade, on the compositor", size: 20, color: "#B3FFFFFF", align: "center" },
          { type: "stack", gap: 14, padding: "32,0,0,0", children: LINES.map(line) },
        ],
      },
    ],
  };
}

function stop() {
  if (timer !== null) clearInterval(timer);
  timer = null;
}

function open() {
  current = 0;
  Ampify.ui.fullscreen.open(view());
  stop();
  timer = setInterval(() => {
    current = (current + 1) % LINES.length;
    Ampify.ui.fullscreen.open(view());  // the same session: the view is updated in place
  }, STEP_MS);
}

// Closed by the user (or taken by another element): no more updates, or the timer would open it again.
Ampify.ui.fullscreen.onClose(stop);
Ampify.onStop(stop);

Ampify.ui.topbar.addButton({
  label: "Fullscreen demo",
  icon: "",  // FullScreen, in Segoe Fluent Icons
  onClick: () => (timer === null ? open() : (stop(), Ampify.ui.fullscreen.close())),
});
