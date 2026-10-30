# Parité avec Spicetify, et au-delà

L'objectif est de reproduire toutes les fonctions de Spicetify, puis d'aller plus loin. Pour chaque fonction, ce
document donne un chemin d'implémentation concret : le mécanisme, le travail à faire, la façon de le prouver, et le
fil de travail qui le construit. Aucune ligne n'est écartée sans chemin de remplacement.

Sources Spicetify : [commandes](https://spicetify.app/docs/cli/commands),
[configuration](https://spicetify.app/docs/customization/config-file),
[thèmes](https://spicetify.app/docs/advanced-usage/themes),
[extensions](https://spicetify.app/docs/development/extensions),
[apps personnalisées](https://spicetify.app/docs/development/custom-apps) et le [dépôt](https://github.com/spicetify/cli).

## Pourquoi tout passe par Windows

Spicetify écrit dans les fichiers de Spotify. Le kit n'écrit rien dans le paquet : il injecte son noyau au lancement,
puis agit dans le processus. Cinq mécanismes servent à presque tout le reste :

1. **Injection et noyau** (M3) : lancement du paquet suspendu, puis chargement du noyau dans `AppleMusic.exe` et
   `AMPLibraryAgent.exe`. Windhawk injecte déjà une DLL dans ces deux processus.
2. **Moteur JavaScript dans le noyau** : QuickJS, une instance par élément, objet global `AMK`.
3. **Arbre XAML dans le processus** : le noyau partage le processus de l'interface. Il lit les ressources de l'app,
   parcourt l'arbre visuel depuis la fenêtre, ajoute des éléments et écoute des événements (ouverture d'un menu,
   changement de page). Toute modification passe par le fil de l'interface.
4. **Projections C++/WinRT des winmd** : les winmd d'Apple Music sont dans le paquet (`knowledge/winmd/`). `cppwinrt`
   génère des projections typées. Le kit appelle alors les services internes (`AMP.Services`) par leur nom, comme
   l'app elle-même.
5. **Son après décodage** : l'égaliseur intégré passe par un « tap » (`AVCFAudioMixInputParametersSetAudioTapProcessor`,
   relevé dans `knowledge/exports/AVFoundationCF.txt`). Le noyau peut s'y brancher pour lire et modifier les échantillons.

Pour la lecture et les commandes, on utilise aussi l'API publique SMTC de Windows, et les appels COM relevés.

## Statuts

- **Clair** : le mécanisme est connu. Il reste à coder et à tester, sans rien découvrir.
- **Limité** : le mécanisme marche, avec une limite écrite dans la ligne.
- **À prouver** : le mécanisme est plausible. Un relevé ou un test sur le PC doit le confirmer.

## Fils de travail

- **JS** : « Construire le moteur d'extensions JavaScript ».
- **XAML** : « Construire le moteur de thèmes XAML ».
- **CLI** : « Créer la CLI et la marketplace ».
- **Noyau** : injection, lancement, mode sans échec (M3).
- **Audio** : tap audio et hooks Atmos (M4).
- **Test** : kit de test sur le PC (M5). Installateur (M6).

## 1. Installation et ligne de commande

| Spicetify | Chemin d'implémentation | Statut | Fil, jalon |
|---|---|---|---|
| Installateur, détection de Spotify | Installateur qui trouve le paquet Apple Music (`Get-AppxPackage`, puis le registre) et crée les dossiers de données. Il ne copie pas l'app. | Clair | Test, M6 |
| `backup` et `apply` (patch des fichiers) | `amk apply` lance l'app avec le noyau injecté : processus suspendu puis injection, ou `IPackageDebugSettings`. Aucun fichier du paquet n'est écrit. | Clair | Noyau, M3 |
| `restore` | `amk restore` lance l'app sans noyau. Il n'y a rien à remettre en place. Pour désinstaller : supprimer le dossier de données et la clé de démarrage. | Clair | Test, M6 |
| `auto` | `amk launch` : vérifie l'installation, applique la configuration, lance l'app. | Clair | Noyau, M3 |
| `config` (lire, écrire) | `amk config [clé] [valeur]`, sur le `settings.txt` existant (`core/settings.*`). | Clair | CLI, M5 |
| `config <liste> <élément>` et `<élément>-` | `amk extension enable <id>` et `amk extension disable <id>`. La syntaxe `amk config extensions +id` et `-id` est aussi acceptée. | Clair | CLI, M7 |
| `apply --extension <fichier>` | `amk extension enable <id>` ne charge que cet élément, sans relancer l'app. | Clair | JS, M7 |
| `watch` | `amk watch` surveille le dossier du thème (`ReadDirectoryChangesW`) et recharge le dictionnaire de ressources à chaque sauvegarde. | Clair | XAML, M8 |
| `update` (rechargement à chaud) | Les extensions se rechargent seules : arrêt puis démarrage. Les thèmes changent de dictionnaire actif. Les éléments XAML déjà affichés gardent l'ancien style jusqu'à leur recréation : `amk watch` recrée la page visible. | Limité | XAML, M8 |
| `enable-devtools` | Trois outils : un port de débogage WebView2 pour les pages web (si l'app en a, à vérifier), un export JSON de l'arbre XAML (voir 7.2), et un panneau d'inspection du kit. | Limité | XAML, M8 |
| `upgrade` | `amk update` lit la dernière release GitHub du kit, vérifie le SHA-256 publié et demande confirmation. | Clair | CLI, après M6 |
| `path`, `config-dir` | `amk path userdata`, `amk path app` et `amk path prefs` affichent les chemins. `amk config --dir` ouvre le dossier. | Clair | CLI, M5 |
| Restauration après une mise à jour cassée | Le noyau se désactive seul sur une version inconnue (`core/version_gate.*`). `amk versions update` récupère la liste des versions testées, publiée dans le catalogue. | Clair | Noyau (fait pour la partie noyau), CLI |
| `--help`, `--version`, enchaînement (`backup apply enable-devtools`) | `amk` accepte plusieurs verbes sur une ligne, par exemple `amk apply devtools`. | Clair | CLI, M5 |
| Anciennes versions (Legacy) | `amk install-kit <version>` installe une release passée, avec son SHA-256. | Clair | CLI, après M6 |

## 2. Configuration

| Clé Spicetify | Chemin d'implémentation | Statut | Fil, jalon |
|---|---|---|---|
| `spotify_path` | `app_path` : chemin du paquet, détecté par l'installateur, modifiable par `amk config app_path`. | Clair | Noyau, M3 |
| `prefs_path` | `prefs_path` : plist de préférences de l'app (relevé). Lecture seule pour le kit, sauf réglage explicite. | Clair | Test, M5 |
| `spotify_launch_flags` | `app_launch_flags` : arguments ajoutés au lancement, par exemple le port de débogage de la ligne 1.10. | Clair | Noyau, M3 |
| `check_spicetify_upgrade` | `check_kit_update` : vérification au démarrage de `amk`, sans téléchargement automatique. | Clair | CLI, M6 |
| `disable_sentry`, `disable_ui_logging` | `telemetry.block`, désactivé par défaut. Voir 8.5. | Limité | Noyau, M5 |
| `remove_rtl_rule` | Sans objet en CSS, il n'y en a pas. Pour une langue de droite à gauche, un thème règle `FlowDirection` élément par élément. | Clair | XAML, si besoin |
| `expose_apis` | `expose_api` : active l'objet `AMK` pour les éléments. Désactivé, aucun élément JavaScript ne démarre. | Clair | JS, M7 |
| `current_theme` | `theme.active` : thème actif (`none` ou vide pour aucun). | Clair | XAML, M8 |
| `color_scheme` | `theme.scheme` : schéma de couleurs du thème actif. | Clair | XAML, M8 |
| `extensions` (liste) | Une clé par élément, `extensions.<id>.enabled`, au lieu d'une liste. | Clair | JS, M7 |
| `custom_apps` (liste) | `apps.<id>.enabled`, même principe. | Clair | JS, M7 |
| `inject_css` | `theme.parts.resources` : les règles deviennent des surcharges de ressources XAML (voir 4.2). | Limité | XAML, M8 |
| `inject_theme_js` | `theme.parts.script` : le `main.js` du thème tourne ou non. | Clair | JS, M7 |
| `replace_colors` | `theme.parts.colors` : les couleurs de `colors.ini` sont appliquées ou non. | Clair | XAML, M8 |
| `overwrite_assets` | `theme.parts.assets` : images par remplacement de la source d'une ressource (`ms-appdata:///`), polices par `FontFamily`. | Limité | XAML, M8 |
| `sidebar_config`, `home_config` (générés) | `sidebar.json` et `home.json`, générés par le kit à partir des apps enregistrées (ordre, masquage). Le plan réel de la barre latérale reste à relever. | À prouver | XAML, M8 |
| `xpui.js_find_8008`, `xpui.js_repl_8008` (patchs) | Table de patchs par version testée, repérés par motif d'octets, jamais par adresse fixe. Voir 8.4. | Limité | Noyau, par version |

## 3. Extensions (API JavaScript)

| Spicetify | Chemin d'implémentation | Statut | Fil, jalon |
|---|---|---|---|
| Fichier JavaScript unique | `main.js` dans un dossier avec `manifest.json`, chargé par le noyau. | Clair | JS, M7 |
| Creator (TypeScript, JSX, compilation) | `amk-creator` : types `amk.d.ts` générés depuis la spec, JSX compilé vers `AMK.React`, un seul fichier en sortie. Le compilateur reste à choisir sous licence permissive. | Clair | CLI, après le moteur JS |
| Attendre que les API existent (boucle `setTimeout`) | `AMK.ready`, une Promise résolue quand le lecteur et l'interface sont prêts. | Clair | JS, M7 |
| `Player` : état, morceau, événements | Lecture par SMTC (titre, artiste, album, état, position) et par COM (`PlaybackEventDelegate`). Identifiant de variante par `AVCFPlayerItemCopyCurrentStableVariantID` (relevé). Le lien avec l'identifiant de morceau Apple reste à établir. | Limité | Noyau, M4 et M5 |
| `Player` : lecture, pause, suivant, précédent, seek | SMTC : `TryPlayAsync`, `TryPauseAsync`, `TrySkipNextAsync`, `TrySkipPreviousAsync`. Ou `MediaAppExecAgentCommandAsync` (relevé). Le seek est à prouver. | Limité | JS, M4 |
| `Player` : volume | `AMK.audio.setGainDb` : gain interne relatif, appliqué sur les échantillons. Un volume absolu suit le curseur : le noyau lit sa position et ajuste le gain interne, sans jamais l'écrire. Voir 8.1. | Limité | Audio, M4 |
| `Platform.History` (navigation) | `AMK.ui.navigate(route)`. Le kit tient une table de routes : chaque route renvoie à une page de l'app (types de `AMP.UI.Views`) ou à une page du kit. Le paramètre attendu par chaque page reste à relever. | Limité | XAML, M8 |
| `Platform.Library`, `Platform.Playlists` | Projection winmd de `AMP.Services` : lecture de la bibliothèque et des playlists par les services de l'app. Lecture d'abord, écriture ensuite, sous une permission `library.write` à créer. | Limité | Noyau et JS, M8 |
| `CosmosAsync` | Voir 8.2. | Limité | Noyau, M8 |
| `GraphQL` | Voir 8.3. | Limité | Noyau, M8 |
| `URI` | `AMK.uri` en JavaScript : analyse et construction des liens `music:`, `musics:`, `itms:` et `https://music.apple.com`. Testé sous Node. | Clair | JS, M7 |
| `React` | `AMK.React` : petit moteur compatible avec l'écriture React (hooks de base, fragments, props), qui rend l'arbre de primitives du kit. | Clair | JS, M7 |
| `ReactDOM` | `AMK.ReactDOM.render(vue, cible)` : la cible est un conteneur du kit (page, popup, panneau). | Clair | JS, M7 |
| `ReactComponent` (Button, Toggle, etc.) | `AMK.components` : Button, Toggle, Slider, Card, List, Dropdown, Tooltip, aux couleurs du thème. Les composants internes de Spotify ne se reprennent pas tels quels. | Clair | JS et XAML, M8 |
| `ReactHook` | `AMK.hooks` : `useState`, `useEffect`, `useMemo`, `useRef`, `useContext`, plus `usePlayer()` et `useTheme()` qui s'abonnent aux événements. | Clair | JS, M7 |
| `TippyProps` | `AMK.ui.tooltip` : `ToolTipService.ToolTip`, avec délai et position. | Clair | XAML, M8 |
| `Topbar` (boutons de la barre du haut) | Repérer le panneau de la barre du haut par parcours de l'arbre (identifiant d'automatisation, sinon type), puis y insérer un `Button`. Le nom du panneau reste à relever. | À prouver | XAML, M8 |
| `Playbar` (boutons de la barre de lecture) | Même principe, sur le panneau qui contient les commandes de lecture. | À prouver | XAML, M8 |
| `ContextMenu` | Écouter `FlyoutBase.Opening` sur les menus de l'app, lire le `DataContext` (le morceau visé), ajouter des `MenuFlyoutItem`. | Limité | XAML, M8 |
| `Menu` (menu de l'app) | Ajouter une entrée à la barre de menus si elle existe, sinon à un menu du kit dans la barre du haut. | À prouver | XAML, M8 |
| `PopupModal` | `ContentDialog` avec le `XamlRoot` de la fenêtre active. | Clair | XAML, M8 |
| `Panel` | Un `Grid` ajouté à droite de la fenêtre, ouvert et fermé par un bouton, largeur mémorisée. La place est partagée avec la mise en page de l'app. | Limité | XAML, M8 |
| `AppTitle` | `AppWindow.Title`, remis à la valeur d'origine à la fermeture. | Clair | XAML, M8 |
| `Keyboard` | `AMK.keyboard.bind("Ctrl+Alt+L", fn)` : `RegisterHotKey` pour les raccourcis globaux, et `KeyDown` sur la fenêtre pour ceux qui ne valent que dans l'app. | Clair | JS, M7 |
| `LocalStorage` | `AMK.storage` : un fichier JSON par élément. | Clair | JS, M7 |
| `Config` | `AMK.config.get` et `AMK.config.set`, limités aux clés de l'élément. | Clair | JS, M7 |
| `Queue` (propriété) | `AMK.player.queue.list()`, lu depuis le service de file d'attente d'`AMP.Services`. Lecture seule d'abord. | Limité | Noyau et JS, M8 |
| `addToQueue`, `removeFromQueue` | `AMK.player.queue.add(id)` et `remove(id)`, par le service de file. Il faut prouver que l'appel passe par le service, et non par l'interface. | À prouver | Noyau, M8 |
| `colorExtractor` | Lire les pixels de la pochette affichée (`RenderTargetBitmap`), puis extraire les couleurs dominantes par quantification. L'algorithme se teste hors de l'app. | Limité | JS et noyau, M8 |
| `getAudioData` | `AMK.audio.analyze()` : niveau, spectre (FFT) et tempo, calculés sur le tap audio, en mémoire. | Limité | Audio, M4 |
| `showNotification` | `AMK.notify({ title, text })` : notification Windows par `AppNotificationManager`. | Clair | JS, M7 |
| `getFontStyle` | `AMK.fonts.style(nom)` : famille et graisse utilisées par le thème pour un rôle (titre, texte). | Clair | XAML, M8 |
| `SVGIcons` | `AMK.icons` : jeu d'icônes livré avec le kit, plus Segoe Fluent Icons (police de Windows 11). La licence du jeu reste à vérifier. | Clair | JS, M7 |
| `Locale` | `AMK.locale` : langue de Windows et langue de l'app. | Clair | JS, M7 |

## 4. Thèmes

| Spicetify | Chemin d'implémentation | Statut | Fil, jalon |
|---|---|---|---|
| `color.ini` (sections, couleurs nommées) | `colors.ini` : chaque clé remplace une couleur ou une brosse du dictionnaire de l'app, pour les thèmes clair et sombre. La liste des clés se relève sur le dictionnaire réel. | Limité | XAML, M8 |
| `user.css` (règles libres) | `resources.xaml` : des styles et des modèles de contrôle surchargent les types standard (`Button`, `TextBlock`…) et les éléments nommés. Pas de sélecteur CSS complet, mais des cibles par type et par nom. | Limité | XAML, M8 |
| `theme.js` | `main.js` du thème, exécuté comme une extension. | Clair | JS, M7 |
| Schémas de couleurs (`[Base]`, `[Dark]`) | Sections de `colors.ini`, et `theme.scheme` pour le choix. | Clair | XAML, M8 |
| Parties d'un thème | `theme.parts.*`, voir 2 (`inject_css`, `inject_theme_js`, `replace_colors`). | Clair | XAML, M8 |
| Images et polices | Voir 2 (`overwrite_assets`). | Limité | XAML, M8 |
| Dépôt officiel de thèmes | Dépôt `amk-themes`, même format, validé en CI par `tools/validate.py`. | Clair | CLI, après M8 |
| Recharger un thème modifié | Voir 1 (`watch`). | Clair | XAML, M8 |

## 5. Apps personnalisées

| Spicetify | Chemin d'implémentation | Statut | Fil, jalon |
|---|---|---|---|
| Dossier `CustomApps/<nom>/` (`index.js`, `manifest.json`, `style.css`) | `apps/<id>/` : `manifest.json` (`kind: app`), `main.js`, icône `.svg`. Le style passe par les ressources (4.2). | Clair | JS, M7 |
| `render()` en React, sans JSX | `AMK.React` et les composants de 3 (`React`, `ReactComponent`). | Clair | JS, M7 et M8 |
| Entrée de barre latérale, icône normale et active | Ajouter un `NavigationViewItem` à la barre latérale, si elle est un `NavigationView` (à vérifier). L'icône : le `d` du SVG devient la géométrie d'un `PathIcon`, pour les icônes d'un seul tracé. L'état actif suit la sélection. | À prouver | XAML, M8 |
| Routage et sous-pages (`Platform.History.push`) | `AMK.ui.navigate(route)` et les routes déclarées par l'app (voir 3). | Limité | XAML, M8 |
| Page en HTML | Option : une page WebView2 créée en code, qui parle au noyau par `AMK.bridge.postMessage`. WebView2 est présent dans le paquet (relevé) ; reste à confirmer qu'il est utilisable comme runtime. | Limité | XAML, M8 |
| `subfiles_extension` (scripts au démarrage) | Une app peut être liée à une extension de même identifiant : l'extension tourne au démarrage, l'app à l'ouverture de sa page. | Clair | JS, M7 |
| `subfiles` (plusieurs fichiers, sous-dossiers) | Modules ES : `import` relatif, limité au dossier de l'élément. | Clair | JS, M7 |

## 6. Snippets et marketplace

| Spicetify | Chemin d'implémentation | Statut | Fil, jalon |
|---|---|---|---|
| Snippets (petits CSS activables) | `kind: snippet` : fragment `resources.xaml` activable par `amk snippet enable <id>`. | Limité | XAML, M8 |
| Marketplace : onglet dans l'app | Phase 1 : `amk market` lit `catalog/catalog.json` et installe. Phase 2 : un onglet, qui est une page d'app (5), lit le même catalogue. | Limité | CLI puis XAML, après M8 |
| Installer et mettre à jour depuis la marketplace | `amk install <id>` et `amk update <id>` : SHA-256 vérifié, permissions affichées avant l'activation. | Clair | CLI, M7 |
| Catalogue public | `catalog/catalog.json`, schéma, validateur et relecture humaine, voir `CATALOG.md`. | Clair | Fait, M7 |

## 7. Outils de développement

| Spicetify | Chemin d'implémentation | Statut | Fil, jalon |
|---|---|---|---|
| Console du développeur | `amk logs --follow` (journal du noyau et `AMK.log`), plus un volet de journal dans le panneau d'inspection. | Clair | CLI, M5 |
| React DevTools | `amk inspect` : arbre de `AMK.React` (inspectable par construction) et export JSON de l'arbre XAML, affiché dans le navigateur. Pour l'interface native, il n'existe pas d'outil officiel équivalent. | Limité | CLI et XAML, M8 |
| Creator (projet, tests, compilation) | `amk new <type>`, `amk build` (compilation, validation du manifeste, zip, SHA-256) et `amk test` (voir 7.5). | Clair | CLI, après le moteur JS |
| Rechargement à chaud pendant le développement | `amk dev <dossier>` : surveille le dossier et recharge l'élément à chaque sauvegarde. | Clair | CLI, après M7 |
| Tests d'éléments | `amk test` exécute le `main.js` dans Node, avec un faux `AMK` qui suit le contrat de l'API. Pas besoin de l'app. Un prototype existe dans `tools/`. | Clair | CLI, après M7 |
| Types pour l'éditeur | `amk.d.ts` généré depuis la même source que `docs/extensions/API.md`. | Clair | JS |

## 8. Les fonctions qui n'étaient pas directes

Cinq points n'avaient pas d'équivalent direct. Ils ont maintenant un chemin, avec sa limite.

### 8.1 Volume du lecteur (`Player.setVolume`)

Règle de l'utilisateur (2026-10-30) : tout changement de gain est interne. Il ne doit jamais se voir sur la barre de volume
d'Apple Music. Concrètement :

- le gain passe par `AMK.audio.setGainDb`, appliqué sur les échantillons décodés. C'est un gain relatif, qui s'ajoute au
  volume choisi par l'utilisateur ;
- un volume absolu (`setVolume`) se fait en lisant la position du curseur, puis en ajustant le gain interne pour que le
  son corresponde à la valeur demandée. Le curseur n'est jamais écrit, et `AVCFPlayerSetVolume` n'est jamais appelé ;
- le noyau ne répond jamais au curseur une autre valeur que celle de l'utilisateur. Un « faux volume » se verrait sur la
  barre, donc il est exclu.

*Statut : Limité.* Le gain relatif est clair. Le suivi du curseur demande de vérifier, sur le PC, que la lecture de
la position est fiable et que le gain suit sans à-coup (M4).

### 8.2 `CosmosAsync` (API interne de Spotify)

Chemin : `AMK.services.call(...)` appelle les services que l'app utilise déjà, par leurs projections de `AMP.Services`.
Les appels sont en lecture seule, sur une liste blanche de méthodes publiée avec le kit. Pas de nouvelle connexion vers
Apple : on réutilise ce que l'app fait déjà. Pour le catalogue public, l'API MusicKit exige un jeton développeur (compte
Apple Developer). Option à ouvrir plus tard, hors v1. *Statut : Limité.*

### 8.3 `GraphQL`

Spotify a une API GraphQL. Apple n'en expose pas d'équivalent connu. Les données passent par les services de 8.2, et
il n'y a pas de traduction GraphQL. *Statut : Limité.*

### 8.4 Section `[Patch]` (patchs du bundle JavaScript)

Chemin en trois étages :

1. **Ressources XAML** du paquet, surchargées à l'exécution (voir 4.2). C'est le cas le plus courant.
2. **Scripts éventuels de l'app** : le paquet contient V8, JavaScriptCoreV8 et le moteur Jet, pour les pages du store.
   Il faut relever s'il existe du JavaScript modifiable à l'exécution.
3. **En dernier recours**, une table de motifs d'octets par version testée. Jamais une adresse fixe, pour respecter la
   règle « résoudre par nom ». Une version absente de la table ne reçoit aucun patch (version gate).

*Statut : Limité.* Chaque mise à jour d'Apple Music demande une vérification.

### 8.5 Préprocessus de télémétrie (`disable_sentry`, `disable_ui_logging`)

Chemin : `telemetry.block`, désactivé par défaut. Le noyau répond localement aux requêtes envoyées aux hôtes de
télémétrie relevés, en accrochant les appels réseau de `AppleMusic.exe` (WinHTTP ou WinINet, selon ce que l'app
utilise). Les hôtes de licence et de DRM ne figurent jamais sur cette liste, et le code ne les touche pas.
*Statut : Limité.* La liste d'hôtes vient d'une capture du trafic, faite sur le PC par le kit de test (M5).

## 9. Au-delà de Spicetify

Idées ajoutées pour aller plus loin. Chacune a un chemin, et son statut suit la même légende.

| Idée | Chemin d'implémentation | Statut | Fil, jalon |
|---|---|---|---|
| Nivellement Atmos | Déjà écrit : extension intégrée au noyau, gain sur les échantillons, cible stéréo. | Limité | Audio, M4 (validé hors app seulement) |
| Égaliseur et effets audio | DSP natif (C++) branché sur le tap, réglé par `AMK.audio.setBand(...)`. Pas de DSP en JavaScript, trop lent. | À prouver | Audio, M4 |
| Mesure LUFS affichée | Résultat du meter BS.1770 (`extensions/atmos-leveler/`) dans le panneau d'inspection. | Clair | Audio et test, M5 |
| Paroles (fenêtre ou surimpression) | `AMK.lyrics.current()` par les services de paroles de l'app (`AMP.Services`), ou un fournisseur ouvert (LRCLIB) via `network:`. | Limité | JS, M8 |
| Discord Rich Presence | Élément qui parle au client Discord par son tube local (`\\.\pipe\discord-ipc-0`), avec une permission `ipc.discord`. | Clair | JS, M7 |
| Scrobbling Last.fm et ListenBrainz | `AMK.http` vers les hôtes déclarés, clés saisies par l'utilisateur et gardées dans `storage`. | Clair | JS, M7 |
| Touches média et raccourcis globaux | SMTC et `RegisterHotKey`. La touche lecture/pause du clavier agit sur l'app. | Clair | Noyau, M3 |
| Mini-lecteur toujours au-dessus | Fenêtre WinUI secondaire (`AppWindow`, `OverlappedPresenter.IsAlwaysOnTop`) avec les commandes SMTC. | Clair | XAML, M8 |
| Icône dans la zone de notification | `Shell_NotifyIconW` depuis le noyau, avec un menu. | Clair | Noyau, M3 |
| Télécommande locale | Serveur HTTP ou WebSocket lié à `127.0.0.1` seulement, jeton créé à l'installation. Sert aux téléphones et aux Stream Deck. | Clair | Noyau, après M7 |
| Contrôleur MIDI | `midiInOpen` (Win32) : notes et contrôleurs reliés à des commandes du kit. Pas de retour lumineux. | Limité | JS et noyau, après M7 |
| « En cours » pour OBS ou un stream | Fichier texte mis à jour, ou source de la télécommande locale. | Clair | JS, M7 |
| Minuteur de sommeil | `AMK.player.pause()` à l'heure choisie, avec la permission `player.control`. | Clair | JS, M7 |
| Statistiques d'écoute | Journal JSON local : titre, artiste, durée écoutée. Métadonnées seulement, jamais de fichier audio ni de flux. | Clair | JS, M7 |
| Playlists M3U8 (import et export) | Services de bibliothèque (voir 3), fichiers locaux. | Limité | JS et noyau, M8 |
| Profils de réglages | `amk profile use <nom>` : un dossier de réglages par profil (travail, soirée, test). | Clair | CLI, M5 |
| Thème qui suit le système ou l'heure | `UISettings.ColorValuesChanged` pour clair et sombre, et horaires dans le thème. | Clair | XAML, M8 |
| Accessibilité | Thème à contraste élevé et réglage de la taille du texte, par les ressources. | Clair | XAML, M8 |
| Diagnostic (`amk doctor`) | Version d'Apple Music, mode sans échec, état de chaque élément, dernières lignes du journal, dans un seul rapport. Sert aux tests. | Clair | CLI, M5 |
| Export et import des réglages | `amk export <fichier.zip>` et `amk import`, sans les fichiers de l'app. | Clair | CLI, M6 |
| Interface du kit en français et en anglais | `AMK.locale` et des fichiers de textes FR et EN. | Clair | JS, M7 |
| Visualiseur dans la barre de lecture | Spectre (voir 3, `getAudioData`) dessiné dans un `Path` ou un canevas. Le coût CPU reste à mesurer. | Limité | XAML, M8 |

## 10. Comment chaque chemin sera prouvé

- **Logique pure** (JavaScript, validateur, mesure, réglages) : tests sous Node et sous Linux, sans l'app.
- **Mécanismes Windows** (injection, arbre XAML, SMTC, notifications) : sondes exécutées sur le PC par le kit de test
  (M5). Le résultat est écrit dans le rapport.
- **Services internes** (projections winmd) : une sonde appelle chaque méthode retenue en lecture seule, et journalise
  le résultat (M3 et M8).
- **Son** (tap, hooks, Atmos) : mesures sur des morceaux réels, sur le PC, avec un enregistrement en mémoire seulement
  (M4).
- **Ce qui ne se teste pas ici** : tout ce qui touche à l'app réelle. Il n'y a ni KVM ni QEMU dans ce conteneur, et
  Apple Music demande Windows, un compte du Microsoft Store et un abonnement. Le compte reste sur le PC de l'utilisateur, comme
  le prévoit `MASTER_PROMPT.md`.

## 11. Décisions

Aucune décision n'est ouverte dans ce document. Le volume a été tranché par l'utilisateur le 2026-10-30 : le gain est interne,
et il ne doit jamais se voir sur la barre de volume (8.1).
