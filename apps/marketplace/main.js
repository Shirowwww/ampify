// The Marketplace: a page of Apple Music to install, remove and choose themes, schemes, extensions and apps. It does
// nothing itself: every action goes through ampify (Ampify.kit.run), with the same checks as in the console (SHA-256,
// verified archive, backup before a removal). Specification: docs/extensions/catalog.md.
const { h, useState, useEffect } = Ampify.React;
const { Card, Button, ButtonPrimary, ButtonTertiary, Toggle, Dropdown, TextField, Text } = Ampify.components;

// One sentence per permission, shown before the installation and before turning it on.
const PERMISSIONS = {
  "player.read": "see the track that is playing",
  "player.control": "play, pause and change track",
  "audio.analyze": "analyze the sound (tempo, volume)",
  "audio.gain": "change the internal volume of the sound, never the volume bar",
  "audio.eq": "set an equalizer",
  storage: "keep its settings",
  notifications: "show notifications",
  "ipc.discord": "talk to Discord (status)",
  "midi.input": "read a MIDI controller",
  "library.read": "read the library",
  "library.write": "change the library",
  "services.read": "read the state of Ampify's services",
  "ui.keyboard": "add keyboard shortcuts",
  "ui.topbar": "add a button at the top",
  "ui.playbar": "add a button in the playback bar",
  "ui.sidebar": "add an entry to the sidebar",
  "ui.menu": "add menu entries",
  "ui.popup": "open pop-up windows",
  "ui.panel": "show a panel on the right",
  "ui.page": "add a page",
  "ui.window": "open a window",
  "ui.tray": "add an icon near the clock",
  "fs.output": "write text files in its Ampify folder",
};

const describePermission = (permission) =>
  permission.startsWith("network:") ? "send requests to " + permission.slice(8) : PERMISSIONS[permission] || permission;

const permissionText = (list) =>
  list && list.length ? "Can: " + list.map(describePermission).join(", ") + "." : "Asks for no permission.";

const KINDS = { theme: "Theme", extension: "Extension", app: "App", snippet: "Snippet" };

// Takes the free width of a row, so the last action of every card sits at its right edge.
const SPACER = h("stack", { grow: true });

const matches = (item, search) =>
  !search || [item.name, item.id, item.description, item.author].some((field) =>
    String(field || "").toLowerCase().includes(search.toLowerCase()));

// The last useful line of ampify's output, for the banner.
const lastLine = (text) => String(text || "").trim().split(/\r?\n/).filter(Boolean).pop() || "";

function ThemeCard({ theme, active, activeScheme, busy, run }) {
  const schemes = theme.schemes || [];
  const [scheme, setScheme] = useState(active && activeScheme ? activeScheme : schemes[0] || "");
  const [confirm, setConfirm] = useState(false);
  const choose = (wanted) =>
    run(["theme", "set", theme.id].concat(wanted ? ["--scheme", wanted] : []), "Theme \"" + theme.name + "\" chosen");
  return h(Card, { title: theme.name + (active ? "  (active)" : ""), subtitle: (theme.author ? theme.author + " · " : "") + (theme.description || "") },
    h("row", { gap: 12 },
      schemes.length > 1
        ? h(Dropdown, {
            label: "Scheme",
            value: scheme,
            options: schemes,
            disabled: busy,
            onChange: (value) => {
              setScheme(value);
              if (active) choose(value);  // theme already active: the scheme changes right away
            },
          })
        : null,
      active
        ? h(Text, { variant: "caption" }, "In use")
        : h(ButtonPrimary, { label: "Use this theme", disabled: busy || !theme.valid, onClick: () => choose(scheme) }),
      SPACER,
      // Turning a theme off and deleting it are two different actions: only an unused theme can be uninstalled.
      active
        ? h(Button, { label: "Turn off", disabled: busy, onClick: () => run(["theme", "clear"], "Theme turned off") })
        : confirm
          ? h(Button, { label: "Confirm uninstall", disabled: busy, onClick: () => run(["remove", theme.id, "--yes"], "Theme uninstalled") })
          : h(ButtonTertiary, { label: "Uninstall", disabled: busy, onClick: () => setConfirm(true) })));
}

function ElementCard({ element, busy, run }) {
  const [confirm, setConfirm] = useState(false);
  const kind = element.kind;
  return h(Card, { title: element.name || element.id, subtitle: KINDS[kind] + (element.author ? " · " + element.author : "") },
    h(Text, { variant: "caption" }, element.description || ""),
    h(Text, { variant: "caption" }, permissionText(element.permissions)),
    h("row", { gap: 12 },
      h(Toggle, {
        checked: element.enabled,
        disabled: busy || !element.valid || element.id === "marketplace",
        onChange: (on) => run([kind, on ? "enable" : "disable", element.id], (on ? "Turned on: " : "Turned off: ") + element.id),
      }),
      SPACER,
      element.id === "marketplace"
        ? null
        : confirm
          ? h(Button, { label: "Confirm uninstall", disabled: busy, onClick: () => run(["remove", element.id, "--yes"], "Uninstalled: " + element.id) })
          : h(ButtonTertiary, { label: "Uninstall", disabled: busy, onClick: () => setConfirm(true) })));
}

function CatalogCard({ entry, installed, busy, run }) {
  const update = installed && installed.version !== entry.version;
  return h(Card, { title: entry.name, subtitle: KINDS[entry.kind] + " · " + entry.author + " · version " + entry.version },
    h(Text, { variant: "caption" }, entry.description),
    h(Text, { variant: "caption" }, permissionText(entry.permissions)),
    installed && !update
      ? h(Text, { variant: "caption" }, "Installed")
      : h(ButtonPrimary, {
          label: update ? "Update" : "Install",
          disabled: busy,
          onClick: () => run([update ? "update" : "install", entry.id, "--yes"], (update ? "Updated: " : "Installed: ") + entry.name),
        }));
}

function Marketplace() {
  const [state, setState] = useState(null);
  const [message, setMessage] = useState("Reading the catalog…");
  const [busy, setBusy] = useState(false);
  const [search, setSearch] = useState("");

  const refresh = () =>
    Ampify.kit.run(["market", "--json"]).then((result) => {
      try {
        setState(JSON.parse(result.out));
        setMessage(result.err ? lastLine(result.err) : "");
      } catch (error) {
        setMessage("Cannot read the state of Ampify: " + (lastLine(result.err) || "unreadable reply"));
      }
    });

  // One action: ampify, then the state read again. The banner says what happened, or ampify's error as it is.
  const run = (args, success) => {
    setBusy(true);
    setMessage("Working…");
    return Ampify.kit.run(args).then((result) => {
      setMessage(result.code === 0 ? success : lastLine(result.err) || lastLine(result.out) || "Failed (code " + result.code + ")");
      return refresh();
    }).then(() => setBusy(false), () => setBusy(false));
  };

  useEffect(() => {
    refresh();
  }, []);

  if (!state) return h("stack", { gap: 12 }, h(Text, null, message));

  // settings.txt may say "none" for no theme, as the kernel reads it.
  const activeTheme = state.theme && state.theme !== "none" ? state.theme : "";
  const installed = state.installed.filter((item) => item.id !== "marketplace" || item.kind !== "app");
  const themes = installed.filter((item) => item.kind === "theme" && matches(item, search));
  const others = installed.filter((item) => item.kind !== "theme" && matches(item, search));
  const catalog = state.catalog.filter((entry) => matches(entry, search));
  const find = (entry) => state.installed.find((item) => item.kind === entry.kind && item.id === entry.id);

  return h("stack", { gap: 16 },
    h("row", { gap: 12 },
      h(TextField, { placeholder: "Search for a theme, an extension…", value: search, onChange: setSearch }),
      h(Button, { label: "Refresh", disabled: busy, onClick: () => { setMessage("Reading the catalog…"); refresh(); } })),
    message ? h(Text, { variant: "caption" }, message) : null,
    h(Text, { variant: "subtitle" }, "Themes"),
    h("row", { gap: 12 },
      h(Text, { variant: "caption" }, activeTheme ? "Theme in use: " + activeTheme + (state.scheme ? " (" + state.scheme + ")" : "") : "Apple Music's own look, no theme"),
      activeTheme ? h(Button, { label: "Restore Apple Music's look", disabled: busy, onClick: () => run(["theme", "clear"], "Theme turned off") }) : null),
    themes.length
      ? h("list", { gap: 12 }, themes.map((theme) =>
          h(ThemeCard, { key: theme.id, theme, active: theme.id === activeTheme, activeScheme: state.scheme, busy, run })))
      : h(Text, { variant: "caption" }, "No theme installed. ampify theme import brings one over from Spicetify."),
    h(Text, { variant: "subtitle" }, "Extensions and apps"),
    others.length
      ? h("list", { gap: 12 }, others.map((element) => h(ElementCard, { key: element.kind + element.id, element, busy, run })))
      : h(Text, { variant: "caption" }, "No extension installed."),
    h(Text, { variant: "subtitle" }, "Catalog"),
    !state.catalogOk
      ? h(Text, { variant: "caption" }, "Cannot read the catalog. Try again when you are online.")
      : catalog.length
        ? h("list", { gap: 12 }, catalog.map((entry) => h(CatalogCard, { key: entry.kind + entry.id, entry, installed: find(entry), busy, run })))
        : h(Text, { variant: "caption" }, "The catalog is empty for now."));
}

// The app's "/marketplace" page (Ampify.ready renders it), and the "Ampify" entry of the sidebar that opens it.
function render() {
  return h(Marketplace);
}
globalThis.render = render;

Ampify.ui.title.set("Marketplace");
Ampify.ui.sidebar.addItem({ label: "Ampify", onClick: () => Ampify.ui.navigate("/marketplace") });
