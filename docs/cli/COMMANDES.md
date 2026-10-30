# Ligne de commande `amk`

`amk` est l'outil de ligne de commande du kit. Il fait pour Apple Music ce que `spicetify` fait pour Spotify :
il lance l'app avec le noyau, gère les extensions, les thèmes et les snippets, installe depuis le catalogue,
et sait revenir en arrière. Il ne modifie jamais le paquet d'Apple Music : tout se passe dans
`%USERPROFILE%\AppleMusicModKit`.

Le code est dans [cli/](../../cli/). La parité ligne par ligne avec Spicetify est dans
[PARITE-SPICETIFY.md](../extensions/PARITE-SPICETIFY.md), sections 1, 6 et 7.

## Démarrer

Une fois `amk.exe` à côté de `amk_launcher.exe` :

```
amk path                          où sont les fichiers
amk market                        ce qui est disponible dans le catalogue
amk install demo                  installe (demande confirmation, affiche les permissions)
amk extension enable demo         l'active
amk launch                        applique et lance Apple Music avec le noyau
```

Ouvrir un terminal dans le dossier du kit, ou taper `amk` avec le chemin complet.

## Commandes

Les colonnes « Spicetify » donnent l'équivalent. « Statut » dit ce qui est fait dans cette version.

### Lancement et réglages

| Commande | Rôle | Spicetify | Statut |
|---|---|---|---|
| `amk launch` (ou `auto`) | Écrit ce qui doit se charger, puis lance `amk_launcher.exe` | `auto` | Fait. Sur Linux, `--dry-run` affiche la commande |
| `amk restore` | Lance Apple Music sans noyau. Rien n'est à remettre dans le paquet | `restore` | Fait |
| `amk restore --backup <nom>` | Remet les réglages et les éléments d'une sauvegarde | `restore` (variante) | Fait |
| `amk apply` | Écrit `active.txt` et `reload.txt`, affiche ce qui se charge et ce qui ne se charge pas | `apply` | Fait. Le noyau relit ces fichiers à partir de M7 |
| `amk watch [--interval ms]` | Applique à chaque modification d'un thème, d'une extension ou des réglages | `watch` | Fait, sans rechargement XAML (M8) |
| `amk config` | Affiche tous les réglages | `config` | Fait |
| `amk config <clé>` | Affiche un réglage | `config <clé>` | Fait |
| `amk config <clé> <valeur>` | Écrit un réglage, valeur vérifiée | `config <clé> <valeur>` | Fait |
| `amk config extensions +id` / `-id` | Active ou désactive un élément | `config extensions +id` | Fait (aussi `apps`, `snippets`) |
| `amk path` | Affiche les dossiers | `path` | Fait |
| `amk backup` / `amk backup list` | Sauvegarde les réglages et les éléments | `backup` | Fait |
| `amk version`, `amk help` | | `--version`, `help` | Fait |

### Éléments : extensions, snippets, apps, thèmes

| Commande | Rôle | Statut |
|---|---|---|
| `amk list` | Tous les éléments installés, leur type, leur version et leur état | Fait |
| `amk extension list` / `enable <id>` / `disable <id>` | Extensions. Le nivellement Atmos (`atmos-leveler`) est intégré et s'active de la même façon | Fait |
| `amk snippet enable <id>` / `disable <id>` | Snippets (fragments XAML) | Fait |
| `amk app enable <id>` / `disable <id>` | Apps (pages d'interface) | Fait. Le chargement de la page vient avec M8 |
| `amk theme list` | Thèmes installés | Fait |
| `amk theme set <id> [--scheme <nom>]` | Choisit le thème et son schéma | Fait |
| `amk theme clear` | Aucun thème | Fait |
| `amk new <type> <id>` | Crée un élément de départ (extension, theme, snippet ou app) dans le dossier courant | Fait |
| `amk validate <dossier>` | Vérifie le manifeste et les fichiers d'un élément, avec les mêmes règles que `tools/validate.py` | Fait |
| `amk validate <catalogue.json>` | Vérifie un catalogue | Fait |

### Marketplace

| Commande | Rôle | Statut |
|---|---|---|
| `amk market` (ou `catalog`) | Liste le catalogue et ce qui est déjà installé | Fait |
| `amk install <id> [--yes]` | Télécharge, vérifie le SHA-256, valide, installe. Désactivé par défaut | Fait |
| `amk update <id> [--yes]` | Met à jour un élément depuis le catalogue, si la version est plus récente | Fait |
| `amk remove <id>` | Retire un élément. Une sauvegarde est faite avant | Fait |
| `amk update` | Vérifie la dernière release du kit, télécharge, contrôle le SHA-256 | Fait |
| `amk update --check` | Dit seulement si une version plus récente existe | Fait |
| `amk update --apply` | Installe la version téléchargée. Apple Music doit être fermé | Fait |

### Pas encore dans cette version

| Commande prévue | Ce qui la remplace aujourd'hui | Jalon |
|---|---|---|
| `amk inspect` avec l'arbre d'interface (`AMK.React`) | Rien | M8 |

## Options

| Option | Effet |
|---|---|
| `--kit-dir <dossier>` | Dossier du kit. Par défaut `%USERPROFILE%\AppleMusicModKit` |
| `--catalog <source>` | Catalogue : fichier local, `file://` ou `https://`. Par défaut le catalogue du dépôt |
| `--dry-run` | Affiche la commande qui serait lancée, sans la lancer |
| `--yes`, `-y` | Répond oui aux questions (installation, mise à jour) |
| `--check` | Avec `update` : vérifie seulement |
| `--apply` | Avec `update` : installe la mise à jour téléchargée |
| `--release-url <adresse>` | Avec `update` : autre adresse de release (tests, miroir) |
| `--install-dir <dossier>` | Avec `update --apply` : dossier où copier les fichiers. Par défaut, le dossier de `amk.exe` |
| `--scheme <nom>` | Avec `theme set` : schéma de couleurs |
| `--backup <nom>` | Avec `restore` : sauvegarde à remettre |
| `--interval <ms>`, `--max-cycles <n>` | Avec `watch` : période de surveillance, et nombre de passages avant l'arrêt |

Codes de retour : `0` réussi, `1` échec, `2` mauvais usage. `apply` rend `1` si un élément activé ne peut pas
se charger. Les messages d'erreur vont sur la sortie d'erreur, précédés de `amk :`.

## Fichiers du kit

```
%USERPROFILE%\AppleMusicModKit\
  settings.txt              réglages, une clé = valeur par ligne (format de core/settings.*)
  extensions\<id>\          manifest.json, main.js, ...
  themes\<id>\              manifest.json, colors.ini, resources.xaml
  apps\<id>\                manifest.json, main.js, icon.svg
  snippets\<id>\            manifest.json, resources.xaml
  active.txt                ce qui doit se charger : « extension demo 1.0.0 », « theme night 2.0.0 scheme=Dark »
  reload.txt                compteur écrit à chaque apply, et lignes « restart <id> » écrites par dev (lues par le noyau)
  backups\<aaaammjj-hhmmss>\  copie de settings.txt et des quatre dossiers d'éléments
  downloads\, updates\, cache\  fichiers temporaires et dernier catalogue lu
```

Réglages reconnus par `amk config` :

| Clé | Valeur | Défaut |
|---|---|---|
| `extensions.<id>.enabled`, `apps.<id>.enabled`, `snippets.<id>.enabled` | `1` ou `0` | `0` (un élément de la communauté est désactivé) |
| `theme.active` | identifiant d'un thème, vide pour aucun | vide |
| `theme.scheme` | nom d'une section de `colors.ini` | défaut du thème |
| `theme.parts.colors`, `theme.parts.resources`, `theme.parts.script` | `1` ou `0` | `1` |
| `catalog.url` | `https://…` ou `file://…` | catalogue du dépôt |

Un élément est dans `extensions\<id>` seulement si son nom de dossier est son identifiant. Les dossiers qui
commencent par un point sont des travaux en cours et sont ignorés.

## Installer : ce qui est vérifié, dans cet ordre

1. L'identifiant est valide, et il figure dans le catalogue.
2. L'adresse est en `https://`. Un catalogue distant ne peut pas pointer vers un fichier local.
3. Le nom, l'auteur, la description et les permissions sont affichés. Sans `--yes`, la réponse doit être « o ».
4. L'archive est téléchargée. Son SHA-256 doit être celui du catalogue, sinon elle est jetée.
5. La liste des fichiers de l'archive est contrôlée : pas de chemin absolu, pas de `..`, pas de `\`, pas de
   caractère de contrôle. Une seule entrée douteuse fait tout refuser.
6. L'archive est extraite dans un dossier de travail. Un lien symbolique dans le résultat fait tout refuser.
7. Le manifeste est validé : identifiant, type et version doivent correspondre au catalogue.
8. Seulement alors, le dossier est mis en place. L'ancienne version reste en place jusqu'à la réussite.

Rien de téléchargé n'est exécuté pendant l'installation. Un élément installé est désactivé ; il faut l'activer
avec `amk extension enable`, ce qui affiche ses permissions.

## Mise à jour du kit

`amk update` lit la dernière release GitHub (`releases/latest`). Il lui faut deux fichiers dans cette release :

- `amk-kit-<version>-windows-x64.zip`, avec `amk.exe`, `amk_launcher.exe` et `amk_kernel.dll` à la racine ;
- `SHA256SUMS.txt`, au format de `sha256sum` (`<empreinte>  <nom>` par ligne).

Le zip doit correspondre à son empreinte. Les deux fichiers viennent de la même release : la vérification protège
contre un téléchargement corrompu ou tronqué, pas contre une release publiée avec de mauvais fichiers. Une signature
de code est une question ouverte (QUESTIONS.md). `amk update --apply` ne ferme pas Apple Music : il refuse de copier
tant que l'app tourne.
Les anciens fichiers sont renommés en `*.old` avant d'être remplacés, parce que Windows ne remplace pas un
fichier en cours d'exécution.

## Outils utilisés sur Windows

`amk` n'embarque ni zip ni client HTTP. Il appelle deux outils fournis avec Windows 10 (version 1803) et
Windows 11 :

- `tar.exe` (bsdtar) pour lire et extraire les zip ;
- `curl.exe` pour les téléchargements, avec `--proto =https` (pas de repli vers HTTP, même sur redirection).

Les programmes sont lancés sans passer par `cmd.exe`. Une adresse ou un chemin contenant des guillemets ou des
`&` ne peut donc pas exécuter autre chose.

## Création, diagnostic et transferts

`build <dossier> [fichier.zip]` valide l'élément, écrit un zip, le relit avec le même outil que `install`, puis affiche
le SHA-256 et l'entrée de catalogue à compléter (l'adresse est une valeur à remplacer). Le zip n'est pas compressé et
ses dates sont fixes : le même dossier donne toujours le même fichier. Refuse d'écraser un fichier existant. Les
fichiers et dossiers dont le nom commence par un point ne sont pas inclus. Le zip de `build` n'est pas identique à
celui de `tools/pack.py` (qui compresse) : les deux sont valides, mais le SHA-256 du catalogue doit venir de l'outil
que l'auteur a utilisé pour publier.

`inspect <dossier>` ou `inspect <type> <id>` affiche le manifeste, les permissions, l'état d'activation (pour un
élément installé), et chaque fichier avec sa taille et son SHA-256. Pas encore d'arbre d'interface : il viendra avec le
moteur XAML (M8).

`dev <dossier>` valide l'élément, sauvegarde le kit si l'élément y est déjà, l'active, puis le copie dans le kit à
chaque modification du dossier. Chaque copie écrit `active.txt`, et une ligne `restart <id>` dans `reload.txt`. Le
noyau relit ce fichier toutes les 500 ms, relit `settings.txt`, et redémarre l'élément dans son moteur JavaScript : le
code `main.js` est relu depuis le kit, et l'élément repart avec son stockage. Une modification invalide est signalée,
et la dernière version qui marchait reste dans le kit. Le dossier de l'auteur reste la source : `install`, `update` ou
`remove` remplacent la copie du kit. Un changement de `manifest.json` (permissions, processus) n'est pris en compte
qu'au prochain démarrage d'Apple Music : le noyau garde le manifeste lu au démarrage.

`test <dossier>` charge `main.js` dans le même moteur JavaScript (QuickJS) que le noyau, avec un hôte factice : les
réglages et le stockage restent en mémoire, et le lecteur et le réseau répondent une valeur fixe. Il affiche le
journal de l'élément, chaque appel fait à l'hôte, puis le résultat. Une API utilisée sans la permission déclarée fait
échouer le test (code 1). Rien n'est écrit dans le kit. Un test réussi ne dit pas que l'élément marche dans Apple
Music : cela se vérifie sur le PC.

`types [fichier]` écrit `amk.d.ts` (par défaut dans le dossier courant) : les types de l'objet `AMK` de la section 5
de `docs/extensions/API.md`. Le fichier est écrit à la main, dans `cli/sdk_types.cpp`. Le test
`tools/test_cli.py` vérifie que chaque membre de la section 5 y figure. Il n'est pas généré à partir du texte.
Refuse d'écraser un `amk.d.ts` existant, sauf avec `--yes`.

`doctor` fait un rapport sur le kit. Chaque ligne commence par `[OK]`, `[ATTENTION]` ou `[ERREUR]`. Il vérifie les
réglages, les éléments installés, les éléments activés qui ne se chargeront pas, `active.txt`, le mode sans échec, les
restes d'une installation interrompue, les dernières lignes des journaux, la présence de `tar` (ou `tar.exe`) et de
`curl`, et les versions d'Apple Music testées. Il sort avec le code 1 seulement s'il y a une erreur.

`logs [nombre] [--follow]` affiche les dernières lignes (50 par défaut) de chaque journal du dossier `journal\`.
Avec `--follow`, il continue d'afficher les lignes ajoutées. `--max-cycles <n>` l'arrête après n passages.

`profile list`, `profile save <nom>`, `profile use <nom>` et `profile delete <nom>` gèrent des profils de réglages :
une copie de `settings.txt` rangée dans `profiles\<nom>\`. Un profil ne contient pas les éléments. `profile use`
sauvegarde l'état actuel avant de le remplacer : `restore --backup <nom>` le remet.

`export <fichier.zip>` écrit les réglages et les éléments installés (`extensions`, `themes`, `apps`, `snippets`). Il
n'inclut ni les sauvegardes, ni le journal, ni les profils, ni le cache, ni aucun fichier de l'app. La première entrée
de l'archive est `AMK-EXPORT`.

`import <fichier.zip> [--dry-run] [--yes]` refuse une archive sans `AMK-EXPORT`, et toute entrée hors des dossiers
prévus. L'archive est décompressée dans un dossier temporaire, puis chaque élément est validé : si un seul échoue,
rien n'est écrit. Sinon, la commande affiche le contenu et demande confirmation (sauf avec `--yes`). Elle sauvegarde
alors le kit, remplace les éléments et `settings.txt`, et laisse à `apply` le soin de les charger. `--dry-run` affiche
le contenu sans rien écrire.

Ce qui manque encore à la parité avec Spicetify : l'arbre d'interface de `inspect` (M8). Le côté noyau du rechargement
de `dev` (lecture de `reload.txt`, redémarrage de l'élément) est compilé pour Windows, mais il n'a pas encore tourné dans
Apple Music : il reste à le voir sur le PC de l'utilisateur. Les tests de la CLI vérifient seulement ce qui est écrit dans
`reload.txt`.

## Tests

```bash
cmake -S . -B build && cmake --build build
./build/amk_tests                                   # 135 tests unitaires : JSON, SHA-256, manifestes, catalogue, archives, zip, sauvegardes, rechargement
AMK_BIN=$PWD/build/amk python3 -m unittest discover -s tools -v   # validateur, pack.py, 63 tests de bout en bout du binaire (dont amk test)
node --test tools/test_examples.mjs                 # exemples d'extensions, contre un faux AMK
```

Sous Wine, `AMK_WINE=1 AMK_BIN=build-win/amk.exe` lance les tests de bout en bout qui ne demandent pas `tar.exe`
ni `curl.exe`, que Wine n'a pas. L'installation réelle sous Windows se teste sur un Windows 10 ou 11 (le banc de
test Windows), et sur le PC de l'utilisateur pour Apple Music lui-même.

Ce qui n'est pas testé ici : tout ce qui touche à Apple Music réel (lancement, injection, réaction du noyau).
Voir [PROGRESS.md](../../PROGRESS.md).
