# Parité avec Spicetify

Ce tableau compare, fonction par fonction, Spicetify (Spotify) et le kit (Apple Music pour Windows).
Référence Spicetify : [docs CLI](https://spicetify.app/docs/cli/commands), [configuration](https://spicetify.app/docs/customization/config-file),
[thèmes](https://spicetify.app/docs/advanced-usage/themes), [extensions](https://spicetify.app/docs/development/extensions),
[apps personnalisées](https://spicetify.app/docs/development/custom-apps) et le [dépôt](https://github.com/spicetify/cli).

## Pourquoi l'écart existe

Spicetify réécrit les fichiers de Spotify. L'interface de Spotify est une application web (CEF, React) : du CSS,
du JavaScript et des composants React suffisent pour la modifier.

Apple Music pour Windows est une interface WinUI 3, en XAML. Il n'y a pas de bundle web à réécrire. Le kit ne
modifie donc aucun fichier du paquet, qui est en lecture seule. Il injecte son noyau au lancement (M3). L'apparence
passe par les ressources XAML (M8). Le comportement passe par des hooks dont les noms restent à confirmer (M4 et M8).

Le relevé de `knowledge/apple-music-internals.md` montre que `AppleMusic.exe` embarque WebView2, un moteur
JavaScript (V8 ou JavaScriptCoreV8) et le moteur Jet pour les pages du store. Ce sont des pistes pour des pages web
ou du JavaScript dans l'app. Aucune n'est vérifiée.

## Légende

- **Oui** : faisable avec l'architecture prévue, sans inconnue majeure.
- **Partiel** : faisable pour une partie seulement. La limite est écrite dans la ligne.
- **À étudier** : dépend d'un relevé pas encore fait (winmd, arbre XAML, comportement réel de l'app).
- **Non** : hors de portée, ou écarté pour une raison écrite dans la ligne.

## 1. Installation et ligne de commande

| Spicetify | Équivalent dans le kit | Faisable | Jalon |
|---|---|---|---|
| Installateur, détection du chemin de Spotify | Installateur et désinstallation propre | Oui | M6 |
| `spicetify backup` et `apply` : patcher les fichiers de l'app | Injection au lancement. Aucun fichier du paquet n'est écrit | Oui | M3 |
| `spicetify restore` : revenir à l'original | `amk restore` : lancer sans injecter. Rien à restaurer dans le paquet | Oui | M6 |
| `spicetify auto` : sauvegarder, appliquer, lancer | `amk launch` | Oui | M3 |
| `spicetify config` : lire et écrire les réglages | `amk config`, sur le `settings.txt` déjà en place | Oui | M5 |
| `config <liste> <élément>` et `<élément>-` : ajouter ou retirer une extension | `amk extension enable <id>` et `amk extension disable <id>` | Oui | M7 |
| `apply --extension <fichier>` : appliquer une seule extension | `amk extension enable <id>` avec rechargement de cette seule extension | Oui | M7 |
| `watch` : appliquer à chaque modification des fichiers du thème | `amk watch` : rechargement du thème à la sauvegarde | Partiel : couleurs et ressources XAML, pas le code natif | M8 |
| `update` : rechargement à chaud sans relancer Spotify | Rechargement d'un thème ou d'une extension sans relancer Apple Music | Partiel : les extensions se rechargent, les ressources XAML seulement si elles le permettent | M7, M8 |
| `enable-devtools` : outils de développement Chromium | Outils WebView2 pour les parties web, et arbre visuel XAML pour le reste | Partiel : les pages web et l'arbre XAML, pas l'interface entière | Plus tard |
| `upgrade` et `check_spicetify_upgrade` : mise à jour du CLI | `amk update` : nouvelle version depuis une release GitHub, SHA-256 vérifié | Oui | Après M6 |
| `path`, `config-dir` | `amk path` | Oui | M5 |
| Restauration après une mise à jour cassée | Version gate : sur une version inconnue, le noyau reste inactif. Mode sans échec | Oui | Fait (M1) |
| `preprocesses` : couper Sentry, la télémétrie d'interface, les règles RTL | Non retenu en v1 : le kit ne coupe pas la télémétrie d'Apple | Non | À décider après M4 |
| `spotify_launch_flags` : port de débogage distant | Arguments de lancement d'Apple Music | À étudier | M3 |
| Section `[Patch]` : patchs du bundle JavaScript | Sans objet : pas de bundle à patcher. Les fonctions sont résolues par nom (exports, winmd), jamais par adresse | Non | — |

## 2. Configuration

| Spicetify | Équivalent dans le kit | Faisable | Jalon |
|---|---|---|---|
| `current_theme`, `color_scheme` | `theme.id`, `theme.scheme` dans `settings.txt` | Oui | M8 |
| `extensions`, `custom_apps` (listes) | Une clé par élément : `extensions.<id>.enabled`, `apps.<id>.enabled`, `snippets.<id>.enabled` | Oui | M7 |
| `inject_css`, `inject_theme_js`, `replace_colors` (activer chaque partie) | `theme.parts.colors`, `theme.parts.resources`, `theme.parts.script` | Oui | M8 |
| `overwrite_assets` : remplacer images et polices du thème | Remplacement des ressources graphiques à l'exécution | À étudier : le paquet est en lecture seule, les polices et images passent peut-être par les ressources | M8 |
| `sidebar_config`, `home_config` (générés) | Configuration de la barre latérale, générée par l'app `page` | À étudier : dépend de l'entrée de navigation | M8 |

## 3. Extensions : API JavaScript

| Spicetify | Équivalent dans le kit | Faisable | Jalon |
|---|---|---|---|
| Fichier JavaScript unique, chargé au démarrage | `main.js` dans un dossier avec `manifest.json` | Oui | M7 |
| Creator : TypeScript, JSX, compilation | TypeScript avec fichiers de types `.d.ts`. Pas de JSX en v1 | Partiel | Après M7 |
| Attendre que les `Spicetify.*` existent (boucle `setTimeout`) | Le noyau appelle `main.js` une fois l'API prête. Plus besoin de boucle | Oui | M7 |
| `Player` : état, morceau, événements | `AMK.player.state()` et `AMK.player.on(...)`, à partir de COM (`PlaybackEventDelegate`) | Partiel : à valider sur l'app réelle | M4, M5 |
| `Player` : lecture, pause, suivant, précédent, seek | `AMK.player.play()`, `pause()`, `next()`, `previous()`, `seek()` | Partiel : passe par `MediaAppExecAgentCommandAsync` ou SMTC, à valider | M4 |
| `Player` : volume (`setVolume`) | Non : le curseur d'Apple Music se déplacerait. Le gain se règle sur les échantillons (`AMK.audio.setGainDb`) | Non | — |
| `Platform.History` : navigation entre les pages | `AMK.ui.navigate(chemin)` | À étudier : besoin de la navigation de l'app | M8 |
| `Platform.Library`, `Platform.Playlists` : bibliothèque et playlists | `AMK.library` en lecture, via les services winmd (`AMP.Services`). L'écriture viendra plus tard | Partiel : lecture seulement pour l'instant | M8 |
| `CosmosAsync` : appels à l'API interne de Spotify | Non pour l'API interne d'Apple. Pour les services tiers (Last.fm, ListenBrainz, paroles) : `AMK.http.get` limité aux hôtes déclarés | Partiel | M7 |
| `GraphQL` : API GraphQL de Spotify | Non : API propre à Spotify | Non | — |
| `URI` : lire et construire des liens | `AMK.uri` : liens `music://`, `https://music.apple.com` | Oui | M7 |
| `React`, `ReactDOM`, `ReactComponent`, `ReactHook` | Non : pas de React dans WinUI. Remplacé par `AMK.ui`, une description déclarative rendue en XAML | Partiel | M8 |
| `Topbar` : boutons dans la barre du haut | `AMK.ui.topbar.addButton(...)` | À étudier : emplacement nommé dans l'arbre XAML | M8 |
| `Playbar` : boutons dans la barre de lecture | `AMK.ui.playbar.addButton(...)` | À étudier : même question | M8 |
| `ContextMenu` : entrées du clic droit | `AMK.ui.menu.addItem('context', ...)` | À étudier : le menu contextuel doit être repéré dans l'arbre XAML | M8 |
| `Menu` : entrées du menu de l'app | `AMK.ui.menu.addItem('app', ...)` | À étudier | M8 |
| `PopupModal` : fenêtres modales | `AMK.ui.popup.open(vue)`, avec un `ContentDialog` WinUI | Partiel | M8 |
| `Panel`, `AppTitle` : panneau latéral et titre de fenêtre | `AMK.ui.panel.register(...)` et `AMK.ui.title.set(...)` | À étudier | M8 |
| `showNotification` : message à l'écran | `AMK.notify({title, text})`, par notification Windows | Oui | M7 |
| `Keyboard` : raccourcis clavier | `AMK.keyboard.bind('Ctrl+Alt+L', fn)`. Raccourcis globaux (RegisterHotKey) ou limités à la fenêtre | Oui | M7 |
| `LocalStorage` : stockage par extension | `AMK.storage`, un fichier JSON par extension | Oui | M7 |
| `Config` : lire la configuration | `AMK.config.get(clé)`, limité aux clés de l'extension | Oui | M7 |
| `addToQueue`, `removeFromQueue` : file d'attente | `AMK.player.queue.add(...)` et `remove(...)` | À étudier : file d'attente dans `AMP.Services` | M8 |
| `colorExtractor` : couleur dominante de la pochette | `AMK.color.fromArtwork(url)` | Partiel : il faut trouver où l'app décode la pochette | M8 |
| `getAudioData` : données d'analyse audio | `AMK.audio.onSamples` : analyse locale (niveaux, spectre) sur le son décodé. Pas de données serveur | Partiel : après M4 | M4 |
| `SVGIcons`, `getFontStyle` : icônes et polices | `AMK.icons.get(nom)` : jeu d'icônes fourni par le kit | Oui | M7 |
| `Locale` : textes traduits | `AMK.locale` : langue de l'app | Oui | M7 |
| *(sans équivalent Spicetify)* Hooks audio après décodage : nivellement Atmos, gain, égaliseur | `AMK.audio.onSamples` et `AMK.audio.setGainDb`, sur le modèle du nivellement Atmos | Partiel : à valider sur de vrais morceaux Atmos | M4 |

## 4. Thèmes

| Spicetify | Équivalent dans le kit | Faisable | Jalon |
|---|---|---|---|
| `color.ini` : couleurs nommées, sections de schémas (`[Base]`, `[Dark]`) | `colors.ini`, même forme. Chaque couleur remplace une brosse XAML | Partiel : la liste des brosses à remplacer reste à relever | M8 |
| `user.css` : règles CSS libres | Non en CSS, il n'y a pas de DOM. `resources.xaml` : dictionnaire de ressources, limité à une liste de clés autorisées | Partiel | M8 |
| `theme.js` : JavaScript livré avec le thème | `main` dans le manifeste d'un thème, exécuté comme une extension | Oui | M7 |
| Choix du schéma de couleurs | `defaultScheme` et choix dans les Réglages | Oui | M8 |
| Thème dans le dépôt officiel `spicetify-themes` | Dépôt de thèmes du kit, même principe | Oui | Après M8 |
| Recharger un thème modifié | `amk watch` (voir section 1) | Partiel | M8 |

## 5. Apps personnalisées

| Spicetify | Équivalent dans le kit | Faisable | Jalon |
|---|---|---|---|
| Dossier `CustomApps/<nom>/` avec `index.js`, `manifest.json`, `style.css` | Dossier `apps/<id>/` avec `manifest.json` (`kind: app`), `main.js` et `icon` SVG | Oui pour le format | M7 |
| `render()` en React, sans JSX | `AMK.ui.page.register(chemin, vue)` avec une vue déclarative | Partiel | M8 |
| Entrée dans la barre latérale, icône normale et active | Entrée de navigation, icône SVG en deux états | À étudier | M8 |
| Routage et sous-pages (`Platform.History.push`) | `AMK.ui.navigate` et chemins déclarés par la page | À étudier | M8 |
| Page en HTML, dans WebView2 | Option : une app peut être une page web. WebView2 est déjà dans `AppleMusic.exe` | À étudier : aucun emplacement de page identifié | M8 |
| `subfiles_extension` : scripts lancés au démarrage | Une app peut déclarer des extensions (`permissions` et `main` du manifeste) | Oui | M7 |

## 6. Snippets et marketplace

| Spicetify | Équivalent dans le kit | Faisable | Jalon |
|---|---|---|---|
| Snippets : petits fragments CSS que l'on active | `kind: snippet` : fragment `resources.xaml` activable | Partiel | M8 |
| Marketplace : onglet dans l'app pour parcourir et installer | Catalogue JSON sur GitHub, avec `amk install <id>` d'abord, puis un onglet dans l'app | Partiel : l'onglet dépend de `ui.page` | M7 (catalogue), M8 (onglet) |
| Installation depuis le marketplace avec mise à jour | `amk install` et `amk update` : vérification du SHA-256 à chaque fois | Oui | M7 |

## 7. Outils de développement

| Spicetify | Équivalent dans le kit | Faisable | Jalon |
|---|---|---|---|
| Console du développeur (journaux des extensions) | Journal du noyau et `AMK.log`, lisibles dans `%LOCALAPPDATA%` | Oui | Fait en partie (M1) |
| React DevTools : inspecter l'arbre de composants | Arbre visuel XAML (Live Visual Tree) et outils WebView2 | À étudier | Plus tard |
| Creator : modèle de projet, tests, compilation | `amk new <type>` et les fichiers de types. Pas prévu avant M8 | Partiel | Après M8 |
| Rechargement à chaud d'une extension en développement | Arrêt puis démarrage de la seule extension, sans relancer l'app | Oui | M7 |

## Résumé

Sur les 68 fonctions comparées (sections 1 à 7) :

- **30 Oui** : faisables avec l'architecture prévue.
- **20 Partiel** : faisables en partie, avec la limite écrite dans la ligne.
- **14 À étudier** : à décider après un relevé sur l'app réelle.
- **4 Non** : hors de portée ou écartées.

Les lignes « À étudier » et « Partiel » qui touchent à l'interface se décident surtout après le relevé de l'arbre XAML,
en M8. Les lignes « Partiel » sur le lecteur et le son se décident après M4.

## Ce que le kit fera mieux

- **Le son.** Spicetify ne touche pas à l'audio. Le kit peut agir sur le son décodé (Atmos, gain, égaliseur), sans
  toucher au volume affiché.
- **Pas de patch de fichiers.** Rien n'est écrit dans le paquet MSIX. Une mise à jour d'Apple Music ne laisse pas de
  fichiers modifiés à réparer, et la désinstallation consiste à lancer sans injecter.
- **Les versions connues.** Sur une version non testée, le noyau reste inactif plutôt que de deviner.

## Ce qui ne sera pas copié

- L'API interne de Spotify (`CosmosAsync`, `GraphQL`) : ce n'est pas l'API d'Apple et elle n'est pas publique.
- React dans les extensions : la description déclarative tient le même rôle sans React.
- Le CSS libre : il n'y a pas de DOM. Les thèmes passent par les ressources XAML.
- La télémétrie : décision reportée après M4.

## Ordre de travail

1. **M7 (maintenant)** : format du manifeste, schéma du catalogue, validateur, API et tableau de parité. Ce qui est
   écrit ici ne touche pas à l'app.
2. **M3 et M4** : confirmer le lancement, le moteur d'injection et les hooks audio. Les lignes « Partiel » et
   « À étudier » qui dépendent de l'app réelle se décident là.
3. **M7 (suite)** : runtime JavaScript dans le noyau, chargement des extensions depuis un dossier, `amk extension`,
   `amk install`.
4. **M8** : thèmes XAML, `amk watch`, puis apps, snippets et l'onglet du marketplace, selon les relevés d'arbre XAML.
