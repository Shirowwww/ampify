# API d'extensions

Ce document décrit ce que peut faire une extension, un thème, une app ou un snippet, et comment le noyau les charge.
Le format du fichier `manifest.json` est dans [MANIFEST.md](MANIFEST.md). Le catalogue communautaire est dans
[CATALOG.md](CATALOG.md). Le tableau de comparaison avec Spicetify est dans [PARITE-SPICETIFY.md](PARITE-SPICETIFY.md).

Chaque ligne marquée **[ferme]** est tenue pour acquise : elle ne changera pas sans une nouvelle version majeure de
l'API. Chaque ligne marquée **[provisoire M4]** ou **[provisoire M8]** dépend d'un relevé ou d'un jalon à venir. Elle
peut changer avant ce jalon, et passera en « ferme » une fois validée sur Apple Music réel.

## 1. Principes

- **Les extensions de la communauté sont du JavaScript, dans un bac à sable.** Elles n'ont ni accès au système de
  fichiers, ni appel de code natif, ni accès réseau hors des hôtes qu'elles ont déclarés.
- **Une extension ne touche qu'à ce que l'API expose.** Elle ne peut pas demander un hook sur un composant non listé.
- **L'API est versionnée** (`kitApi`, voir section 9).
- **Règles qui ne changent pas**, car elles viennent du `MASTER_PROMPT.md` :
  - aucun contournement de DRM : pas de hook sur `CoreFP`, `CoreADI64` ni sur le chemin des clés FairPlay ;
  - le son n'est modifié qu'après décodage ;
  - le volume du lecteur n'est jamais modifié, car le curseur d'Apple Music se déplacerait ;
  - aucun enregistrement de la sortie audio sur disque.

## 2. Architecture

```
AppleMusic.exe ou AMPLibraryAgent.exe
  └─ noyau (C++, dossier core/) : réglages, journal, mode sans échec, versions, hôte d'extensions
       ├─ extensions natives intégrées   ex. atmos-leveler, compilé avec le noyau
       └─ runtime JavaScript [provisoire M7]   une instance par extension, objet global AMK
            └─ extensions de la communauté : main.js + manifest.json
```

- **Le runtime JavaScript** est [quickjs-ng](https://github.com/quickjs-ng/quickjs), la branche maintenue de QuickJS
  (licence MIT, compilée en C avec MSVC et MinGW pour Windows, et sous Linux pour les tests). La version vendue et
  son empreinte sont dans `vendor/quickjs/UPSTREAM.txt`. QuickJS 2021-03-27 ne se compilait pas avec MSVC.
- **Les extensions natives** (DLL chargées à l'exécution) ne sont pas ouvertes à la communauté en v1. Une DLL tourne
  dans le processus d'Apple Music avec tous les droits : un catalogue public ne peut pas la garantir. Les extensions
  natives restent intégrées au noyau, comme `atmos-leveler`.
- **Pourquoi du JavaScript** : c'est le modèle de Spicetify, donc les auteurs connaissent déjà la forme. Un script
  se modifie sans recompiler. Le bac à sable limite les dégâts d'un script défectueux ou malveillant aux permissions
  qu'il a demandées. Et le runtime se teste sous Linux.
- **Un même `main.js` peut tourner dans les deux processus.** `AMK.process` vaut `"ui"` dans `AppleMusic.exe` et
  `"engine"` dans `AMPLibraryAgent.exe`. Les permissions `audio.*` n'existent que dans le processus moteur. [provisoire M3]
- **Le manifeste choisit les processus** avec `processes` (voir MANIFEST.md). Par défaut, un élément tourne dans les deux.
  Choix provisoire, décidé avec l'utilisateur : un élément qui écrit son stockage doit déclarer un seul processus, sans quoi
  deux processus écrivent le même fichier. [provisoire M7]

### Organisation du code (pour les contributeurs)

L'objet global est fait de **modules** (`core`, `config`, `storage`, `player`, `audio`, `keyboard`, `notify`, `http`,
`icons`, `uri`, `timers`, `console`, `ui`). Chaque module vit dans ses propres fichiers, pour que plusieurs personnes
puissent en ajouter en même temps sans modifier les mêmes lignes :

| Partie d'un module `foo` | Où |
|---|---|
| JavaScript (le morceau du prélude qui remplit l'objet et, s'il reçoit des événements, `dispatch.foo`) | `core/prelude/foo.cpp`, constante `kFoo`. Il tourne dans son propre bloc : ses constantes lui restent privées. Il voit `native`, l'objet global en construction, `dispatch` et les aides de `core/prelude/frame.cpp` (`text`, `describe`, `runGuarded`). Le gel final est fait par le cadre, pas par le module |
| Fonctions natives (le C++ appelé par `native.xxx`) | `core/natives/foo.cpp`, fonction `DefineFoo`, plus `StartFoo` ou `StopFoo` si le module doit charger ou rendre quelque chose. Les aides communes et la vérification de permission sont dans `core/api/bridge.hpp` (`Require`, `HostOf`, `State<T>` pour un état par élément) |
| Inscription | une ligne dans la table `kModules` de `core/api/modules.cpp` (et la déclaration de `kFoo` et `DefineFoo` juste au-dessus). L'ordre de la table est l'ordre du prélude |
| Événements venus du noyau | `ExtensionLoader::DeliverEvent("foo", charge)` appelle `dispatch.foo(charge)` dans chaque élément actif, sans nouvelle méthode dans les fichiers partagés |
| Service de l'hôte | une capacité optionnelle, déclarée dans l'en-tête du module (`struct FooHost { static constexpr std::string_view kCapabilityName = "foo"; ... }`), lue avec `host.Capability<FooHost>()`, nulle sur les hôtes qui ne l'offrent pas. Seuls les hôtes qui la prennent en charge l'implémentent (voir `core/host.hpp`). Les méthodes virtuelles pures existantes de `Host` ne bougent pas |
| Construction | un nouveau fichier `cmake/sources/<paquet>.cmake` avec `list(APPEND AMK_CORE_SOURCES ...)` et, pour les tests, `list(APPEND AMK_TESTS_SOURCES ...)` (convention en tête de `CMakeLists.txt`) |
| Tests | `tests/foo_tests.cpp`, listé dans le fichier `.cmake` du paquet. `tests/api_module_tests.cpp` fige les membres existants de l'objet global, un par un : un nouveau membre n'y touche pas, une modification d'un membre existant le fait échouer |
| Types et documentation | la section 5 de ce document, `cli/sdk_types.cpp` (`amk types`, vérifié par `tools/test_cli.py`) et, si les exemples s'en servent, l'objet factice de `tools/test_examples.mjs` |

## 3. Cycle de vie

### 3.1 Emplacement

Les éléments installés vivent dans les données de l'utilisateur, jamais dans le paquet MSIX, qui est en lecture seule :

```
%USERPROFILE%\AppleMusicModKit\
  settings.txt
  extensions\<id>\manifest.json, main.js, ...
  apps\<id>\...
  themes\<id>\...
  snippets\<id>\...
  storage\<id>.json            données de AMK.storage
```

Chemin retenu en M3 : `%USERPROFILE%\AppleMusicModKit`, et non `%LOCALAPPDATA%`. MSIX redirige les écritures
d'Apple Music faites sous AppData, donc le lanceur et le noyau ne verraient pas les mêmes fichiers. Les deux
processus chargent les éléments JavaScript, et `AMK.process` permet à un élément de les distinguer.

### 3.2 Étapes

1. **Découverte.** Au démarrage du noyau, chaque sous-dossier est examiné. Un dossier sans `manifest.json` est ignoré.
2. **Validation.** Le manifeste doit respecter le schéma, `id` doit être le nom du dossier, `kitApi` doit être de
   la version majeure prise en charge, et les fichiers déclarés doivent exister. En cas d'échec, l'élément reçoit
   l'état `Failed` et une ligne est écrite dans le journal. Les autres éléments continuent.
3. **Activation.** Un élément ne démarre que s'il est activé dans les réglages (section 3.3). Un élément de la
   communauté est désactivé par défaut : le manifeste ne peut pas changer cette valeur.
4. **Démarrage.** Le noyau évalue `main` (pour une extension ou une app) dans son runtime, puis appelle les
   fonctions enregistrées. Une exception donne l'état `Failed`, sans arrêter les autres.
5. **Arrêt.** Les fonctions passées à `AMK.onStop` sont appelées dans l'ordre inverse du démarrage. Un arrêt qui échoue
   est journalisé et ignoré.
6. **Rechargement.** Une extension se recharge seule : arrêt puis démarrage. Apple Music ne redémarre pas.

### 3.3 Réglages d'activation

| Élément | Clé dans `settings.txt` | Défaut |
|---|---|---|
| Extension | `extensions.<id>.enabled` | `false` pour la communauté |
| App | `apps.<id>.enabled` | `false` |
| Snippet | `snippets.<id>.enabled` | `false` |
| Thème | `theme.active`, `theme.scheme` | aucun thème |
| Parties d'un thème | `theme.parts.colors`, `theme.parts.resources`, `theme.parts.script` | `true` |

La clé `extensions.<id>.enabled` existe déjà dans `core/extension.hpp`. Les autres suivent la même forme. [provisoire M7]

### 3.4 États

Ce sont les états de `core/extension.hpp` :

- `Off` : désactivé dans les réglages.
- `Running` : démarré.
- `Failed` : activé, mais le démarrage a échoué ou a levé une exception.
- `Blocked` : activé, mais le noyau est inactif, soit en mode sans échec, soit sur une version d'Apple Music inconnue.

### 3.5 Mode sans échec et versions inconnues

- Après deux lancements sans que l'app soit « saine », le lancement suivant ne charge aucun élément : ni extension,
  ni thème, ni snippet, ni app. C'est le mode sans échec de `core/safe_mode.*`.
- Sur une version d'Apple Music absente de la liste testée, rien ne démarre. L'app tourne intacte. C'est la règle de
  `core/version_gate.*`.

## 4. Permissions

Un élément déclare les permissions dont il a besoin dans `manifest.json`. Le catalogue les affiche, et l'utilisateur
les voit avant d'activer l'élément. Une fonction protégée par une permission non déclarée lève une erreur, sans rien
faire.

| Permission | Ce qu'elle donne | Statut |
|---|---|---|
| (aucune) | `AMK.log`, `AMK.onStop`, `AMK.config`, `AMK.uri`, `AMK.locale` | [ferme] |
| (aucune) | `AMK.icons` | [provisoire M7] |
| `storage` | `AMK.storage`, un fichier JSON par élément, dans `storage\<id>.json` | [ferme] |
| `notifications` | `AMK.notify`, notification Windows | [provisoire M7] |
| `ui.keyboard` | `AMK.keyboard.bind`, raccourcis globaux ou limités à la fenêtre | [provisoire M7] |
| `player.read` | `AMK.player.state()` et les événements du lecteur | [provisoire M4] |
| `player.control` | `AMK.player.play()`, `pause()`, `next()`, `previous()`, `seek()` | [provisoire M4] |
| `audio.analyze` | `AMK.audio.onSamples`, lecture seule du son décodé, en mémoire | [provisoire M4] |
| `audio.gain` | `AMK.audio.setGainDb`, gain appliqué aux échantillons, borné par le plafond du noyau | [provisoire M4] |
| `ui.topbar`, `ui.playbar`, `ui.menu`, `ui.popup` | Ajout d'éléments d'interface à ces emplacements | [provisoire M8] |
| `ui.panel`, `ui.window`, `ui.tray` | Panneau latéral, fenêtre secondaire, icône de zone de notification | [provisoire M8] |
| `ui.page` | Pages d'app et entrées de navigation | [provisoire M8] |
| `library.read` | `AMK.library`, lecture de la bibliothèque et des playlists | [provisoire M8] |
| `library.write` | Écriture de playlists et de la file, après la lecture | [provisoire M8] |
| `services.read` | `AMK.services.call`, appels en lecture à une liste blanche de services de l'app | [provisoire M8] |
| `ipc.discord` | Tube local vers le client Discord, pour la présence | [provisoire M7] |
| `midi.input` | Entrées d'un contrôleur MIDI | [provisoire, après M7] |
| `network:<hôte>` | `AMK.http.get`, uniquement vers l'hôte déclaré | [ferme] pour le mécanisme |

Chaque chemin de [PARITE-SPICETIFY.md](PARITE-SPICETIFY.md) indique la permission qu'il demande.

Aucune fonction ne modifie le curseur de volume ni `AVCFPlayerSetVolume`. Le gain interne (`audio.gain`) agit sur les
échantillons seulement. Il n'existe pas de permission pour le système de fichiers ni pour le code natif.
Ils ne seront pas ajoutés.

## 5. Objet global `AMK`

Les exemples sont en JavaScript. Les fonctions asynchrones renvoient des `Promise`.

### 5.1 Base [ferme]

```js
AMK.version              // "0.1.0" : version du noyau
AMK.kitApi               // "1.0" : version de l'API visée
AMK.process              // "ui" ou "engine" [provisoire M3], voir section 2
AMK.log(message)         // écrit dans le journal, préfixé par l'identifiant de l'élément
AMK.onStop(fn)           // fn est appelée à l'arrêt de l'élément
AMK.locale               // langue de l'app, ex. "fr-FR" (lecture seule)
```

### 5.2 Réglages et stockage [ferme]

```js
AMK.config.get(clé, défaut)      // lit extensions.<id>.<clé> (ou apps., snippets. selon le type)
AMK.config.set(clé, valeur)      // écrit la même clé, limitée à l'élément
AMK.storage.get(clé, défaut)     // valeur JSON, stockée dans storage\<id>.json   [permission storage]
AMK.storage.set(clé, valeur)
AMK.storage.remove(clé)
```

Un élément ne lit ni n'écrit les clés d'un autre élément.

### 5.3 Lecteur [provisoire M4]

```js
AMK.player.state()
// -> { playing, positionMs, durationMs, track: { id, title, artist, album, variantId, atmos, lossless } }
AMK.player.on("trackchange" | "statechange", fn)
AMK.player.play()
AMK.player.pause()
AMK.player.next()
AMK.player.previous()
AMK.player.seek(ms)
AMK.player.queue.add(trackId)       // [provisoire M8]
AMK.player.queue.remove(trackId)    // [provisoire M8]
```

Le volume absolu n'est pas exposé en v1. Un élément qui veut changer le son règle un gain interne sur les échantillons
(section 5.4), jamais le curseur (PARITE, section 8.1).

### 5.4 Son [provisoire M4]

```js
AMK.audio.onSamples(fn)
// fn({ samples: Float32Array, channels, sampleRate, frames }), après décodage et avant la sortie.
// Lecture seule. Les échantillons ne sont pas copiés sur disque.

AMK.audio.setGainDb(db)
// Gain appliqué aux échantillons du morceau courant, borné par le plafond de la config du noyau.
// Ne touche jamais au curseur de volume. Modèle : le nivellement Atmos (extensions/atmos-leveler/).

AMK.audio.analyze(...)   // niveaux et spectre, calculés localement sur les échantillons décodés
```

### 5.5 Interface [provisoire M8]

Une extension décrit son interface de deux façons, qui donnent le même résultat :

- directement, par une arborescence de primitives ;
- avec `AMK.React`, un petit moteur qui accepte l'écriture React (hooks de base, fragments, props) et produit la même
  arborescence. Les composants internes de Spotify ne se reprennent pas : on utilise `AMK.components` à la place.

Dans les deux cas, le noyau rend l'arborescence en XAML.

Primitives prévues : `stack`, `row`, `text`, `button`, `toggle`, `slider`, `list`, `image`, `icon`.

```js
AMK.ui.topbar.addButton({ label, icon, onClick })
AMK.ui.playbar.addButton({ label, icon, onClick })
AMK.ui.menu.addItem("context" | "app", { label, onClick })
AMK.ui.popup.open(vue)
AMK.ui.panel.register(id, vue)
AMK.ui.title.set(texte)
AMK.ui.page.register(chemin, vue)     // pour une app
AMK.ui.navigate(chemin)
```

Exemple :

```js
AMK.ui.playbar.addButton({
  label: "Paroles",
  icon: AMK.icons.get("lyrics"),
  onClick: () => AMK.ui.popup.open(vueParoles),
});
```

Une extension ne peut pas injecter de XAML brut en v1. Elle évite qu'un élément de la communauté touche à des parties de
l'interface qu'on n'a pas relevées. Les thèmes sont le seul cas où du XAML est livré par un élément (section 6). Une
version suivante pourra accepter du XAML validé contre une liste de types et de propriétés autorisés (voir section 11).

### 5.6 Services [provisoire M7 et M8]

```js
AMK.notify({ title, text })                      // [provisoire M7] permission notifications
AMK.keyboard.bind("Ctrl+Alt+L", fn)              // [provisoire M7] permission ui.keyboard
AMK.http.get(url) -> Promise<{ status, text }>   // [ferme] permission network:<hôte>
AMK.uri.parse(texte) / AMK.uri.build(...)        // [ferme] liens music:// et https://music.apple.com
AMK.icons.get(nom) -> chaîne SVG                 // [provisoire M7]
AMK.color.fromArtwork(url) -> Promise<couleur>   // [provisoire M8]
AMK.library.playlists() / AMK.library.tracks()   // [provisoire M8] permission library.read
AMK.services.call(service, méthode, args)        // [provisoire M8] permission services.read, liste blanche
AMK.lyrics.current() -> Promise<paroles>         // [provisoire M8] permission player.read
AMK.audio.analyze() -> { level, spectrum, tempo } // [provisoire M4] permission audio.analyze
```

`AMK.http.get` ne sert qu'aux services tiers (Last.fm, ListenBrainz, fournisseurs de paroles). Il ne peut pas
atteindre les API internes d'Apple : le noyau refuse tout hôte qui ne figure pas dans les permissions du manifeste.
`AMK.services.call` ne crée pas de connexion nouvelle : il appelle les services que l'app utilise déjà, et seulement
ceux de la liste blanche publiée avec le kit.

## 6. Thèmes, apps et snippets

### 6.1 Thèmes [provisoire M8]

Un thème est un dossier avec `colors.ini`, `resources.xaml`, ou les deux, et éventuellement un `main.js`.

- `colors.ini` a une section par schéma (`[Base]`, `[Dark]`…). Chaque clé est une couleur hexadécimale sans `#`, comme
  dans Spicetify. Chaque clé remplace une brosse XAML du noyau. La liste des clés est relevée en M8.
- `resources.xaml` est un dictionnaire de ressources. Il ne peut définir que des clés figurant dans la liste autorisée
  publiée avec M8. Une clé hors liste fait échouer le thème, avec une ligne dans le journal.
- `main.js` suit les permissions du manifeste comme une extension.

### 6.2 Apps [provisoire M8]

Une app est une extension qui ajoute une page de navigation (`AMK.ui.page.register`), avec une icône SVG en deux
états (`icon`, et `active-icon` prévue). Son code ne tourne que lorsque la page est ouverte.

### 6.3 Snippets [provisoire M8]

Un snippet est un fragment de `resources.xaml`, activable seul. Il suit la même liste de clés autorisées que les thèmes.
Il peut aussi avoir un `user.css`, appliqué après le thème actif. Détails : [`docs/themes/FORMAT.md`](../themes/FORMAT.md),
section « Snippets ».

## 7. Ligne de commande `amk`

Le CLI remplace le CLI de Spicetify. Son écriture (C++ avec le noyau, ou autre) est décidée en M5.

| Commande | Rôle | Équivalent Spicetify | Jalon |
|---|---|---|---|
| `amk config [clé] [valeur]` | Lire ou écrire un réglage | `spicetify config` | M5 |
| `amk extension list` | Lister les extensions, leur état et leurs permissions | `config extensions` | M7 |
| `amk extension enable <id>` / `disable <id>` | Activer ou désactiver une extension | `config extensions <id>` et `-` | M7 |
| `amk install <id>` | Installer depuis le catalogue, SHA-256 vérifié | Marketplace | M7 |
| `amk theme set <id> [--scheme <nom>]` | Choisir un thème et un schéma | `config current_theme` | M8 |
| `amk watch` | Recharger le thème à chaque sauvegarde | `spicetify watch` | M8 |
| `amk launch` | Lancer Apple Music avec le noyau | `spicetify auto` | M3 |
| `amk restore` | Lancer Apple Music sans injection. Rien n'est à restaurer dans le paquet | `spicetify restore` | M6 |
| `amk path` | Afficher les chemins du kit et d'Apple Music | `spicetify path` | M5 |
| `amk update` | Mettre à jour le kit depuis une release | `spicetify upgrade` | Après M6 |

## 8. Journal et erreurs

- Chaque ligne du journal indique l'élément, par exemple `Extension hello-extension démarrée`.
- Une erreur dans `main.js` est journalisée avec son message, sans arrêter les autres éléments.
- Une fonction appelée sans permission lève une erreur `PermissionDenied`, avec le nom de la permission manquante.
- Les appels répétés à `AMK.log` ne sont pas limités en v1. Une limite pourra venir plus tard.

## 9. Versions

- `kitApi` est au format `MAJEUR.MINEUR`.
- Une version majeure change quand une fonction ferme est retirée ou modifiée de façon incompatible.
- Une version mineure ajoute des fonctions, sans rien casser.
- Une fonction retirée reste disponible pendant une version mineure, avec un avertissement dans le journal.
- Un élément déclare la version qu'il vise. Le noyau accepte toute version mineure de la même majeure.

## 10. Points provisoires

Ces points doivent être confirmés avant de passer au statut « ferme » :

- la taille de quickjs-ng et son comportement sous MinGW et MSVC ;
- le chemin des données de l'utilisateur (M3) ;
- la forme des données de `AMK.player` et des événements (M4) ;
- le format des échantillons transmis à `AMK.audio.onSamples` (M4) ;
- l'emplacement des éléments d'interface dans l'arbre XAML (M8) ;
- la liste des clés XAML autorisées pour les thèmes (M8) ;
- les limites de temps et de mémoire d'un élément (à fixer en M7, sur des mesures) ;
- les chemins de la section 8 de PARITE-SPICETIFY.md, qui dépendent de l'app réelle : la navigation, la barre
  latérale, les emplacements de la barre du haut et de la barre de lecture, la file d'attente, les hôtes de télémétrie.

## 11. Hors v1, avec leur chemin

Rien n'est écarté définitivement. Chaque point reste hors de la v1 pour une raison précise, et le chemin suivant est
décrit dans [PARITE-SPICETIFY.md](PARITE-SPICETIFY.md).

- **DLL natives de la communauté** : elles tourneraient avec tous les droits dans le processus d'Apple Music. Chemin :
  une phase suivante, avec une signature de l'éditeur et une relecture du code. Le catalogue les listerait à part, avec
  leur mode de confiance propre.
- **XAML brut dans les extensions** : chemin : du XAML validé par le noyau contre une liste publiée de types et de
  propriétés, avant rendu.
- **Volume absolu** : il suit le curseur sans jamais l'écrire. Le gain interne reste le seul moyen de changer le son
  (PARITE, section 8.1).
- **Services internes d'Apple** : liste blanche d'appels en lecture (PARITE, section 8.2). L'API publique MusicKit
  demande un compte Apple Developer : option à ouvrir plus tard.
- **Télémétrie** : option `telemetry.block`, désactivée par défaut, qui ne touche jamais aux hôtes de licence ou de DRM
  (PARITE, section 8.5).
- **Patchs par version** : table de motifs d'octets, sans adresse fixe, pour chaque version testée (PARITE, section 8.4).

## 12. État de la mise en œuvre (M7)

| Partie | État | Vérification |
|---|---|---|
| `version`, `kitApi`, `process`, `locale`, `log`, `onStop` | fait | tests C++ et Wine |
| `config.get/set` : une clé par élément, jamais `enabled` | fait. `get` rend un booléen, un nombre ou un texte selon la valeur par défaut | tests C++ |
| `storage.get/set/remove` : fichier JSON par élément, quota de 1 Mo | fait. Écriture par fichier temporaire puis remplacement. Un élément qui écrit doit déclarer un seul processus (`processes`) | tests C++ et Wine |
| `processes` : `ui`, `engine`, ou les deux | fait. Un élément d'un autre processus est ignoré, avec une ligne au journal | tests C++ |
| `player.state`, `on`, `play`, `pause`, `next`, `previous`, `seek` | fait côté runtime. Le noyau Windows ne les branche pas encore : `state` est vide, les commandes répondent « non possible » | hôte factice |
| `audio.setGainDb` : borné par `audio.maxGainDb` (6 dB par défaut, 12 au plus) et remis à 0 à l'arrêt | fait côté runtime. Le son n'est pas encore branché (M4) | tests C++ |
| `notify` (titre 120 caractères, texte 500) | fait. Sous Windows : bulle de notification (icône temporaire, 8 s), et une ligne au journal | tests C++ ; bulle à voir sur le PC |
| `keyboard.bind` (forme normalisée, touche F, média ou modificateur obligatoire) | fait. Sous Windows : `RegisterHotKey`, remis sur le fil JavaScript | tests C++ ; press simulée sous Wine |
| `http.get` (https, hôte déclaré, 1 Mo au plus) | fait. Sous Windows : WinHTTP, délai de 5 à 10 s, une erreur renvoie statut 0 au lieu de lever une exception | tests C++ ; échec testé sous Wine, succès à voir sur le PC |
| `uri.parse`, `uri.build`, `icons.get`, `setTimeout`, `setInterval`, `queueMicrotask`, `console` | fait | tests C++ |
| Permissions : `PermissionDenied` à l'exécution, refus du manifeste à la lecture | fait | tests C++ |
| Découverte des dossiers, activation, rechargement (`Restart`), isolement des pannes | fait | tests C++ et Wine |
| `audio.onSamples`, `audio.analyze` | à faire (M4) | |
| `ui.*`, `React`, `components`, `library`, `player.queue`, `services`, `lyrics`, `color` | à faire (M8) | |
| Thèmes et snippets | pas chargés par cette version du noyau | |

Limites connues, à corriger avant la version 1 : l'appel réseau est synchrone, donc il bloque le fil qui exécute
le JavaScript le temps de la réponse (jusqu'à 10 s). Un élément qui tourne dans les deux processus et écrit son stockage peut perdre
une mise à jour : c'est pourquoi `processes` existe (voir `QUESTIONS.md`).

