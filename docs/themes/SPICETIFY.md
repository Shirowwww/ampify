# Thèmes Spicetify dans Apple Music

Un `color.ini` de Spicetify marche tel quel dans le kit : posé dans un dossier de thème, ses couleurs habillent
Apple Music. `amk theme import` reprend un thème Spicetify entier en une commande.

## Comment Spicetify marche, et ce qui change ici

Spotify sur PC est une page web (Chromium). Spicetify modifie les fichiers de l'app pour y injecter le CSS du thème
et du JavaScript. Un thème Spicetify, c'est :
- `color.ini` : des schémas de couleurs (`[Base]`, `[Nord]`, `[mocha]`...), chacun avec les mêmes clés (`main`,
  `sidebar`, `text`...). Spicetify en fait des variables CSS `--spice-main`, `--spice-sidebar`...
- `user.css` : des règles CSS qui visent les classes HTML de Spotify (`.main-view-container`, `.Root__nav-bar`...) ;
- parfois `theme.js` : du JavaScript qui tourne dans la page.

Apple Music pour Windows n'est pas une page web : c'est une app native (XAML, WinUI 3). Il n'y a ni HTML ni classes
`.main-...`. Le kit lit donc `color.ini` et traduit chaque clé vers les vrais éléments d'Apple Music. Le `user.css`
d'un thème Spicetify ne peut pas s'appliquer : il est gardé à part, dans `spicetify-user.css`, pour qui veut le
réécrire. `theme.js` est gardé aussi (`spicetify-theme.js`) mais jamais lancé.

## Ce que devient chaque clé

| Clé de `color.ini` | Éléments d'Apple Music | Pinceau Fluent remplacé |
|---|---|---|
| `main` | `ContentPage`, la zone des pages, `Grid#PageRoot`, `NavigationView#NavView` | `SolidBackgroundFillColorBaseBrush`, `ApplicationPageBackgroundThemeBrush` |
| `sidebar` | `Grid#PaneRoot` (barre latérale) | |
| `player` (ou `sidebar`) | `BackDrop` (fond de la barre de lecture) | |
| `card` (ou `main-elevated`) | `NowPlayingControl#LCD > Grid#LCD` (écran du morceau) | `CardBackgroundFillColorDefaultBrush` |
| `text` | le texte de la barre latérale, le titre du morceau | `TextFillColorPrimaryBrush` |
| `subtext` | l'artiste, le temps écoulé et la durée | `TextFillColorSecondaryBrush` |
| `playback-bar` (ou `button`) | la barre de progression de l'écran | |
| `button` | | `AccentFillColorDefaultBrush` |
| `button-active` | | `AccentFillColorSecondaryBrush` |
| `button-disabled` | | `AccentFillColorDisabledBrush` |
| `main-elevated` | | `ControlFillColorDefaultBrush` |
| `highlight` | | `SubtleFillColorSecondaryBrush` |
| `selected-row` | | `SubtleFillColorTertiaryBrush` |
| `notification`, `notification-error` | | `SystemFillColorAttentionBrush`, `SystemFillColorCriticalBrush` |

- Les clés de Spicetify 1 (`main_fg`, `main_bg`, `sidebar_and_player_bg`, `slider_bg`...) sont reprises sous leur
  nom actuel.
- Toute clé du fichier, connue ou non (`accent`, `rose`...), devient une couleur du thème : `$accent` ou
  `var(--spice-accent)` dans `user.css`.
- Le texte des pages suit `TextFillColorPrimaryBrush`, pas une règle : une règle colorerait aussi les légendes
  posées sur les pochettes.
- Le mode clair ou sombre d'Apple Music suit la couleur de `main` (claire ou foncée), quel que soit le nom du schéma.
- Une règle du `user.css` du thème passe toujours devant la traduction de `color.ini`, même moins précise.
- Une clé en majuscules (`TextFillColorPrimaryBrush`) garde son sens habituel : elle remplace la ressource de même
  nom. On peut mélanger les deux styles dans un même fichier.

## Importer un thème

```bash
amk theme import <dossier | archive.zip | adresse> [identifiant]
```

| Source | Exemple |
|---|---|
| Un dossier | `amk theme import C:\Téléchargements\Sleek` |
| Une archive | `amk theme import Sleek.zip` |
| Un dépôt GitHub | `amk theme import https://github.com/catppuccin/spicetify` |
| Un dossier d'un dépôt | `amk theme import https://github.com/spicetify/spicetify-themes/tree/master/Sleek` |

La commande cherche le dossier qui contient `color.ini` ou `user.css`. S'il y en a plusieurs, elle les liste et
demande l'adresse du bon. Le thème arrive dans le kit avec :
- `color.ini`, tel quel ;
- `spicetify-user.css` et `spicetify-theme.js`, gardés à part, jamais appliqués ;
- `user.css`, presque vide, pour tes propres règles ;
- `manifest.json`, avec le premier schéma par défaut ;
- `LICENSE` et `README`, recopiés.

Un dossier qui a déjà un `manifest.json` de thème du kit est copié tel quel. `--yes` remplace un thème du même
identifiant. L'archive passe par les mêmes contrôles que `amk install` (chemins, liens symboliques).

Essayés le 30/10 sur Apple Music 1.1792 : Catppuccin (`mocha` sombre, `latte` clair) et Sleek (19 schémas, dont
`Dracula` et `Nord`).

## Ce qui ne passe pas

- Les règles CSS de Spotify : à réécrire avec les noms d'Apple Music ([GUIDE.md](GUIDE.md), [FORMAT.md](FORMAT.md)).
- Les couleurs écrites `${xrdb:...}` (pywal) : refusées, avec la ligne dans les problèmes du thème.
- `theme.js` : le kit lance seulement le `main.js` d'un thème du kit, avec ses propres API (`AMK`), pas celles de
  Spotify.
