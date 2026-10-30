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
- **Le manifeste choisit les processus** avec `processes` (voir MANIFEST.md). Par défaut, un élément tourne seulement dans
  l'interface (`ui`, AppleMusic.exe). Un élément qui agit sur le son déclare `engine` (AMPLibraryAgent.exe).
  Choix provisoire, décidé avec l'utilisateur : un élément qui écrit son stockage doit déclarer un seul processus, sans quoi
  deux processus écrivent le même fichier. [provisoire M7]

### Organisation du code (pour les contributeurs)

L'objet global est fait de **modules** (`core`, `config`, `storage`, `player`, `audio`, `keyboard`, `notify`, `http`,
`icons`, `uri`, `color`, `timers`, `console`, `ui`, `link`, et `audioAnalysis` qui complète `audio`). Chaque module vit dans ses propres fichiers, pour que plusieurs personnes
`icons`, `uri`, `timers`, `console`, `ui`, `react`). Chaque module vit dans ses propres fichiers, pour que plusieurs personnes
`icons`, `uri`, `timers`, `console`, `ui`, `crypto`, `i18n`, `discord`, `tray`, `midi`, `remote`, `fs`). Chaque module vit dans ses propres fichiers, pour que plusieurs personnes
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

1. **Découverte.** Au démarrage du noyau, chaque sous-dossier de `extensions`, `apps` et `themes` est examiné. Un
   dossier sans `manifest.json` est ignoré, comme un dossier dont le nom commence par un point (travail en cours
   d'`amk`). Chaque type a son dossier : une app placée dans `extensions` (ou l'inverse) est refusée. Un snippet n'a
   pas de script : le noyau ne le charge pas.
2. **Validation.** Le manifeste doit respecter le schéma, `id` doit être le nom du dossier, `kitApi` doit être de
   la version majeure prise en charge, et les fichiers déclarés doivent exister. En cas d'échec, l'élément reçoit
   l'état `Failed` et une ligne est écrite dans le journal. Les autres éléments continuent.
3. **Activation.** Un élément ne démarre que s'il est activé dans les réglages (section 3.3). Un élément de la
   communauté est désactivé par défaut : le manifeste ne peut pas changer cette valeur.
4. **Démarrage.** Le noyau évalue `main` (pour une extension ou une app) dans son runtime, puis appelle les
   fonctions enregistrées. Une exception donne l'état `Failed`, sans arrêter les autres.
5. **Arrêt.** Les fonctions passées à `AMK.onStop` sont appelées dans l'ordre inverse du démarrage. Un arrêt qui échoue
   est journalisé et ignoré.
6. **Rechargement.** Une extension ou une app se recharge seule : arrêt, nouvelle lecture du dossier (manifeste et
   code), puis démarrage. Apple Music ne redémarre pas. `amk` le demande par `reload.txt` : `start`, `stop`, `restart`
   et `rescan` (nouveaux dossiers et dossiers retirés). Détails : `docs/cli/COMMANDES.md`, « Effet immédiat ».

Dans le noyau, une app porte l'identifiant `app:<id>` (journal, `reload.txt`), parce qu'une app et une extension
peuvent avoir le même identifiant : elles sont alors liées (section 6.2).

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

### 3.6 Plusieurs fichiers : modules ES [provisoire M7]

Un élément peut répartir son code en plusieurs fichiers, comme les `subfiles` de Spicetify. Avec `"type": "module"`
dans le manifeste, `main.js` est un module ES :

```js
// main.js
import { formater } from "./lib/texte.js";
import reglages from "./reglages.mjs";
AMK.log(formater(reglages.titre));
```

Règles du chargeur de modules (`core/js_runtime.cpp`, `ResolveModuleName` et `ReadModuleFile`) :

- seul un chemin relatif au fichier qui importe est accepté : `./x.js`, `./lib/x.js`, `../x.js` ;
- le chemin ne sort jamais du dossier de l'élément : `../` au-dessus du dossier est refusé ;
- pas de nom nu (`import "lodash"`), pas de chemin absolu (`/x.js`, `C:/x.js`), pas de `\`, pas de `:` ;
- les noms de fichiers et de dossiers sont faits de lettres, chiffres, `.`, `_` et `-`, sans point au début ;
- seuls les fichiers `.js` et `.mjs` s'importent, 1 Mo au plus chacun ;
- un lien symbolique (ou une jonction Windows) sur le chemin est refusé.

Un import refusé lève une `ReferenceError` qui commence par « import refusé ». L'élément passe alors à l'état `Failed`,
avec la raison dans le journal. Sans `"type": "module"`, `main.js` reste un script classique : il peut quand même
charger un module avec `import("./x.js")`, qui renvoie une `Promise`, selon les mêmes règles. Un module partage l'objet
`AMK` et les permissions de l'élément : importer un fichier ne donne aucun droit de plus.

## 4. Permissions

Un élément déclare les permissions dont il a besoin dans `manifest.json`. Le catalogue les affiche, et l'utilisateur
les voit avant d'activer l'élément. Une fonction protégée par une permission non déclarée lève une erreur, sans rien
faire.

| Permission | Ce qu'elle donne | Statut |
|---|---|---|
| (aucune) | `AMK.log`, `AMK.onStop`, `AMK.config`, `AMK.uri`, `AMK.locale` | [ferme] |
| (aucune) | `AMK.icons`, `AMK.i18n`, `AMK.crypto`, `AMK.remote` (réglage `remote.enabled`) | [provisoire M7] |
| `storage` | `AMK.storage`, un fichier JSON par élément, dans `storage\<id>.json` | [ferme] |
| `notifications` | `AMK.notify`, notification Windows | [provisoire M7] |
| `ui.keyboard` | `AMK.keyboard.bind`, raccourcis globaux ou limités à la fenêtre | [provisoire M7] |
| `player.read` | `AMK.player.state()` et les événements du lecteur | [provisoire M4] |
| `player.control` | `AMK.player.play()`, `pause()`, `next()`, `previous()`, `seek()` | [provisoire M4] |
| `audio.analyze` | `AMK.audio.onFrame`, `analyze`, `loudness` : spectre, niveaux, tempo et loudness du son décodé, calculés en mémoire | [provisoire M4] |
| `audio.gain` | `AMK.audio.setGainDb`, gain appliqué aux échantillons, borné par le plafond du noyau | [provisoire M4] |
| `audio.eq` | `AMK.audio.eq`, égaliseur à 10 bandes appliqué aux échantillons | [provisoire M4] |
| `ui.topbar`, `ui.playbar`, `ui.menu`, `ui.popup` | Ajout d'éléments d'interface à ces emplacements | [provisoire M8] |
| `ui.panel`, `ui.window`, `ui.tray` | Panneau latéral, fenêtre secondaire, icône de zone de notification | [provisoire M8] |
| `ui.page` | Pages d'app et entrées de navigation | [provisoire M8] |
| `library.read` | `AMK.library`, lecture de la bibliothèque et des playlists | [provisoire M8] |
| `library.write` | Écriture de playlists et de la file, après la lecture | [provisoire M8] |
| `services.read` | `AMK.services.call`, appels en lecture à une liste blanche de services de l'app | [provisoire M8] |
| `ipc.discord` | `AMK.discord`, tube local vers le client Discord, pour la présence (réglage `discord.enabled`) | [provisoire M7] |
| `midi.input` | `AMK.midi`, entrées d'un contrôleur MIDI (réglage `midi.enabled`) | [provisoire, après M7] |
| `ui.tray` | `AMK.tray`, entrées dans le menu de l'icône du kit (réglage `tray.enabled`) | [provisoire M7] |
| `fs.output` | `AMK.fs.writeText`, un fichier texte dans `output\<id>\` du kit, et nulle part ailleurs | [provisoire M7] |
| `network:<hôte>` | `AMK.http.get` et `AMK.http.post`, uniquement vers l'hôte déclaré | [ferme] pour le mécanisme |

Chaque chemin de [PARITE-SPICETIFY.md](PARITE-SPICETIFY.md) indique la permission qu'il demande.

Aucune fonction ne modifie le curseur de volume ni `AVCFPlayerSetVolume`. Le gain interne (`audio.gain`) agit sur les
échantillons seulement. Il n'existe pas de permission pour lire le disque, choisir un dossier, ni appeler du code
natif. Elles ne seront pas ajoutées. `fs.output` est la seule écriture permise : un fichier texte (`.txt`, `.json`,
`.csv`, `.html`, `.xml`, 256 Ko et 16 fichiers au plus) dans `%USERPROFILE%\AppleMusicModKit\output\<id>\`, pour
qu'un autre programme le lise (une source texte d'OBS). L'élément ne peut ni relire, ni effacer, ni sortir de ce dossier.

Les services du kit (présence Discord, icône de notification, télécommande locale, MIDI) sont tous **désactivés par
défaut**. Chacun demande un réglage du kit (`discord.enabled`, `tray.enabled`, `remote.enabled`, `midi.enabled`), que
seul l'utilisateur change, en plus de la permission de l'élément. Un élément ne peut pas les allumer.

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

Textes en français et en anglais [provisoire M7], sans permission :

```js
AMK.i18n.language                    // "fr" ou "en" : langue du kit, réglage language = fr, en ou auto (auto suit Windows)
AMK.i18n.define({ fr: { salut: "Bonjour {nom}" }, en: { salut: "Hello {nom}" } })
AMK.i18n.t("salut", { nom: "Léa" })  // texte dans la langue du kit, sinon en français, sinon la clé
AMK.i18n.t("kit.tray.next")          // textes du kit lui-même, préfixe kit.
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

Le volume absolu se lit avec `AMK.audio.volume()` (section 5.4), sans jamais l'écrire. Un élément qui veut changer le
son règle un gain interne sur les échantillons (section 5.4), jamais le curseur (PARITE, section 8.1).

### 5.4 Son [provisoire M4]

```js
AMK.audio.setGainDb(db)
// Gain appliqué aux échantillons du morceau courant, borné par le plafond de la config du noyau.
// Ne touche jamais au curseur de volume. Modèle : le nivellement Atmos (extensions/atmos-leveler/).

AMK.audio.onFrame(fn)                        // permission audio.analyze. Renvoie false sans son dans ce processus
// fn({ bands, rms, peak, time }), environ 20 fois par seconde au plus. bands : 16 bandes de 40 Hz à 16 kHz
// (espacées en log), chacune de 0 (-80 dBFS ou moins) à 1 (0 dBFS). rms et peak : niveau linéaire, 1 = pleine
// échelle. time : secondes depuis le début de l'analyse. Un script lent reçoit la dernière image, sans file d'attente.

AMK.audio.analyze()                          // permission audio.analyze
// -> { tempo, confidence, loudness: { momentary, shortTerm }, rms, peak, bands, frequencies }
// tempo en BPM (null pendant les premières secondes), confidence de 0 à 1, frequencies : centre de chaque bande en Hz.

AMK.audio.loudness()                         // permission audio.analyze
// -> { momentary, shortTerm } en LUFS (BS.1770 : 400 ms et 3 s), null si rien ne joue. Mesurée avant le gain du
// noyau et l'égaliseur, comme si le curseur était au maximum.

AMK.audio.volume()                           // permission player.read
// -> volume du lecteur, de 0 à 1, tel que le curseur l'affiche. Lecture seule. null tant qu'il n'a pas été vu.

AMK.audio.eq.setBand(index, gainDb)          // permission audio.eq. 10 bandes, de 31 Hz à 16 kHz, +/- 12 dB
AMK.audio.eq.preset(nom)                     // "flat", "rock", "jazz", "bass-booster"... (liste : presets())
AMK.audio.eq.reset()                         // revient à plat
AMK.audio.eq.bands()                         // -> [{ frequency, gainDb, q, type }]
AMK.audio.eq.presets()                       // -> noms des préréglages
```

Le son est analysé et égalisé dans `AMPLibraryAgent.exe` (élément avec `"processes": ["engine"]`). Dans l'autre
processus, ces fonctions ne lèvent pas d'erreur : `onFrame` renvoie `false`, les lectures valent `null` et l'égaliseur
répond `false`.

- **Où :** sur les échantillons que les hooks du noyau traitent déjà (flux stéréo `IAudioRenderClient` et objets
  spatiaux Dolby Atmos), jamais sur le tap audio d'Apple, qu'Apple saute pour l'Atmos
  (`knowledge/atmos-loudness.md`). Ces hooks sont ceux du nivellement Atmos : il doit être activé.
- **Ordre :** gain du noyau, puis égaliseur, puis genou doux et plafond. Un égaliseur à plat ne touche aucun
  échantillon (sortie identique bit à bit). Un changement de bande est lissé (12 dB en 0,1 s), sans clic.
- **Une seule égalisation par processus.** Le dernier élément qui la règle l'emporte, et elle revient à plat quand
  cet élément s'arrête.
- **Ce qui ne sort pas :** les échantillons bruts. Le script ne reçoit que de petites images de nombres. Rien n'est
  écrit sur disque.

### 5.5 Interface [provisoire M8]

Une extension décrit son interface de deux façons, qui donnent le même résultat :

- directement, par une arborescence de primitives ;
- avec `AMK.React`, un petit moteur qui accepte l'écriture React (hooks de base, fragments, props) et produit la même
  arborescence. Les composants internes de Spotify ne se reprennent pas : on utilise `AMK.components` à la place.

Dans les deux cas, le noyau rend l'arborescence en XAML.

Primitives : `stack`, `row`, `list` et `card` (qui ont des enfants), `text`, `button`, `toggle`, `slider`,
`textfield`, `image`, `icon`, `dropdown` (ses enfants sont des `option`) et `option` (seulement dans un `dropdown`).
Chaque nœud est un objet `{ type, ...attributs, children }`. Les attributs sont des valeurs simples (texte, nombre,
booléen), jamais des objets ni des listes. Le noyau vérifie l'arbre (`core/ui_model.cpp`) : 8 niveaux, 300 nœuds et
500 caractères par texte au plus.

| Primitive | Attributs lus par le rendu |
|---|---|
| `stack`, `row`, `list` | `gap` (nombre), `children` |
| `card` | `title`, `subtitle`, `onClick`, `children` |
| `text` | `text`, `variant` (`title`, `subtitle`, `body`, `caption`) |
| `button` | `label`, `icon` (SVG), `disabled`, `variant` (`primary`, `secondary`, `tertiary`), `onClick` |
| `toggle` | `label`, `checked`, `disabled`, `onChange` |
| `slider` | `label`, `min`, `max`, `step`, `value`, `disabled`, `onChange` |
| `textfield` | `label`, `value`, `placeholder`, `disabled`, `onChange` |
| `dropdown` | `label`, `value`, `disabled`, `onChange`, `children` (des `option`) |
| `option` | `value`, `label` |
| `image` | `src`, `label` (texte de remplacement), `onClick` |
| `icon` | `icon` (SVG, comme `AMK.icons.get`), `label`, `onClick` |
| toutes | `tooltip` (texte), `tooltipPlacement` (`top`, `bottom`, `left`, `right`), `tooltipDelay` (0 à 10000 ms) |

`onClick` et `onChange` contiennent un numéro de rappel (entier positif), donné par le noyau. Le noyau refuse
`onClick` hors de `button`, `card`, `image` et `icon`, et `onChange` hors de `toggle`, `slider`, `textfield` et
`dropdown`. L'hôte signale un clic avec `ScriptExtension::DeliverClick(numéro)` et une nouvelle valeur avec
`DeliverEvent("change", {"id": numéro, "value": valeur})`. `AMK.React` (section 5.9) remplit ces numéros.

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
AMK.http.post(url, corps, { headers }) -> Promise<{ status, text }>  // [provisoire M7] mêmes règles que get
AMK.crypto.md5(texte) -> chaîne hexadécimale     // [provisoire M7] pour les services qui signent ainsi (Last.fm)
AMK.uri.parse(texte) / AMK.uri.build(...)        // [ferme] liens music:// et https://music.apple.com
AMK.icons.get(nom) -> chaîne SVG                 // [provisoire M7]
AMK.color.extract(pixels, largeur, hauteur)      // [provisoire M8] aucune permission
// pixels : RVBA, ligne par ligne (tableau, Uint8Array ou ArrayBuffer). Renvoie les clés de colorExtractor de
// Spicetify : PROMINENT, VIBRANT, DARK_VIBRANT, LIGHT_VIBRANT, DESATURATED, VIBRANT_NON_ALARMING ("#rrggbb"),
// et palette : [{ color, population }]. Les images par adresse ne sont pas encore lues.
AMK.color.fromArtwork(url) -> Promise<couleur>   // [provisoire M8]
AMK.library.playlists() / AMK.library.tracks()   // [provisoire M8] permission library.read
AMK.services.call(service, méthode, args)        // [provisoire M8] permission services.read, liste blanche
AMK.lyrics.current() -> Promise<paroles>         // [provisoire M8] permission player.read
```

`AMK.http.get` ne sert qu'aux services tiers (Last.fm, ListenBrainz, fournisseurs de paroles). Il ne peut pas
atteindre les API internes d'Apple : le noyau refuse tout hôte qui ne figure pas dans les permissions du manifeste.
`AMK.http.post` suit les mêmes règles (https, hôte déclaré). Un corps texte part en formulaire
(`application/x-www-form-urlencoded`), un objet part en JSON. Seuls les en-têtes `content-type`, `authorization` et
`accept` sont acceptés, sans retour à la ligne. Corps de 64 Ko au plus.

### 5.7 Services locaux [provisoire M7]

Chaque service est éteint tant que l'utilisateur n'a pas mis son réglage à 1 (`amk config discord.enabled 1`...).
Éteint, ou absent de l'hôte, un appel renvoie `false` et écrit une ligne au journal, sans lever d'erreur.

```js
// Présence Discord, permission ipc.discord, réglage discord.enabled
AMK.discord.setActivity({ clientId, details, state, largeImage, largeText, smallImage, smallText, startMs, endMs })
AMK.discord.clear()

// Menu de l'icône du kit dans la zone de notification, permission ui.tray, réglage tray.enabled (AppleMusic.exe)
AMK.tray.setMenu([{ label, onClick, checked, enabled }, "-"])   // "-" : séparateur, 20 entrées au plus
AMK.tray.onClick(fn)                                            // clic gauche sur l'icône

// Contrôleur MIDI, permission midi.input, réglage midi.enabled (AMPLibraryAgent.exe)
AMK.midi.on("note:60" | "cc:7" | "ch2:note:36" | "note:*" | "*", fn)   // fn({ type, channel, number, value })
AMK.midi.devices() -> ["nom", ...]

// Télécommande locale, réglage remote.enabled (AMPLibraryAgent.exe), sans permission
AMK.remote.setState(objet)          // ajouté à GET /status sous extensions.<id>, 4 Ko au plus

// Fichier de sortie, permission fs.output
AMK.fs.writeText("now-playing.txt", texte)   // -> output\<id>\now-playing.txt, remplacé d'un coup
```

`clientId` est l'identifiant d'une application créée sur le portail des développeurs Discord (17 à 20 chiffres).
Le dernier élément qui appelle `setActivity` l'emporte, et sa présence disparaît quand il s'arrête.

**Télécommande locale.** Serveur HTTP lié à `127.0.0.1` seulement, sur le port `remote.port` (8974 par défaut). Le
jeton `remote.token` est créé quand on met `remote.enabled` à 1 (`amk config remote.token` l'affiche). Il va dans
l'en-tête `X-AMK-Token` (ou `Authorization: Bearer`), jamais dans l'adresse. Une requête venue d'une page web (en-tête
`Origin`, préflight `OPTIONS`) ou avec un autre `Host` que `127.0.0.1` ou `localhost` est refusée.

| Requête | Effet |
|---|---|
| `GET /status` | `{ playing, positionMs, durationMs, track, gainDb, extensions }` |
| `POST /play`, `/pause`, `/next`, `/previous` | commande du lecteur, `409` si l'app ne peut pas maintenant (sous Windows, `play` et `pause` attendent l'état du lecteur, M4) |
| `POST /volume?db=-6` (ou le corps `{"db": -6}`) | gain interne sur les échantillons, borné par `audio.maxGainDb`. Le curseur de volume ne bouge pas |

```sh
curl -H "X-AMK-Token: <jeton>" http://127.0.0.1:8974/status
curl -X POST -H "X-AMK-Token: <jeton>" http://127.0.0.1:8974/next
```

**MIDI.** `midi.device` choisit l'appareil (0 pour le premier). `midi.map` relie des touches à des commandes du kit,
sans élément : `note:60=next;note:62=previous;cc:20=play;cc:7=gain` (`gain` : un potentiomètre de -30 à 0 dB). Pas de
retour lumineux vers le contrôleur.
`AMK.services.call` ne crée pas de connexion nouvelle : il appelle les services que l'app utilise déjà, et seulement
ceux de la liste blanche publiée avec le kit.

### 5.8 Lien entre une app et son extension [provisoire M7]

Une app et une extension qui ont le même identifiant sont **liées** (les `subfiles_extension` de Spicetify). Les deux
tournent dans le même processus, chacune dans son moteur. Elles s'échangent des messages JSON :

```js
// extensions/paroles/main.js : tourne au démarrage
AMK.link.on((message) => {
  if (message.type === "demande") AMK.link.post({ type: "paroles", texte: "..." });
});

// apps/paroles/main.js : la page
AMK.link.on((message) => AMK.log("reçu : " + message.texte));
AMK.link.post({ type: "demande" });
```

```js
AMK.link.post(message)   // envoie une valeur JSON (64 Ko au plus) ; true si l'autre élément tourne
AMK.link.on(fn)          // fn(message) reçoit chaque message de l'autre ; renvoie une fonction qui retire fn
AMK.link.connected()     // true si l'autre élément tourne
```

- Un message est copié en JSON : une fonction ou `undefined` lève une `TypeError`, un message de plus de 64 Ko une
  `RangeError`.
- Il arrive au tour suivant du fil JavaScript, jamais pendant l'appel à `post`. L'ordre des messages est gardé.
- Si l'autre élément est arrêté ou en train de démarrer, `post` renvoie `false` et le message est perdu. Le plus simple :
  celui qui démarre en second envoie un premier message, et l'autre lui répond.
- Aucune permission n'est demandée : rien ne sort du processus, et seuls les deux éléments du même identifiant se
  parlent. Un script de thème n'est lié à rien.
- L'app et l'extension partagent le fichier `storage\<id>.json` (même identifiant). Une seule des deux doit écrire
  son stockage : l'autre passe par `AMK.link`.
### 5.9 React, composants et `AMK.ready` [provisoire M8]

Spicetify donne React aux extensions. Une app native ne peut pas faire tourner le vrai React : le kit fournit un petit
moteur qui accepte la même écriture, sans JSX, et qui produit l'arbre de primitives de la section 5.5. Il n'a pas de
permission propre : la cible demande celle de son emplacement (`ui.panel`, `ui.page` ou `ui.popup`).

```js
const { h, useState } = AMK.React;

function Compteur() {
  const [n, setN] = useState(0);
  return h("stack", null,
    h("text", null, "Clics : " + n),
    h("button", { label: "Plus un", onClick: () => setN(n + 1) }));
}

AMK.ReactDOM.render(h(Compteur), "panel:compteur");
AMK.ready.then(() => AMK.log("prêt"));
```

```js
AMK.React.createElement(type, props, ...enfants)   // aussi AMK.React.h
AMK.React.Fragment
AMK.React.isValidElement(valeur)
AMK.React.cloneElement(élément, props, ...enfants)
AMK.React.Children.toArray(enfants)               // aussi map, forEach, count, only
AMK.React.createContext(défaut)                    // -> { Provider, Consumer }
AMK.React.memo(composant)                          // rend le composant tel quel
AMK.React.useState(initial)                        // aussi useReducer, useEffect, useLayoutEffect,
AMK.React.useContext(contexte)                     //   useRef, useMemo, useCallback
AMK.hooks.usePlayer()                              // état du lecteur, suivi par ses événements [permission player.read]
AMK.ReactDOM.render(élément, cible)
AMK.ReactDOM.createRoot(cible)                     // -> { render(élément), unmount() }
AMK.ReactDOM.unmountComponentAtNode(cible)
AMK.ready                                          // Promise résolue quand le noyau a démarré l'élément
```

`AMK.hooks` reprend les hooks de `AMK.React`, plus `usePlayer`.

Règles :

- **Types d'éléments** : un nom de primitive (`"stack"`, `"text"`…), une fonction composant, `Fragment`, ou le
  `Provider` ou le `Consumer` d'un contexte. Quelques noms HTML sont acceptés : `div` et `section` donnent `stack`,
  `span`, `p` et `label` donnent `text`, `h1` un `text` de variante `title`, `h2` et `h3` la variante `subtitle`,
  `img` donne `image`, `select` donne `dropdown`, `ul` donne `list`. Pas de classes (`React.Component`). Pas de JSX
  dans le noyau : un outil peut compiler le JSX vers `AMK.React.createElement`.
- **Props** : un texte, un nombre ou un booléen devient un attribut. `style`, les objets, les listes et les autres
  fonctions sont ignorés.
- **Enfants texte** : dans un conteneur, un texte devient une primitive `text`. Dans `text`, `button`, `toggle` et
  `option`, les textes sont joints et vont dans l'attribut `text` ou `label`.
- **Événements** : `onClick` et `onChange` deviennent un numéro de rappel, qui ne change pas d'un rendu à l'autre. Le
  gestionnaire reçoit `{ type: "click", id }` ou `{ type: "change", id, value, target: { value, checked } }`.
- **Rendu** : un changement d'état rend toute la cible une seule fois, à la fin de l'appel en cours du noyau (clic,
  minuterie, événement). Plusieurs `setState` dans un même gestionnaire donnent un seul rendu. L'arbre n'est renvoyé
  au noyau que s'il a changé. Plus de 50 rendus à la suite sont arrêtés, avec une ligne au journal.
- **Effets** : ils tournent après l'envoi de l'arbre, ceux des enfants avant ceux des parents. Leur nettoyage tourne
  avant l'effet suivant, au démontage du composant, et à l'arrêt de l'élément.
- **Cibles** : `"panel:<nom>"`, `"page:/<chemin>"` (pour une app), `"popup:<nom>"`, ou `"popup"` pour
  `"popup:main"`. Les objets `{ panel }`, `{ page }` et `{ popup }` marchent aussi. Un popup nommé est mis à jour sur
  place à chaque rendu, et `unmount` le ferme. Démonter un panneau ou une page y laisse une pile vide.
- **Erreurs** : une erreur dans `ReactDOM.render` est levée, donc `main.js` échoue avec un message clair. Une erreur
  dans un rendu suivant est écrite au journal, et l'arbre précédent reste affiché.
- **Apps** : une app (`kind: app`) qui définit une fonction globale `render()`, comme une app Spicetify, voit ce
  qu'elle rend affiché sur la page `/<id>` au démarrage, avant que `AMK.ready` soit résolue (permission `ui.page`).
- **`AMK.ready`** : résolue au premier tour de la boucle du noyau après `main.js`. L'élément est alors démarré, et ses
  clics et événements arrivent. C'est l'équivalent de l'attente de `Spicetify.Player` ou de `Spicetify.Platform`.

Composants de `AMK.components`, à la place de `Spicetify.ReactComponent` :

| Composant | Props | Primitive |
|---|---|---|
| `Button` (et `ButtonPrimary`, `ButtonSecondary`, `ButtonTertiary`) | `label` (ou un texte en enfant), `icon`, `disabled`, `variant`, `onClick` | `button` |
| `Toggle` | `label`, `value` ou `checked`, `disabled`, `onChange(coché)` | `toggle` |
| `Slider` | `label`, `min`, `max`, `step`, `value`, `disabled`, `onChange(nombre)` | `slider` |
| `TextField` | `label`, `value`, `placeholder`, `disabled`, `onChange(texte)` | `textfield` |
| `Dropdown` | `label`, `value`, `options` (textes ou `{ value, label }`), `disabled`, `onChange(valeur)` | `dropdown` et `option` |
| `Card` | `title`, `subtitle`, `onClick`, enfants | `card` |
| `List` | enfants | `list` |
| `Text` (et `TextComponent`) | `text` ou enfants, `variant` | `text` |
| `Tooltip` (et `TooltipWrapper`) | `label` ou `content`, `placement`, `delay` ou `showDelay`, enfants | ajoute `tooltip`, `tooltipPlacement` et `tooltipDelay` à chaque enfant |

Chaque composant accepte aussi `tooltip`, `tooltipPlacement` et `tooltipDelay`. `Tooltip` entoure une primitive ou un
composant de `AMK.components`. Un composant écrit par l'extension doit passer ces trois props à sa primitive. Le
second argument des `onChange` est l'événement complet. Exemple complet : `examples/react-panel/`.

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
états (`icon`, et `active-icon` prévue). Elle vit dans `apps\<id>` et s'active avec `apps.<id>.enabled`
(`amk app enable <id>`). Dans cette version, son code tourne dès qu'elle est activée, comme une extension : l'entrée
de la barre latérale et l'ouverture de la page viennent avec M8. Une app peut avoir une extension du même identifiant,
qui tourne au démarrage : les deux se parlent par `AMK.link` (section 5.8). Son code peut être réparti en modules
(section 3.6).

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
- la forme des images de `AMK.audio.onFrame` et le nombre de bandes (M4, à confirmer sur le PC) ;
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
| `storage.get/set/remove` : fichier JSON par élément, quota de 1 Mo | fait. Écriture par fichier temporaire puis remplacement. Par défaut, un élément ne tourne que dans l'interface : une seule écriture à la fois | tests C++ et Wine |
| `processes` : `ui`, `engine`, ou les deux | fait. Un élément d'un autre processus est ignoré, avec une ligne au journal | tests C++ |
| `player.state`, `on`, `play`, `pause`, `next`, `previous`, `seek` | fait côté runtime. Le noyau Windows ne les branche pas encore : `state` est vide, les commandes répondent « non possible » | hôte factice |
| `audio.setGainDb` : borné par `audio.maxGainDb` (6 dB par défaut, 12 au plus) et remis à 0 à l'arrêt | fait côté runtime. Le son n'est pas encore branché (M4) | tests C++ |
| `notify` (titre 120 caractères, texte 500) | fait. Sous Windows : bulle de notification (icône temporaire, 8 s), et une ligne au journal | tests C++ ; bulle à voir sur le PC |
| `keyboard.bind` (forme normalisée, touche F, média ou modificateur obligatoire) | fait. Sous Windows : `RegisterHotKey`, remis sur le fil JavaScript | tests C++ ; press simulée sous Wine |
| `http.get` (https, hôte déclaré, 1 Mo au plus) | fait. Sous Windows : WinHTTP, délai de 5 à 10 s, une erreur renvoie statut 0 au lieu de lever une exception | tests C++ ; échec testé sous Wine, succès à voir sur le PC |
| `uri.parse`, `uri.build`, `icons.get`, `setTimeout`, `setInterval`, `queueMicrotask`, `console` | fait | tests C++ |
| Modules ES (`"type": "module"`, `import` relatif limité au dossier de l'élément, `import()`) | fait | tests C++, `tools/test_validate.py` |
| `link.post`, `link.on`, `link.connected` : app et extension du même identifiant | fait | tests C++ |
| Permissions : `PermissionDenied` à l'exécution, refus du manifeste à la lecture | fait | tests C++ |
| Découverte des dossiers (`extensions`, `apps`, scripts de `themes`), activation, isolement des pannes | fait | tests C++ et Wine |
| Effet immédiat par `reload.txt` : `start`, `stop`, `restart` (manifeste relu), `rescan` | fait côté noyau. Pas encore vu dans Apple Music | tests C++, tests de la CLI |
| `audio.onFrame`, `analyze`, `loudness`, `volume`, `eq` | fait. Sous Windows : branchés sur les hooks du nivellement Atmos (flux stéréo et objets spatiaux), qui doit être activé | tests C++ (signaux synthétiques) ; égaliseur et analyse vus sur le flux stéréo sous Wine ; écoute à faire sur le PC |
| `color.extract` (pixels fournis par le script) | fait | tests C++ |
| `ui.*`, `React`, `components`, `library`, `player.queue`, `services`, `lyrics`, `color.fromArtwork` | à faire (M8) | |
| Découverte des dossiers, activation, rechargement (`Restart`), isolement des pannes | fait | tests C++ et Wine |
| `http.post`, `crypto.md5` | fait. Sous Windows : WinHTTP, mêmes délais et limites que `get` | tests C++ ; échec testé sous Wine, succès à voir sur le PC |
| `i18n` (`language`, `define`, `t`) et textes du kit en français et en anglais (aide et messages principaux de `amk`) | fait. Réglage `language` : `fr`, `en`, `auto` | tests C++ et `tools/test_cli.py` |
| `discord.setActivity`, `clear` | fait. Sous Windows : tube `\\.\pipe\discord-ipc-0` à `9`, sur un fil à part, nouvel essai toutes les 15 s | tests C++ ; faux serveur Discord sous Wine ; vrai Discord à voir sur le PC |
| `tray.setMenu`, `onClick` | fait. Sous Windows : `Shell_NotifyIcon`, fenêtre message sur son propre fil, dans AppleMusic.exe | tests C++ ; Wine refuse l'icône (test sauté) ; à voir sur le PC |
| `midi.on`, `devices`, et `midi.map` | fait. Sous Windows : `midiInOpen` (winmm), dans AMPLibraryAgent.exe | tests C++ ; liaisons testées sous Wine sans appareil ; vrai contrôleur à voir sur le PC |
| `remote.setState` et la télécommande locale | fait. Sous Windows : Winsock sur `127.0.0.1` seulement, dans AMPLibraryAgent.exe | tests C++ ; client local et refus des autres adresses sous Wine |
| `fs.writeText` | fait. `output\<id>\` du kit seulement | tests C++ et Wine |
| `audio.onSamples`, `audio.analyze` | à faire (M4) | |
| `React`, `ReactDOM`, `hooks`, `components`, `ready` | fait côté runtime : l'arbre de primitives est produit et vérifié, les clics et changements reviennent par leurs numéros. Le rendu XAML viendra avec M8 | tests C++ (`tests/react_tests.cpp`) ; `amk test examples/react-panel` |
| `ui.*`, `library`, `player.queue`, `services`, `lyrics`, `color` | à faire (M8) | |
| Thèmes et snippets | pas chargés par cette version du noyau | |

Limites connues, à corriger avant la version 1 : l'appel réseau est synchrone, donc il bloque le fil qui exécute
le JavaScript le temps de la réponse (jusqu'à 10 s). Un élément qui tourne dans les deux processus et écrit son stockage peut perdre
une mise à jour : c'est pourquoi `processes` existe (voir `QUESTIONS.md`).

