// The Marketplace: a page of Apple Music to install, remove and choose themes, schemes, extensions and apps. It does
// nothing itself: every action goes through ampify (Ampify.kit.run), with the same checks as in the console (SHA-256,
// verified archive, backup before a removal). Specification: docs/extensions/catalog.md.
const { h, useState, useEffect } = Ampify.React;
const { Card, Button, ButtonPrimary, ButtonTertiary, Dropdown, Text } = Ampify.components;

// Texts in English and French; Ampify.i18n picks the Windows display language. Apple Music's own French words where it
// has one (Activé, Désactivé, Retour).
Ampify.i18n.define({
  en: {
    "tab.theme": "Themes", "tab.extension": "Extensions", "tab.app": "Apps", "tab.snippet": "Snippets", "tab.installed": "Installed",
    "empty.theme": "No theme in the catalog yet.",
    "empty.extension": "No extension in the catalog yet.",
    "empty.app": "No app in the catalog yet.",
    "empty.snippet": "No snippet in the catalog yet. A snippet is a few lines of user.css: ampify new snippet starts one.",
    "empty.installed": "Nothing installed yet. Pick a theme or an extension in the other tabs.",
    "kind.theme": "Theme", "kind.palette": "Colours", "kind.extension": "Extension", "kind.app": "App", "kind.snippet": "Snippet",
    "by": "by {author}", "version": "version {version}",
    "state.broken": "Cannot load, see ampify doctor", "state.installed": "Installed", "state.inUse": "In use",
    "state.on": "On", "state.off": "Off", "state.update": "Update to {version}",
    "action.install": "Install", "action.update": "Update", "action.use": "Use", "action.turnOff": "Turn off",
    "action.turnOn": "Turn on", "action.details": "Details", "action.uninstall": "Uninstall",
    "action.confirmUninstall": "Confirm uninstall", "action.refresh": "Refresh", "action.clearSearch": "Clear the search",
    "action.appleLook": "Use Apple Music's look", "action.previous": "Previous", "action.next": "Next",
    "uninstall.themeFirst": "Turn the theme off first",
    "perm.none": "Asks for no permission.", "perm.can": "It can:", "perm.count": "Asks for {count} permission(s)",
    "perm.new": "new in version {version}",
    "scheme": "Scheme", "tags": "Tags: {tags}",
    "theme.none": "No theme: Apple Music's own look.", "theme.inUse": "Theme in use: {name}",
    "search.theme": "Search themes", "search.extension": "Search extensions", "search.app": "Search apps",
    "search.snippet": "Search snippets", "search.installed": "Search what is installed",
    "search.none": "Nothing matches \"{search}\"",
    "search.where": "Search looks in names, authors, descriptions and tags.",
    "catalog.unreadable": "The catalog cannot be read",
    "catalog.unreadableText": "Check the connection, then refresh. What is installed stays under Installed.",
    "catalog.offline": "The catalog cannot be read right now: these are the installed copies.",
    "empty.title": "Nothing here yet", "page": "Page {page} of {pages}",
    "status.reading": "Reading the catalog…", "status.working": "Working…",
    "status.unreadable": "Cannot read the state of Ampify: {error}", "status.unreadableReply": "unreadable reply",
    "status.failed": "Failed: {error}", "status.failedCode": "Failed (code {code})",
    "done.installedInUse": "{name} installed and in use", "done.installedOn": "{name} installed and on",
    "done.installedUse": "{name} installed. Read what it can do, then use it.",
    "done.installedTurnOn": "{name} installed. Read what it can do, then turn it on.",
    "done.updated": "{name} updated to {version}", "done.inUse": "{name} in use",
    "done.inUseScheme": "{name} in use, {scheme} scheme", "done.appleLook": "Apple Music's own look is back",
    "done.on": "{name} turned on", "done.off": "{name} turned off", "done.removed": "{name} uninstalled",
    "preview": "Preview of {name}",
  },
  fr: {
    "tab.theme": "Thèmes", "tab.extension": "Extensions", "tab.app": "Apps", "tab.snippet": "Snippets", "tab.installed": "Installés",
    "empty.theme": "Aucun thème dans le catalogue pour l'instant.",
    "empty.extension": "Aucune extension dans le catalogue pour l'instant.",
    "empty.app": "Aucune app dans le catalogue pour l'instant.",
    "empty.snippet": "Aucun snippet dans le catalogue pour l'instant. Un snippet tient en quelques lignes de user.css : ampify new snippet en crée un.",
    "empty.installed": "Rien d'installé pour l'instant. Choisissez un thème ou une extension dans les autres onglets.",
    "kind.theme": "Thème", "kind.palette": "Couleurs", "kind.extension": "Extension", "kind.app": "App", "kind.snippet": "Snippet",
    "by": "par {author}", "version": "version {version}",
    "state.broken": "Ne se charge pas, voir ampify doctor", "state.installed": "Installé", "state.inUse": "Utilisé",
    "state.on": "Activé", "state.off": "Désactivé", "state.update": "Mise à jour {version}",
    "action.install": "Installer", "action.update": "Mettre à jour", "action.use": "Utiliser", "action.turnOff": "Désactiver",
    "action.turnOn": "Activer", "action.details": "Détails", "action.uninstall": "Désinstaller",
    "action.confirmUninstall": "Confirmer la désinstallation", "action.refresh": "Actualiser",
    "action.clearSearch": "Effacer la recherche", "action.appleLook": "Revenir au look d'Apple Music",
    "action.previous": "Précédente", "action.next": "Suivante",
    "uninstall.themeFirst": "Désactivez d'abord le thème",
    "perm.none": "Ne demande aucune permission.", "perm.can": "Il peut :", "perm.count": "Demande {count} permission(s)",
    "perm.new": "nouveau dans la version {version}",
    "scheme": "Variante", "tags": "Mots-clés : {tags}",
    "theme.none": "Aucun thème : le look d'Apple Music.", "theme.inUse": "Thème utilisé : {name}",
    "search.theme": "Rechercher un thème", "search.extension": "Rechercher une extension", "search.app": "Rechercher une app",
    "search.snippet": "Rechercher un snippet", "search.installed": "Rechercher dans ce qui est installé",
    "search.none": "Rien ne correspond à « {search} »",
    "search.where": "La recherche porte sur les noms, auteurs, descriptions et mots-clés.",
    "catalog.unreadable": "Le catalogue est illisible",
    "catalog.unreadableText": "Vérifiez la connexion, puis actualisez. Ce qui est installé reste dans Installés.",
    "catalog.offline": "Le catalogue est illisible pour l'instant : voici les copies installées.",
    "empty.title": "Rien ici pour l'instant", "page": "Page {page} sur {pages}",
    "status.reading": "Lecture du catalogue…", "status.working": "En cours…",
    "status.unreadable": "Impossible de lire l'état d'Ampify : {error}", "status.unreadableReply": "réponse illisible",
    "status.failed": "Échec : {error}", "status.failedCode": "Échec (code {code})",
    "done.installedInUse": "{name} installé et utilisé", "done.installedOn": "{name} installé et activé",
    "done.installedUse": "{name} installé. Lisez ce qu'il peut faire, puis utilisez-le.",
    "done.installedTurnOn": "{name} installé. Lisez ce qu'il peut faire, puis activez-le.",
    "done.updated": "{name} mis à jour en {version}", "done.inUse": "{name} utilisé",
    "done.inUseScheme": "{name} utilisé, variante {scheme}", "done.appleLook": "Le look d'Apple Music est revenu",
    "done.on": "{name} activé", "done.off": "{name} désactivé", "done.removed": "{name} désinstallé",
    "preview": "Aperçu de {name}",
  },
});
const t = (key, values) => Ampify.i18n.t(key, values);

// One sentence per permission, shown before the installation and before turning it on.
const PERMISSIONS = {
  en: {
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
    network: "send requests to {host}",
  },
  fr: {
    "player.read": "voir le morceau en cours",
    "player.control": "lancer, mettre en pause et changer de morceau",
    "audio.analyze": "analyser le son (tempo, volume)",
    "audio.gain": "changer le volume interne du son, jamais la barre de volume",
    "audio.eq": "régler un égaliseur",
    storage: "garder ses réglages",
    notifications: "afficher des notifications",
    "ipc.discord": "parler à Discord (statut)",
    "midi.input": "lire un contrôleur MIDI",
    "library.read": "lire la bibliothèque",
    "library.write": "modifier la bibliothèque",
    "services.read": "lire l'état des services d'Ampify",
    "ui.keyboard": "ajouter des raccourcis clavier",
    "ui.topbar": "ajouter un bouton en haut",
    "ui.playbar": "ajouter un bouton dans la barre de lecture",
    "ui.sidebar": "ajouter une entrée dans la barre latérale",
    "ui.menu": "ajouter des entrées de menu",
    "ui.popup": "ouvrir des fenêtres surgissantes",
    "ui.panel": "afficher un panneau à droite",
    "ui.page": "ajouter une page",
    "ui.window": "ouvrir une fenêtre",
    "ui.tray": "ajouter une icône près de l'horloge",
    "ui.fullscreen": "couvrir la fenêtre d'une vue plein écran",
    "fs.output": "écrire des fichiers texte dans son dossier Ampify",
    "kit.manage": "installer et retirer des éléments avec ampify",
    network: "envoyer des requêtes à {host}",
  },
};
const permissionTexts = PERMISSIONS[Ampify.i18n.language] || PERMISSIONS.en;
const fill = (pattern, values) => pattern.replace(/\{(\w+)\}/g, (all, name) => (name in values ? values[name] : all));

const describePermission = (permission) =>
  permission.startsWith("network:")
    ? fill(permissionTexts.network, { host: permission.slice(8) })
    : permissionTexts[permission] || PERMISSIONS.en[permission] || permission;

const TABS = ["theme", "extension", "app", "snippet", "installed"];
const KIND_ORDER = ["theme", "extension", "app", "snippet"];
// Segoe Fluent Icons glyphs (Color, Puzzle, AllApps, Code) for an element without a preview picture.
const GLYPHS = { theme: "", extension: "", app: "", snippet: "" };
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
  // The manifest's translation for Windows' language, else the English name and description.
  const translated = (source.translations && source.translations[Ampify.i18n.language]) || {};
  return {
    key: source.kind + ":" + source.id,
    id: source.id,
    kind: source.kind,
    name: translated.name || source.name || source.id,
    author: source.author || "",
    version: (installed || entry).version || "",
    description: translated.description || source.description || "",
    tags: source.tags || [],
    // The permissions of the copy a click acts on: the installed one, or the catalog's for an install or an update.
    permissions: (installed && !update ? installed : entry).permissions || [],
    preview: (installed && installed.preview) || (entry && entry.preview) || "",
    entry,
    installed,
    update,
  };
}

// A theme that only brings colours, by its "colors" tag: the card says so, as Spicetify's colour schemes.
const kindLabel = (item) =>
  t(item.kind === "theme" && item.tags.includes("colors") ? "kind.palette" : "kind." + item.kind);

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

// What a card says about the element next to its button. Empty when it is not installed.
function stateOf(item, activeTheme) {
  if (!item.installed) return "";
  if (!item.installed.valid) return t("state.broken");
  const update = item.update ? t("state.update", { version: item.entry.version }) : "";
  if (item.kind === "theme" && item.id !== activeTheme) return update || t("state.installed");
  const now = t(item.kind === "theme" ? "state.inUse" : item.installed.enabled ? "state.on" : "state.off");
  return update ? now + " · " + update : now;
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

function permissionDetails(item) {
  if (!item.permissions.length) return t("perm.none");
  const added = new Set(addedPermissions(item));
  return t("perm.can") + "\n" + item.permissions
    .map((p) => "• " + describePermission(p) + (added.has(p) ? " (" + t("perm.new", { version: item.entry.version }) + ")" : ""))
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
    return h("image", { src: item.preview, label: t("preview", { name: item.name }), fit: fit || "cover", height,
                        cornerRadius: 6, align: "stretch", onClick });
  }
  // A faint grey that reads on light and dark cards alike.
  return h("stack", { height, cornerRadius: 6, background: "#14808080", align: "stretch" },
    h("icon", { icon: GLYPHS[item.kind], size: height > BAND_HEIGHT ? 32 : 24, label: kindLabel(item),
                align: "center", valign: "center", grow: true }));
}

// The main action of an element, from its state: install, update, use a theme, or turn it on and off. On a card, an
// action that grants permissions opens the detail view instead, where they are all written; its label ends with an
// ellipsis, as a Windows button that asks more before acting.
function MainAction({ item, activeTheme, busy, act, open }) {
  if (!item.installed) return h(ButtonPrimary, { label: t("action.install"), disabled: busy, onClick: () => act.install(item) });
  const ask = Boolean(open) && permissionsToGrant(item, activeTheme).length > 0;
  const primary = (label, onClick) =>
    h(ButtonPrimary, { label: ask ? label + "…" : label, disabled: busy, onClick: ask ? open : onClick });
  if (item.update) return primary(t("action.update"), () => act.update(item));
  if (!item.installed.valid) return null;
  if (item.kind === "theme") {
    return item.id === activeTheme
      ? h(Button, { label: t("action.turnOff"), disabled: busy, onClick: act.clearTheme })
      : primary(t("action.use"), () => act.use(item));
  }
  return item.installed.enabled
    ? h(Button, { label: t("action.turnOff"), disabled: busy, onClick: () => act.toggle(item, false) })
    : primary(t("action.turnOn"), () => act.toggle(item, true));
}

// A card: picture, name, who made it, two lines of description, tags; then the action, the state and Details. The
// permissions are listed in the detail view; the card only counts them, and says nothing when there are none.
function ItemCard({ item, activeTheme, busy, act, open, previewHeight }) {
  const meta = [kindLabel(item), item.author && t("by", { author: item.author }), item.version && "v" + item.version]
    .filter(Boolean).join(" · ");
  const state = stateOf(item, activeTheme);
  const count = item.permissions.length;
  // Every line keeps its height, so the buttons of a row of cards line up.
  return h(Card, { grow: true, valign: "stretch", padding: 14 },
    h(Preview, { item, height: previewHeight, onClick: open }),
    h(Text, { weight: 600, wrap: false }, item.name),
    h(Text, { variant: "caption", wrap: false }, meta),
    h("stack", { height: 34 }, h(Text, { variant: "caption", maxLines: 2 }, item.description)),
    h(Text, { variant: "caption", wrap: false }, [item.tags.slice(0, 3).join(" · "),
                                                  count ? t("perm.count", { count }) : ""].filter(Boolean).join("   ") || " "),
    h("row", { gap: 10 },
      h(MainAction, { item, activeTheme, busy, act, open }),
      state ? h(Text, { variant: "caption", valign: "center", wrap: false }, state) : null,
      h("stack", { grow: true }),
      h(ButtonTertiary, { label: t("action.details"), onClick: open })));
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
      [kindLabel(item), item.author && t("by", { author: item.author }), item.version && t("version", { version: item.version })]
        .filter(Boolean).join(" · ")),
    state ? h(Text, { weight: 600 }, state) : null,
    h(Text, null, item.description),
    item.tags.length ? h(Text, { variant: "caption" }, t("tags", { tags: item.tags.join(", ") })) : null,
    h(Text, { variant: "caption" }, permissionDetails(item)),
    item.installed && schemes.length > 1
      ? h(Dropdown, { label: t("scheme"), value: shownScheme(item, active ? scheme : ""), options: schemes, disabled: busy,
                      onChange: (value) => act.use(item, value) })
      : null,
    // The spacer keeps both buttons on the left: a row of two children spreads them apart.
    h("row", { gap: 8 },
      h(MainAction, { item, activeTheme, busy, act }),
      !item.installed ? null
        : confirm
          ? h(Button, { label: t("action.confirmUninstall"), disabled: busy,
                        onClick: () => act.remove(item).then((ok) => ok && back()) })
          : h(ButtonTertiary, { label: t("action.uninstall"), disabled: busy || !removable,
                                tooltip: removable ? "" : t("uninstall.themeFirst"), onClick: () => setConfirm(true) }),
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
  if (!activeTheme) return h(Text, { variant: "caption" }, t("theme.none"));
  const theme = items.find((item) => item.kind === "theme" && item.id === activeTheme && item.installed);
  const schemes = (theme && theme.installed.schemes) || [];
  return h("row", { gap: 12 },
    h(Text, { wrap: false, valign: "center" }, t("theme.inUse", { name: theme ? theme.name : activeTheme })),
    theme && schemes.length > 1
      ? h(Dropdown, { value: shownScheme(theme, scheme), options: schemes, disabled: busy,
                      onChange: (value) => act.use(theme, value) })
      : null,
    h("stack", { grow: true }),
    h(ButtonTertiary, { label: t("action.appleLook"), disabled: busy, onClick: act.clearTheme }));
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
    h(ButtonTertiary, { label: t("action.previous"), disabled: page === 0, onClick: () => setPage(page - 1) }),
    h(Text, { variant: "caption", valign: "center" }, t("page", { page: page + 1, pages })),
    h(ButtonTertiary, { label: t("action.next"), disabled: page >= pages - 1, onClick: () => setPage(page + 1) }));
}

// The page mounts anew each time it opens: it shows the last state read at once, while ampify reads it again, on the
// tab of the last visit.
let lastState = null;
let lastTab = "theme";

function Marketplace() {
  const [state, setState] = useState(lastState);
  const [message, setMessage] = useState(lastState ? "" : t("status.reading"));
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
        setMessage(t("status.unreadable", { error: lastLine(result.err) || t("status.unreadableReply") }));
      }
    }, (error) => setMessage(t("status.unreadable", { error: String(error) })));
  const reread = () => {
    setMessage(t("status.reading"));
    return refresh(true);
  };

  // One action is one or more ampify commands, stopped at the first failure, then the state read again. The status
  // line says what happened, or ampify's error as it is. Resolves to true when every command succeeded.
  const run = (commands, success) => {
    setBusy(true);
    setMessage(t("status.working"));
    const step = (index) => index === commands.length
      ? Promise.resolve("")
      : Ampify.kit.run(commands[index]).then((result) => result.code === 0
          ? step(index + 1)
          : lastLine(result.err) || lastLine(result.out) || t("status.failedCode", { code: result.code }));
    return step(0)
      .catch((error) => t("status.failed", { error: String(error) }))
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
      const theme = item.kind === "theme";
      const turnOn = theme ? ["theme", "set", item.id] : [item.kind, "enable", item.id];
      if (!item.permissions.length) {
        return run([["install", item.id, "--yes"], turnOn],
                   t(theme ? "done.installedInUse" : "done.installedOn", { name: item.name }));
      }
      return run([["install", item.id, "--yes"]], t(theme ? "done.installedUse" : "done.installedTurnOn", { name: item.name }))
        .then((ok) => ok && setSelected(item.key));
    },
    update: (item) => run([["update", item.id, "--yes"]], t("done.updated", { name: item.name, version: item.entry.version })),
    use: (item, scheme) => run([["theme", "set", item.id].concat(scheme ? ["--scheme", scheme] : [])],
                               scheme ? t("done.inUseScheme", { name: item.name, scheme }) : t("done.inUse", { name: item.name })),
    clearTheme: () => run([["theme", "clear"]], t("done.appleLook")),
    toggle: (item, on) => run([[item.kind, on ? "enable" : "disable", item.id]], t(on ? "done.on" : "done.off", { name: item.name })),
    remove: (item) => run([["remove", item.id, "--yes"]], t("done.removed", { name: item.name })),
  };

  useEffect(() => {
    refresh(true);
  }, []);

  if (!state) return h("stack", { gap: 12 }, h(Text, null, message));

  // settings.txt may say "none" for no theme, as the kernel reads it.
  const activeTheme = state.theme && state.theme !== "none" ? state.theme : "";
  const items = itemsOf(state);
  const current = TABS.includes(tab) ? tab : TABS[0];
  const tabLabel = t("tab." + current);

  const detail = selected && items.find((item) => item.key === selected);
  if (detail) {
    return h(Details, { item: detail, activeTheme, scheme: state.scheme, busy, act, back: () => setSelected(""),
                        backLabel: tabLabel, message });
  }

  const inTab = current === "installed"
    ? items.filter((item) => item.installed)
        .sort((a, b) => KIND_ORDER.indexOf(a.kind) - KIND_ORDER.indexOf(b.kind) || a.name.localeCompare(b.name))
    : items.filter((item) => item.kind === current);
  const shown = inTab.filter((item) => matches(item, search));
  const pages = Math.max(1, Math.ceil(shown.length / PAGE_SIZE));
  const shownPage = Math.min(page, pages - 1);
  const visible = shown.slice(shownPage * PAGE_SIZE, (shownPage + 1) * PAGE_SIZE);
  const catalogTab = current !== "installed";

  const empty = search.trim()
    ? h(Empty, { key: "empty", title: t("search.none", { search: search.trim() }), text: t("search.where"),
                 action: h(Button, { label: t("action.clearSearch"), align: "center", onClick: () => setSearch("") }) })
    : catalogTab && !state.catalogOk
      ? h(Empty, { key: "empty", title: t("catalog.unreadable"), text: t("catalog.unreadableText"),
                   action: h(Button, { label: t("action.refresh"), align: "center", disabled: busy, onClick: reread }) })
      : h(Empty, { key: "empty", title: t("empty.title"), text: t("empty." + current) });

  // Keyed sections: the status line coming and going does not make the kernel rebuild the cards under it.
  return h("stack", { gap: 16 },
    h("row", { key: "bar", gap: 8 },
      TABS.map((kind) => h(Button, { key: kind, label: t("tab." + kind), variant: kind === current ? "secondary" : "tertiary",
                                     onClick: () => setTab(kind) })),
      h("textfield", { key: "search", placeholder: t("search." + current), value: search, grow: true,
                       onChange: (event) => setSearch(event.value) }),
      h(Button, { key: "refresh", label: t("action.refresh"), disabled: busy, onClick: reread })),
    message ? h(Text, { key: "status", variant: "caption" }, message) : null,
    current === "theme" || current === "installed"
      ? h(ThemeBar, { key: "theme", items, activeTheme, scheme: state.scheme, busy, act })
      : null,
    catalogTab && !state.catalogOk && visible.length
      ? h(Text, { key: "offline", variant: "caption" }, t("catalog.offline"))
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
