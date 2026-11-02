// The Marketplace: a page of Apple Music to install, remove and choose themes, schemes, extensions and apps. It does
// nothing itself: every action goes through ampify (Ampify.kit.run), with the same checks as in the console (SHA-256,
// verified archive, backup before a removal). Specification: docs/extensions/catalog.md.
const { h, useState, useEffect } = Ampify.React;
const { Card, Button, ButtonPrimary, ButtonTertiary, Dropdown, Text } = Ampify.components;

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
  "ui.fullscreen": "cover the window with a full-screen view",
  "fs.output": "write text files in its Ampify folder",
  "kit.manage": "install and remove elements through ampify",
};

const describePermission = (permission) =>
  permission.startsWith("network:") ? "send requests to " + permission.slice(8) : PERMISSIONS[permission] || permission;

const TABS = [
  { kind: "theme", label: "Themes", empty: "No theme in the catalog yet." },
  { kind: "extension", label: "Extensions", empty: "No extension in the catalog yet." },
  { kind: "app", label: "Apps", empty: "No app in the catalog yet." },
  { kind: "snippet", label: "Snippets", empty: "No snippet in the catalog yet. A snippet is a few lines of user.css: ampify new snippet starts one." },
  { kind: "installed", label: "Installed", empty: "Nothing installed yet. Pick a theme or an extension in the other tabs." },
];
const KINDS = { theme: "Theme", extension: "Extension", app: "App", snippet: "Snippet" };
const KIND_ORDER = ["theme", "extension", "app", "snippet"];
// Segoe Fluent Icons glyphs (Color, Puzzle, AllApps, Code) for an element without a preview picture.
const GLYPHS = { theme: "", extension: "", app: "", snippet: "" };
const COLUMNS = 3;
const PAGE_SIZE = 12;  // four rows of cards keep the page far below the kernel's 300 elements
const PICTURE_HEIGHT = 180;
const BAND_HEIGHT = 72;  // a row of cards without any picture gets a short band with the kind's icon

// The last useful line of ampify's output, for the status line.
const lastLine = (text) => String(text || "").trim().split(/\r?\n/).filter(Boolean).pop() || "";

// True when version a is above version b: "0.10.0" is above "0.9.1".
function newer(a, b) {
  const left = String(a).split(".").map(Number);
  const right = String(b).split(".").map(Number);
  for (let i = 0; i < Math.max(left.length, right.length); i++) {
    if ((left[i] || 0) !== (right[i] || 0)) return (left[i] || 0) > (right[i] || 0);
  }
  return false;
}

// The catalog entry and the installed copy of one element, as one card. The installed picture comes first: it is local.
function merge(entry, installed) {
  const source = entry || installed;
  const update = Boolean(entry && installed && newer(entry.version, installed.version));
  return {
    key: source.kind + ":" + source.id,
    id: source.id,
    kind: source.kind,
    name: source.name || source.id,
    author: source.author || "",
    version: (installed || entry).version || "",
    description: source.description || "",
    tags: source.tags || [],
    // The permissions of the copy a click acts on: the installed one, or the catalog's for an install or an update.
    permissions: (installed && !update ? installed : entry).permissions || [],
    preview: (installed && installed.preview) || (entry && entry.preview) || "",
    entry,
    installed,
    update,
  };
}

// Every element once: the catalog, then what is installed without being in it (a local or development copy).
function itemsOf(state) {
  const installed = state.installed.filter((item) => !(item.kind === "app" && item.id === "marketplace"));
  const keyOf = (item) => item.kind + ":" + item.id;
  const copies = new Map(installed.map((item) => [keyOf(item), item]));
  const items = state.catalog.map((entry) => merge(entry, copies.get(keyOf(entry))));
  const listed = new Set(state.catalog.map(keyOf));
  installed.filter((item) => !listed.has(keyOf(item))).forEach((item) => items.push(merge(null, item)));
  return items.sort((a, b) => a.name.localeCompare(b.name));
}

const matches = (item, search) => {
  const wanted = search.trim().toLowerCase();
  return !wanted || [item.name, item.id, item.description, item.author, item.tags.join(" ")]
    .some((field) => String(field).toLowerCase().includes(wanted));
};

// What a card says about the element before its tags. Empty when it is not installed.
function stateOf(item, activeTheme) {
  if (!item.installed) return "";
  if (!item.installed.valid) return "Cannot load, see ampify doctor";
  const version = item.update ? item.entry.version : "";
  if (item.kind === "theme" && item.id !== activeTheme) return version ? "Update to " + version : "Installed";
  const now = item.kind === "theme" ? "In use" : item.installed.enabled ? "On" : "Off";
  return version ? now + " · update to " + version : now;
}

// The permissions the catalog's version adds to the installed copy.
function addedPermissions(item) {
  if (!item.update) return [];
  const had = new Set(item.installed.permissions || []);
  return item.permissions.filter((permission) => !had.has(permission));
}

// The permissions the main action of an installed element grants: all of them when it turns the element on, the
// added ones when it updates an element that is on.
function permissionsToGrant(item, activeTheme) {
  const on = item.kind === "theme" ? item.id === activeTheme : item.installed.enabled;
  if (item.update) return on ? addedPermissions(item) : [];
  return on ? [] : item.permissions;
}

// A theme may carry a script too, so every kind is described by its permissions.
const permissionLine = (item) =>
  item.permissions.length ? "Can " + item.permissions.map(describePermission).join(", ") : "Asks for no permission";

function permissionDetails(item) {
  if (!item.permissions.length) return "It asks for no permission.";
  const added = new Set(addedPermissions(item));
  return "It can:\n" + item.permissions
    .map((p) => "• " + describePermission(p) + (added.has(p) ? " (new in version " + item.entry.version + ")" : ""))
    .join("\n");
}

// The scheme a menu shows: the one in use when the theme has it ("auto" or a stale name does not), else its default.
function shownScheme(theme, scheme) {
  const schemes = theme.installed.schemes || [];
  if (schemes.includes(scheme)) return scheme;
  return schemes.includes(theme.installed.defaultScheme) ? theme.installed.defaultScheme : schemes[0];
}

// The element's picture, or without one a band with the icon of its kind. Without a height, the picture takes the
// width it is given at its own proportions.
function Preview({ item, height, fit, onClick }) {
  if (item.preview) {
    return h("image", { src: item.preview, label: "Preview of " + item.name, fit: fit || "cover", height,
                        cornerRadius: 4, align: "stretch", onClick });
  }
  // A faint grey that reads on light and dark cards alike.
  return h("stack", { height, cornerRadius: 4, background: "#14808080", align: "stretch" },
    h("icon", { icon: GLYPHS[item.kind], size: height > BAND_HEIGHT ? 32 : 24, label: KINDS[item.kind],
                align: "center", valign: "center", grow: true }));
}

// The main action of an element, from its state: install, update, use a theme, or turn it on and off. On a card, an
// action that grants permissions opens the detail view instead, where they are all written; its label ends with an
// ellipsis, as a Windows button that asks more before acting.
function MainAction({ item, activeTheme, busy, act, open }) {
  if (!item.installed) return h(ButtonPrimary, { label: "Install", disabled: busy, onClick: () => act.install(item) });
  const ask = Boolean(open) && permissionsToGrant(item, activeTheme).length > 0;
  const primary = (label, onClick) =>
    h(ButtonPrimary, { label: ask ? label + "…" : label, disabled: busy, onClick: ask ? open : onClick });
  if (item.update) return primary("Update", () => act.update(item));
  if (!item.installed.valid) return null;
  if (item.kind === "theme") {
    return item.id === activeTheme
      ? h(Button, { label: "Turn off", disabled: busy, onClick: act.clearTheme })
      : primary("Use", () => act.use(item));
  }
  return item.installed.enabled
    ? h(Button, { label: "Turn off", disabled: busy, onClick: () => act.toggle(item, false) })
    : primary("Turn on", () => act.toggle(item, true));
}

function ItemCard({ item, activeTheme, busy, act, open, previewHeight }) {
  const labels = [stateOf(item, activeTheme)].concat(item.tags.slice(0, 3)).filter(Boolean).join(" · ");
  // Every line keeps its height, so the buttons of a row of cards line up.
  return h(Card, { grow: true, valign: "stretch", padding: 12 },
    h(Preview, { item, height: previewHeight, onClick: open }),
    h(Text, { weight: 600, wrap: false }, item.name),
    h(Text, { variant: "caption", wrap: false }, [item.author, item.version && "version " + item.version].filter(Boolean).join(" · ")),
    h("stack", { height: 34 }, h(Text, { variant: "caption", maxLines: 2 }, item.description)),
    h(Text, { variant: "caption", wrap: false }, labels || KINDS[item.kind]),
    h(Text, { variant: "caption", wrap: false, tooltip: permissionLine(item) }, permissionLine(item)),
    h("row", { gap: 8 },
      h(MainAction, { item, activeTheme, busy, act, open }),
      h(ButtonTertiary, { label: "Details", onClick: open })));
}

function Grid({ items, renderCard }) {
  const rows = [];
  for (let start = 0; start < items.length; start += COLUMNS) {
    const row = items.slice(start, start + COLUMNS);
    const previewHeight = row.some((item) => item.preview) ? PICTURE_HEIGHT : BAND_HEIGHT;
    const cells = row.map((item) => renderCard(item, previewHeight));
    while (cells.length < COLUMNS) cells.push(h("stack", { key: "filler" + cells.length, grow: true }));
    rows.push(h("row", { key: "row" + start, gap: 16 }, cells));
  }
  return h("stack", { gap: 16 }, rows);
}

function Details({ item, activeTheme, scheme, busy, act, back, backLabel, message }) {
  const [confirm, setConfirm] = useState(false);
  const schemes = (item.installed && item.installed.schemes) || [];
  const active = item.kind === "theme" && item.id === activeTheme;
  const removable = item.installed && !active;
  const state = stateOf(item, activeTheme);
  const info = h("stack", { key: "content", gap: 12, align: "start", valign: "start", width: item.preview ? 360 : undefined,
                            maxWidth: 640 },
    h(Text, { variant: "title" }, item.name),
    h(Text, { variant: "caption" },
      [KINDS[item.kind], item.author && "by " + item.author, item.version && "version " + item.version].filter(Boolean).join(" · ")),
    state ? h(Text, { weight: 600 }, state) : null,
    h(Text, null, item.description),
    item.tags.length ? h(Text, { variant: "caption" }, "Tags: " + item.tags.join(", ")) : null,
    h(Text, { variant: "caption" }, permissionDetails(item)),
    item.installed && schemes.length > 1
      ? h(Dropdown, { label: "Scheme", value: shownScheme(item, active ? scheme : ""), options: schemes, disabled: busy,
                      onChange: (value) => act.use(item, value) })
      : null,
    // The spacer keeps both buttons on the left: a row of two children spreads them apart.
    h("row", { gap: 8 },
      h(MainAction, { item, activeTheme, busy, act }),
      !item.installed ? null
        : confirm
          ? h(Button, { label: "Confirm uninstall", disabled: busy,
                        onClick: () => act.remove(item).then((ok) => ok && back()) })
          : h(ButtonTertiary, { label: "Uninstall", disabled: busy || !removable,
                                tooltip: removable ? "" : "Turn the theme off first", onClick: () => setConfirm(true) }),
      h("stack", { grow: true })));
  // Keyed, as on the list: the status line coming and going leaves the rest in place.
  return h("stack", { gap: 16 },
    h("row", { key: "back", gap: 8 }, h(ButtonTertiary, { label: "‹ " + backLabel, onClick: back }), h("stack", { grow: true })),
    message ? h(Text, { key: "status", variant: "caption" }, message) : null,
    item.preview
      ? h("row", { key: "content", gap: 24 },
          h("stack", { grow: true, valign: "start" }, h(Preview, { item, fit: "contain" })), info)
      : info);
}

function ThemeBar({ items, activeTheme, scheme, busy, act }) {
  if (!activeTheme) return h(Text, { variant: "caption" }, "No theme: Apple Music's own look.");
  const theme = items.find((item) => item.kind === "theme" && item.id === activeTheme && item.installed);
  const schemes = (theme && theme.installed.schemes) || [];
  return h("row", { gap: 12 },
    h(Text, { wrap: false }, "Theme in use: " + (theme ? theme.name : activeTheme)),
    theme && schemes.length > 1
      ? h(Dropdown, { value: shownScheme(theme, scheme), options: schemes, disabled: busy,
                      onChange: (value) => act.use(theme, value) })
      : null,
    h("stack", { grow: true }),
    h(ButtonTertiary, { label: "Use Apple Music's look", disabled: busy, onClick: act.clearTheme }));
}

function Empty({ title, text, action }) {
  return h("stack", { gap: 8, padding: "48,0,48,0", align: "center" },
    h(Text, { variant: "subtitle", align: "center" }, title),
    h(Text, { variant: "caption", align: "center" }, text),
    action || null);
}

function Pager({ page, pages, setPage }) {
  if (pages <= 1) return null;
  return h("row", { gap: 8 },
    h(ButtonTertiary, { label: "Previous", disabled: page === 0, onClick: () => setPage(page - 1) }),
    h(Text, { variant: "caption" }, "Page " + (page + 1) + " of " + pages),
    h(ButtonTertiary, { label: "Next", disabled: page >= pages - 1, onClick: () => setPage(page + 1) }));
}

// The page mounts anew each time it opens: it shows the last state read at once, while ampify reads it again, on the
// tab of the last visit.
let lastState = null;
let lastTab = "theme";

function Marketplace() {
  const [state, setState] = useState(lastState);
  const [message, setMessage] = useState(lastState ? "" : "Reading the catalog…");
  const [busy, setBusy] = useState(false);
  const [tab, setTabState] = useState(lastTab);
  const [search, setSearchState] = useState("");
  const [page, setPage] = useState(0);
  const [selected, setSelected] = useState("");  // the key of the element shown in detail

  const setTab = (value) => {
    lastTab = value;
    setTabState(value);
    setPage(0);
    setSelected("");
  };
  const setSearch = (value) => {
    setSearchState(value);
    setPage(0);
  };

  // Reads the state again. A reading the user asked for puts the catalog's warning on the status line, or clears it;
  // after an action, the line keeps what the action said.
  const refresh = (reading) =>
    Ampify.kit.run(["market", "--json"]).then((result) => {
      try {
        lastState = JSON.parse(result.out);
        setState(lastState);
        if (reading) setMessage(lastLine(result.err));
      } catch (error) {
        setMessage("Cannot read the state of Ampify: " + (lastLine(result.err) || "unreadable reply"));
      }
    }, (error) => setMessage("Cannot read the state of Ampify: " + error));
  const reread = () => {
    setMessage("Reading the catalog…");
    return refresh(true);
  };

  // One action is one or more ampify commands, stopped at the first failure, then the state read again. The status
  // line says what happened, or ampify's error as it is. Resolves to true when every command succeeded.
  const run = (commands, success) => {
    setBusy(true);
    setMessage("Working…");
    const step = (index) => index === commands.length
      ? Promise.resolve("")
      : Ampify.kit.run(commands[index]).then((result) => result.code === 0
          ? step(index + 1)
          : lastLine(result.err) || lastLine(result.out) || "Failed (code " + result.code + ")");
    return step(0)
      .catch((error) => "Failed: " + error)
      .then((failure) => {
        setMessage(failure || success);
        return refresh().then(() => !failure);
      })
      .finally(() => setBusy(false));
  };

  // An element without permissions is installed and put on in one click. One with permissions stays off: its detail
  // view opens, with the list of what it may do above the button that turns it on.
  const act = {
    install: (item) => {
      const turnOn = item.kind === "theme" ? ["theme", "set", item.id] : [item.kind, "enable", item.id];
      if (!item.permissions.length) {
        return run([["install", item.id, "--yes"], turnOn],
                   item.name + (item.kind === "theme" ? " installed and in use" : " installed and on"));
      }
      const next = item.kind === "theme" ? "use it" : "turn it on";
      return run([["install", item.id, "--yes"]], item.name + " installed. Read what it can do, then " + next + ".")
        .then((ok) => ok && setSelected(item.key));
    },
    update: (item) => run([["update", item.id, "--yes"]], item.name + " updated to " + item.entry.version),
    use: (item, scheme) => run([["theme", "set", item.id].concat(scheme ? ["--scheme", scheme] : [])],
                               item.name + " in use" + (scheme ? ", " + scheme + " scheme" : "")),
    clearTheme: () => run([["theme", "clear"]], "Apple Music's own look is back"),
    toggle: (item, on) => run([[item.kind, on ? "enable" : "disable", item.id]], item.name + (on ? " turned on" : " turned off")),
    remove: (item) => run([["remove", item.id, "--yes"]], item.name + " uninstalled"),
  };

  useEffect(() => {
    refresh(true);
  }, []);

  if (!state) return h("stack", { gap: 12 }, h(Text, null, message));

  // settings.txt may say "none" for no theme, as the kernel reads it.
  const activeTheme = state.theme && state.theme !== "none" ? state.theme : "";
  const items = itemsOf(state);
  const current = TABS.find((entry) => entry.kind === tab) || TABS[0];

  const detail = selected && items.find((item) => item.key === selected);
  if (detail) {
    return h(Details, { item: detail, activeTheme, scheme: state.scheme, busy, act, back: () => setSelected(""),
                        backLabel: current.label, message });
  }

  const inTab = tab === "installed"
    ? items.filter((item) => item.installed)
        .sort((a, b) => KIND_ORDER.indexOf(a.kind) - KIND_ORDER.indexOf(b.kind) || a.name.localeCompare(b.name))
    : items.filter((item) => item.kind === tab);
  const shown = inTab.filter((item) => matches(item, search));
  const pages = Math.max(1, Math.ceil(shown.length / PAGE_SIZE));
  const shownPage = Math.min(page, pages - 1);
  const visible = shown.slice(shownPage * PAGE_SIZE, (shownPage + 1) * PAGE_SIZE);
  const catalogTab = tab !== "installed";

  const empty = search.trim()
    ? h(Empty, { key: "empty", title: "Nothing matches \"" + search.trim() + "\"", text: "Search looks in names, authors, descriptions and tags.",
                 action: h(Button, { label: "Clear the search", align: "center", onClick: () => setSearch("") }) })
    : catalogTab && !state.catalogOk
      ? h(Empty, { key: "empty", title: "The catalog cannot be read", text: "Check the connection, then refresh. What is installed stays under Installed.",
                   action: h(Button, { label: "Refresh", align: "center", disabled: busy, onClick: reread }) })
      : h(Empty, { key: "empty", title: "Nothing here yet", text: current.empty });

  // Keyed sections: the status line coming and going does not make the kernel rebuild the cards under it.
  return h("stack", { gap: 16 },
    h("row", { key: "bar", gap: 8 },
      TABS.map((entry) => h(Button, { key: entry.kind, label: entry.label, variant: entry.kind === tab ? "secondary" : "tertiary",
                                      onClick: () => setTab(entry.kind) })),
      h("textfield", { key: "search", placeholder: "Search " + current.label.toLowerCase(), value: search, grow: true,
                       onChange: (event) => setSearch(event.value) }),
      h(Button, { key: "refresh", label: "Refresh", disabled: busy, onClick: reread })),
    message ? h(Text, { key: "status", variant: "caption" }, message) : null,
    tab === "theme" || tab === "installed"
      ? h(ThemeBar, { key: "theme", items, activeTheme, scheme: state.scheme, busy, act })
      : null,
    catalogTab && !state.catalogOk && visible.length
      ? h(Text, { key: "offline", variant: "caption" }, "The catalog cannot be read right now: these are the installed copies.")
      : null,
    visible.length
      ? h(Grid, { key: "grid", items: visible,
                  renderCard: (item, previewHeight) => h(ItemCard, { key: item.key, item, activeTheme, busy, act, previewHeight,
                                                                     open: () => setSelected(item.key) }) })
      : empty,
    h(Pager, { key: "pager", page: shownPage, pages, setPage }));
}

// The app's "/marketplace" page (Ampify.ready renders it), and the "Ampify" entry of the sidebar that opens it.
function render() {
  return h(Marketplace);
}
globalThis.render = render;

// Ampify's logo as a silhouette: the puzzle piece, drawn in the text color like Apple Music's own sidebar icons.
const AMPIFY_ICON =
  '<svg viewBox="0 0 64 64"><path d="M12.3 13L19 13C21.5 13 22.25 11.25 20.75 9.25C17.75 5 20.5 .75 26 .75' +
  "C31.5 .75 34.25 5 31.25 9.25C29.75 11.25 30.5 13 33 13L39.7 13A11.3 11.3 0 0 1 51 24.3L51 31C51 33.5 52.75 34.25" +
  " 54.75 32.75C59 29.75 63.25 32.5 63.25 38C63.25 43.5 59 46.25 54.75 43.25C52.75 41.75 51 42.5 51 45L51 51.7" +
  'A11.3 11.3 0 0 1 39.7 63L12.3 63A11.3 11.3 0 0 1 1 51.7L1 24.3A11.3 11.3 0 0 1 12.3 13Z"/></svg>';

Ampify.ui.title.set("Marketplace");
Ampify.ui.sidebar.addItem({ label: "Ampify", icon: AMPIFY_ICON, onClick: () => Ampify.ui.navigate("/marketplace") });
