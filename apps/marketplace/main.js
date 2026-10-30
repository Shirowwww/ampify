// La Marketplace du kit : une page d'Apple Music pour installer, retirer et choisir thèmes, schémas, extensions et
// apps. Elle ne fait rien elle-même : chaque action passe par amk (AMK.kit.run), avec les mêmes contrôles qu'en
// console (SHA-256, archive vérifiée, sauvegarde avant un retrait). Spécification : docs/marketplace/PAGE.md.
const { h, useState, useEffect } = AMK.React;
const { Card, Button, ButtonPrimary, ButtonTertiary, Toggle, Dropdown, TextField, Text } = AMK.components;

// Une phrase par permission, montrée avant l'installation et avant l'activation.
const PERMISSIONS = {
  "player.read": "voit le morceau en cours",
  "player.control": "lance, met en pause et change de morceau",
  "audio.analyze": "analyse le son (tempo, volume)",
  "audio.gain": "change le volume interne du son, jamais la barre de volume",
  "audio.eq": "règle un égaliseur",
  storage: "garde ses réglages",
  notifications: "affiche des notifications",
  "ipc.discord": "parle à Discord (statut)",
  "midi.input": "lit un contrôleur MIDI",
  "library.read": "lit la bibliothèque",
  "library.write": "modifie la bibliothèque",
  "services.read": "lit l'état des services du kit",
  "ui.keyboard": "ajoute des raccourcis clavier",
  "ui.topbar": "ajoute un bouton en haut",
  "ui.playbar": "ajoute un bouton dans la barre de lecture",
  "ui.menu": "ajoute des entrées de menu",
  "ui.popup": "ouvre des fenêtres surgissantes",
  "ui.panel": "affiche un panneau à droite",
  "ui.page": "ajoute une page",
  "ui.window": "ouvre une fenêtre",
  "ui.tray": "ajoute une icône près de l'horloge",
  "fs.output": "écrit des fichiers texte dans son dossier du kit",
};

const describePermission = (permission) =>
  permission.startsWith("network:") ? "envoie des requêtes à " + permission.slice(8) : PERMISSIONS[permission] || permission;

const permissionText = (list) =>
  list && list.length ? "Peut : " + list.map(describePermission).join(", ") + "." : "Ne demande aucune permission.";

const KINDS = { theme: "Thème", extension: "Extension", app: "App", snippet: "Snippet" };

const matches = (item, search) =>
  !search || [item.name, item.id, item.description, item.author].some((field) =>
    String(field || "").toLowerCase().includes(search.toLowerCase()));

// La dernière ligne utile de la sortie d'amk, pour le bandeau.
const lastLine = (text) => String(text || "").trim().split(/\r?\n/).filter(Boolean).pop() || "";

function ThemeCard({ theme, active, activeScheme, busy, run }) {
  const schemes = theme.schemes || [];
  const [scheme, setScheme] = useState(active && activeScheme ? activeScheme : schemes[0] || "");
  const [confirm, setConfirm] = useState(false);
  const choose = (wanted) =>
    run(["theme", "set", theme.id].concat(wanted ? ["--scheme", wanted] : []), "Thème « " + theme.name + " » choisi");
  return h(Card, { title: theme.name + (active ? "  (actif)" : ""), subtitle: (theme.author ? theme.author + " · " : "") + (theme.description || "") },
    h("row", { gap: 12 },
      schemes.length > 1
        ? h(Dropdown, {
            label: "Schéma",
            value: scheme,
            options: schemes,
            disabled: busy,
            onChange: (value) => {
              setScheme(value);
              if (active) choose(value);  // thème déjà actif : le schéma change tout de suite
            },
          })
        : null,
      active
        ? h(Text, { variant: "caption" }, "Appliqué")
        : h(ButtonPrimary, { label: "Choisir ce thème", disabled: busy || !theme.valid, onClick: () => choose(scheme) }),
      confirm
        ? h(Button, { label: "Confirmer le retrait", disabled: busy, onClick: () => run(["remove", theme.id, "--yes"], "Thème retiré") })
        : h(ButtonTertiary, { label: "Retirer", disabled: busy, onClick: () => setConfirm(true) })));
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
        onChange: (on) => run([kind, on ? "enable" : "disable", element.id], (on ? "Activé : " : "Coupé : ") + element.id),
      }),
      element.id === "marketplace"
        ? null
        : confirm
          ? h(Button, { label: "Confirmer le retrait", disabled: busy, onClick: () => run(["remove", element.id, "--yes"], "Retiré : " + element.id) })
          : h(ButtonTertiary, { label: "Retirer", disabled: busy, onClick: () => setConfirm(true) })));
}

function CatalogCard({ entry, installed, busy, run }) {
  const update = installed && installed.version !== entry.version;
  return h(Card, { title: entry.name, subtitle: KINDS[entry.kind] + " · " + entry.author + " · version " + entry.version },
    h(Text, { variant: "caption" }, entry.description),
    h(Text, { variant: "caption" }, permissionText(entry.permissions)),
    installed && !update
      ? h(Text, { variant: "caption" }, "Installé")
      : h(ButtonPrimary, {
          label: update ? "Mettre à jour" : "Installer",
          disabled: busy,
          onClick: () => run([update ? "update" : "install", entry.id, "--yes"], (update ? "Mis à jour : " : "Installé : ") + entry.name),
        }));
}

function Marketplace() {
  const [state, setState] = useState(null);
  const [message, setMessage] = useState("Lecture du catalogue…");
  const [busy, setBusy] = useState(false);
  const [search, setSearch] = useState("");

  const refresh = () =>
    AMK.kit.run(["market", "--json"]).then((result) => {
      try {
        setState(JSON.parse(result.out));
        setMessage(result.err ? lastLine(result.err) : "");
      } catch (error) {
        setMessage("Impossible de lire l'état du kit : " + (lastLine(result.err) || "réponse illisible"));
      }
    });

  // Une action : amk, puis l'état relu. Le bandeau dit ce qui s'est passé, ou l'erreur d'amk telle quelle.
  const run = (args, success) => {
    setBusy(true);
    setMessage("En cours…");
    return AMK.kit.run(args).then((result) => {
      setMessage(result.code === 0 ? success : lastLine(result.err) || lastLine(result.out) || "Échec (code " + result.code + ")");
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
      h(TextField, { placeholder: "Chercher un thème, une extension…", value: search, onChange: setSearch }),
      h(Button, { label: "Actualiser", disabled: busy, onClick: () => { setMessage("Lecture du catalogue…"); refresh(); } })),
    message ? h(Text, { variant: "caption" }, message) : null,
    h(Text, { variant: "subtitle" }, "Thèmes"),
    h("row", { gap: 12 },
      h(Text, { variant: "caption" }, activeTheme ? "Thème actif : " + activeTheme + (state.scheme ? " (" + state.scheme + ")" : "") : "Apple Music d'origine, sans thème"),
      activeTheme ? h(Button, { label: "Revenir à Apple Music d'origine", disabled: busy, onClick: () => run(["theme", "clear"], "Thème retiré") }) : null),
    themes.length
      ? h("list", { gap: 12 }, themes.map((theme) =>
          h(ThemeCard, { key: theme.id, theme, active: theme.id === activeTheme, activeScheme: state.scheme, busy, run })))
      : h(Text, { variant: "caption" }, "Aucun thème installé. amk theme import en reprend un de Spicetify."),
    h(Text, { variant: "subtitle" }, "Extensions et apps"),
    others.length
      ? h("list", { gap: 12 }, others.map((element) => h(ElementCard, { key: element.kind + element.id, element, busy, run })))
      : h(Text, { variant: "caption" }, "Aucune extension installée."),
    h(Text, { variant: "subtitle" }, "Catalogue"),
    !state.catalogOk
      ? h(Text, { variant: "caption" }, "Impossible de lire le catalogue. Réessaie quand tu es connecté.")
      : catalog.length
        ? h("list", { gap: 12 }, catalog.map((entry) => h(CatalogCard, { key: entry.kind + entry.id, entry, installed: find(entry), busy, run })))
        : h(Text, { variant: "caption" }, "Le catalogue est vide pour l'instant."));
}

// La page « /marketplace » de l'app (AMK.ready la rend), et un bouton en haut d'Apple Music pour l'ouvrir.
function render() {
  return h(Marketplace);
}
globalThis.render = render;

const BAG = '<svg viewBox="0 0 24 24"><path d="M4 7h16l-1.2 12.2a2 2 0 0 1-2 1.8H7.2a2 2 0 0 1-2-1.8L4 7zm4 0V6a4 4 0 0 1 8 0v1h-2V6a2 2 0 0 0-4 0v1H8z"/></svg>';
AMK.ui.title.set("Marketplace");
AMK.ui.topbar.addButton({ label: "Marketplace", icon: BAG, onClick: () => AMK.ui.navigate("/marketplace") });
