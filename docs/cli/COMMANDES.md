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
| `amk path` | Affiche les dossiers du kit, le dossier d'Apple Music et ses préférences | `path` | Fait |
| `amk path <nom>` | Un seul chemin, sur une ligne : `kit`, `settings`, `extensions`, `themes`, `apps`, `snippets`, `logs`, `backups`, `app` (paquet d'Apple Music), `prefs` (préférences d'Apple Music) | `path` | Fait. `app` et `prefs` : « introuvable » hors Windows |
| `amk config --dir` | Ouvre le dossier du kit dans l'Explorateur (`--dry-run` affiche seulement le dossier) | `config-dir` | Fait |
| `amk backup` / `amk backup list` | Sauvegarde les réglages et les éléments | `backup` | Fait |
| `amk devtools [on\|off]` | Demande au noyau d'écrire l'arbre de l'interface dans `journal\inspector.txt` (réglage `theme.inspect`) | `enable-devtools` | Fait côté `amk`. Le fichier n'est écrit que par un noyau avec le moteur XAML (M8) |
| `amk versions [list]` | Versions d'Apple Music acceptées par le noyau, et celle installée | (aucun) | Fait |
| `amk versions update` | Télécharge la liste des versions testées publiée avec le catalogue et écrit `versions.txt` | (restauration après mise à jour) | Fait |
| `amk install-kit <version>` | Installe une version précise du kit, même plus ancienne (SHA-256 vérifié) | anciennes versions | Fait |
| `amk version`, `amk help` | | `--version`, `help` | Fait |
| `amk backup apply launch` | Plusieurs commandes à la suite, voir « Enchaîner des commandes » | `spicetify backup apply` | Fait |

### Éléments : extensions, snippets, apps, thèmes

| Commande | Rôle | Statut |
|---|---|---|
| `amk list` | Tous les éléments installés, leur type, leur version et leur état | Fait |
| `amk extension list` / `enable <id>` / `disable <id>` | Extensions. Le nivellement Atmos (`atmos-leveler`) est intégré et s'active de la même façon. Si Apple Music tourne avec le noyau, l'extension démarre ou s'arrête tout de suite (voir « Effet immédiat ») | Fait |
| `amk snippet enable <id>` / `disable <id>` | Snippets (fragments XAML) | Fait |
| `amk app enable <id>` / `disable <id>` | Apps (pages d'interface). Le noyau charge `apps\<id>` et démarre l'app tout de suite, comme une extension | Fait. L'entrée dans la barre latérale vient avec M8 |
| `amk theme list` | Thèmes installés | Fait |
| `amk theme set <id> [--scheme <nom>]` | Choisit le thème et son schéma | Fait |
| `amk theme import <dossier\|zip\|adresse> [id]` | Reprend un thème Spicetify : `color.ini` tel quel, `user.css` de Spotify gardé à part (voir [SPICETIFY.md](../themes/SPICETIFY.md)) | Fait |
| `amk theme clear` | Aucun thème | Fait |
| `amk new <type> <id>` | Crée un élément de départ (extension, theme, snippet ou app) dans le dossier courant | Fait |
| `amk validate <dossier>` | Vérifie le manifeste et les fichiers d'un élément, avec les mêmes règles que `tools/validate.py` | Fait |
| `amk validate <catalogue.json>` | Vérifie un catalogue | Fait |

### Marketplace

| Commande | Rôle | Statut |
|---|---|---|
| `amk market` (ou `catalog`) | Liste le catalogue et ce qui est déjà installé | Fait |
| `amk install <id> [--yes]` | Télécharge, vérifie le SHA-256, valide, installe. Désactivé par défaut. Un noyau en marche voit le nouveau dossier tout de suite | Fait |
| `amk update <id> [--yes]` | Met à jour un élément depuis le catalogue, si la version est plus récente. Un noyau en marche le redémarre avec la nouvelle version | Fait |
| `amk remove <id>` | Retire un élément. Une sauvegarde est faite avant. Un noyau en marche l'arrête | Fait |
| `amk update` | Vérifie la dernière release du kit, télécharge, contrôle le SHA-256 | Fait |
| `amk update --check` | Dit seulement si une version plus récente existe | Fait |
| `amk update --apply` | Installe la version téléchargée. Apple Music doit être fermé | Fait |

### Pas encore dans cette version

| Commande prévue | Ce qui la remplace aujourd'hui | Jalon |
|---|---|---|
| `amk inspect` avec l'arbre d'interface en direct (`AMK.React`) | `amk devtools`, puis `amk inspect --tree` affiche le dernier arbre écrit par le noyau | M8 |

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
| `--tree` | Avec `inspect` : affiche le dernier arbre de l'interface (`journal\inspector.txt`) |
| `--dir` | Avec `config` : ouvre le dossier du kit |
| `--` | Tout ce qui suit est un argument, même s'il commence par `--` |

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
  reload.txt                demandes au noyau en marche : « start <id> », « stop <id> », « restart <id> », « rescan » (voir « Effet immédiat »)
  backups\<aaaammjj-hhmmss>\  copie de settings.txt et des quatre dossiers d'éléments
  versions.txt              versions d'Apple Music testées en plus de celles du noyau (écrit par amk versions update)
  journal\                  journaux du noyau et du lanceur, et inspector.txt (arbre de l'interface, voir devtools)
  downloads\, updates\, cache\  fichiers temporaires, dernier catalogue lu, et cache\kit-update.txt (check_kit_update)
```

Réglages reconnus par `amk config` :

| Clé | Valeur | Défaut |
|---|---|---|
| `extensions.<id>.enabled`, `apps.<id>.enabled`, `snippets.<id>.enabled` | `1` ou `0` | `0` (un élément de la communauté est désactivé) |
| `theme.active` | identifiant d'un thème, vide pour aucun | vide |
| `theme.scheme` | nom d'une section de `colors.ini` | défaut du thème |
| `theme.parts.colors`, `theme.parts.resources`, `theme.parts.assets`, `theme.parts.script` | `1` ou `0` : couleurs, ressources XAML, images, script du thème | `1` |
| `theme.live_reload` | `1` ou `0` : réapplique le thème quand ses fichiers changent | `1` |
| `theme.inspect` | `1` ou `0` : le noyau écrit l'arbre de l'interface au prochain passage, puis remet `0` (voir `amk devtools`) | `0` |
| `catalog.url` | `https://…` ou `file://…` | catalogue du dépôt |
| `app_path` | chemin complet du dossier d'Apple Music, vide pour le trouver seul | vide (trouvé par Windows) |
| `prefs_path` | chemin complet du fichier de préférences d'Apple Music, vide pour l'emplacement habituel | vide (`%LOCALAPPDATA%\Packages\AppleInc.AppleMusicWin_nzyj5cx40ttqa\LocalCache\Roaming\Apple Computer\Preferences\AMPLibraryAgent.exe.plist`) |
| `app_launch_flags` | arguments ajoutés au lancement d'Apple Music, séparés par des espaces (guillemets pour garder un espace) | vide |
| `check_kit_update` | `1` ou `0` : `amk` signale une nouvelle version du kit | `0` |
| `telemetry.block` | `1` ou `0` : bloquer les envois de données d'Apple Music. **Pas encore actif** : la clé est acceptée, le noyau ne la lit pas encore | `0` |
| `expose_api` | `1` ou `0` : à `0`, aucun élément JavaScript (extension, app, script de thème) ne démarre | `1` |
| `language` | `fr`, `en` ou `auto` : langue de `amk` et des textes du kit (`AMK.i18n`). `auto` suit la langue du système | `auto` |
| `discord.enabled` | `1` ou `0` : les éléments qui ont `ipc.discord` peuvent afficher la présence Discord | `0` |
| `tray.enabled` | `1` ou `0` : icône du kit dans la zone de notification, avec son menu | `0` |
| `remote.enabled` | `1` ou `0` : télécommande locale sur `127.0.0.1` (jeton obligatoire, créé à la mise à `1`) | `0` |
| `remote.port` | port de la télécommande, de 1024 à 65535 | `8974` |
| `remote.token` | jeton de la télécommande, 32 à 128 caractères `A-Z a-z 0-9 - _`. Vide : un nouveau est créé | créé par le kit |
| `midi.enabled` | `1` ou `0` : lecture d'un contrôleur MIDI | `0` |
| `midi.device` | numéro de l'appareil MIDI, de 0 à 255 | `0` |
| `midi.map` | touches reliées aux commandes du kit, ex. `note:60=next;note:62=previous;cc:7=gain` | vide |

Les services (`discord`, `tray`, `remote`, `midi`) sont tous éteints par défaut. Leur fonctionnement est décrit dans
[API.md](../extensions/API.md), section 5.7. Le noyau les lit au démarrage d'Apple Music : relance l'app après un
changement.

Ces clés correspondent à celles de Spicetify : `app_path` à `spotify_path`, `prefs_path` à `prefs_path`,
`app_launch_flags` à `spotify_launch_flags`, `check_kit_update` à `check_spicetify_update`, `telemetry.block` à
`disable_sentry` et `disable_ui_logging`, `expose_api` à `expose_apis`.

`app_launch_flags` est lu par le lanceur, qui passe ces arguments à Apple Music quand il le démarre (Apple Music peut
les ignorer). `amk launch --dry-run` affiche les arguments qui partiront. Pour une valeur qui commence par `--`,
tape-la directement : `amk config app_launch_flags --mon-option`, ou entre guillemets s'il y en a plusieurs :
`amk config app_launch_flags "--a --b"`. Une valeur invalide (guillemet non fermé, caractère de contrôle) est refusée.

Un élément est dans `extensions\<id>` seulement si son nom de dossier est son identifiant. Les dossiers qui
commencent par un point sont des travaux en cours et sont ignorés.

## Effet immédiat (`reload.txt`)

Quand Apple Music tourne déjà avec le noyau, il n'a pas besoin d'être relancé pour une extension ou une app. `amk`
ajoute une ligne à `reload.txt`, et le noyau lit les nouvelles lignes toutes les 500 ms, relit `settings.txt`, puis
fait la demande dans son moteur JavaScript :

| Ligne | Écrite par | Ce que fait le noyau |
|---|---|---|
| `start <id>` | `extension enable`, `app enable`, `config extensions +id` | Charge l'élément s'il ne le connaît pas encore (nouveau dossier), puis le démarre s'il est activé |
| `stop <id>` | `extension disable`, `app disable`, `config extensions -id`, `remove` | Arrête l'élément |
| `restart <id>` | `dev` (à chaque copie), `update` | Arrête l'élément, relit son dossier (manifeste et code), puis le redémarre s'il est activé |
| `rescan` | `install`, `remove` | Cherche les nouveaux dossiers (il démarre ceux qui sont activés) et oublie ceux qui ont disparu |

Une app s'écrit `app:<id>` (par exemple `start app:paroles`), parce qu'une app et une extension peuvent avoir le même
identifiant (elles sont alors liées, voir `AMK.link` dans `docs/extensions/API.md`). Les lignes s'ajoutent à la fin du
fichier : une demande n'est jamais effacée par la suivante. Au-delà de 64 Ko, le fichier repart de zéro. Le noyau ne
relit pas les lignes déjà présentes au démarrage d'Apple Music : à ce moment-là, il charge tout depuis les dossiers.

Les thèmes et les snippets ne passent pas par ces lignes : le moteur de thèmes suit ses propres fichiers.

Si Apple Music ne tourne pas, ces lignes ne servent à rien et ne gênent pas : le prochain `amk launch` charge tout.

## Enchaîner des commandes

Comme `spicetify backup apply`, plusieurs commandes sans argument peuvent se suivre sur une ligne :

```
amk backup apply launch           sauvegarde, applique, puis lance Apple Music
amk apply devtools launch         idem, et demande l'arbre de l'interface
```

Les commandes qui s'enchaînent : `backup`, `apply`, `launch` (ou `auto`), `restore`, `devtools`, `doctor`, `version`.
Elles s'exécutent dans l'ordre. La première qui échoue arrête la ligne : les suivantes ne sont pas lancées, et le code
de retour est le sien. Les options (`--dry-run`, `--backup <nom>`, `--kit-dir`) valent pour toute la ligne. Dès qu'un
mot n'est pas une de ces commandes, la ligne est une seule commande : `amk backup list` liste les sauvegardes,
`amk devtools off` coupe l'inspecteur.

## Versions d'Apple Music et mises à jour cassées

Le noyau ne s'active que sur une version d'Apple Music testée, et seulement si `AVFoundationCF.dll` exporte encore les
fonctions dont le kit a besoin. Sinon, Apple Music démarre sans mods : rien n'est deviné. Après une mise à jour
d'Apple Music, le kit reste donc inactif jusqu'à ce que la nouvelle version soit testée.

Les versions testées après la sortie du kit sont publiées dans `catalog/versions.json`, à côté du catalogue. Pour
chaque version, il liste les exports vérifiés. `catalog.json` fixe l'adresse et le SHA-256 de ce fichier.

- `amk versions` affiche les versions acceptées (celles du noyau et celles de `versions.txt`), les exports exigés, et
  dit si la version installée est testée.
- `amk versions update` lit le catalogue (même source que `amk market`), télécharge `versions.json`, vérifie son
  SHA-256, puis écrit `versions.txt` dans le dossier du kit. Une version dont les exports vérifiés ne couvrent pas
  ceux dont ce kit a besoin est ignorée.
- Au lancement suivant, le lanceur et le noyau acceptent ces versions en plus des leurs. La vérification des exports
  reste faite à chaque lancement. Un `versions.txt` abîmé est ignoré en entier.

## Revenir à une version du kit

`amk install-kit <version>` (par exemple `amk install-kit 0.1.0`) fait comme `amk update`, mais pour la release
`v<version>` du dépôt, même plus ancienne que celle installée : il télécharge l'archive, vérifie son SHA-256 avec
`SHA256SUMS.txt`, l'extrait et contrôle son contenu. Sans `--apply`, il s'arrête là et dit quelle commande lancer
une fois Apple Music fermé : `amk install-kit <version> --apply`. `--check` dit seulement si cette version existe.
Les réglages et les éléments ne sont pas touchés.

## Nouvelle version du kit

Avec `amk config check_kit_update 1`, les commandes `launch`, `auto`, `apply`, `list`, `doctor` et `market` affichent
une ligne quand une version plus récente du kit existe. La vérification lit la dernière release GitHub au plus une fois
par jour (réponse gardée dans `cache\kit-update.txt`), abandonne après 10 secondes, et ne télécharge rien : c'est
`amk update` qui installe. Les commandes dont un script lit la sortie (`path`, `config`) ne l'affichent jamais.
Désactivé par défaut.

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
fichiers et dossiers dont le nom commence par un point ne sont pas inclus. **`amk build` est l'outil de référence pour
publier** : un auteur n'a besoin que d'`amk`, sans Python. `tools/pack.py` reste pour la CI du dépôt ; son zip
(compressé) n'a pas le même SHA-256, donc on publie avec un seul des deux outils.

`inspect <dossier>` ou `inspect <type> <id>` affiche le manifeste, les permissions, l'état d'activation (pour un
élément installé), et chaque fichier avec sa taille et son SHA-256. `inspect --tree` affiche le dernier arbre de
l'interface écrit par le noyau (`journal\inspector.txt`). Pour en demander un nouveau : `amk devtools`, puis lancer
Apple Music avec `amk launch`. Le noyau écrit l'arbre au passage suivant et remet `theme.inspect` à `0`. Il faut pour
cela un noyau avec le moteur de thèmes XAML (M8) ; sans lui, aucun fichier n'est écrit.

`dev <dossier>` valide l'élément, sauvegarde le kit si l'élément y est déjà, l'active, puis le copie dans le kit à
chaque modification du dossier. Chaque copie écrit `active.txt`, et une ligne `restart <id>` dans `reload.txt` (`restart app:<id>` pour une
app). Le noyau relit ce fichier toutes les 500 ms, relit `settings.txt`, et redémarre l'élément dans son moteur
JavaScript : le dossier est relu depuis le kit (manifeste, `main.js` et les modules qu'il importe), et l'élément repart
avec son stockage. Une modification invalide est signalée,
et la dernière version qui marchait reste dans le kit. Le dossier de l'auteur reste la source : `install`, `update` ou
`remove` remplacent la copie du kit. Un changement de `manifest.json` (permissions, processus, `type`) compte dès le
redémarrage de l'élément : le noyau relit le manifeste à chaque `restart`.

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

Ce qui manque encore à la parité avec Spicetify : l'arbre d'interface en direct de `inspect` (M8), et la lecture
de `telemetry.block` par le noyau. Le côté noyau de l'effet immédiat
(lecture de `reload.txt`, démarrage, arrêt, rechargement, nouvelle lecture des dossiers) est testé sous Linux avec un
hôte factice et compilé pour Windows, mais il n'a pas encore tourné dans Apple Music : il reste à le voir sur le PC de
l'utilisateur. Les tests de la CLI vérifient ce qui est écrit dans `reload.txt`.

## Tests

```bash
cmake -S . -B build && cmake --build build
./build/amk_tests                                   # tests unitaires : JSON, SHA-256, manifestes, catalogue, archives, zip, sauvegardes, versions, arguments
AMK_BIN=$PWD/build/amk python3 -m unittest discover -s tools -v   # validateur, pack.py, tests de bout en bout du binaire (dont amk test)
node --test tools/test_examples.mjs                 # exemples d'extensions, contre un faux AMK
```

Sous Wine, `AMK_WINE=1 AMK_BIN=build-win/amk.exe` lance les tests de bout en bout qui ne demandent pas `tar.exe`
ni `curl.exe`, que Wine n'a pas. L'installation réelle sous Windows se teste sur un Windows 10 ou 11 (le banc de
test Windows), et sur le PC de l'utilisateur pour Apple Music lui-même.

Ce qui n'est pas testé ici : tout ce qui touche à Apple Music réel (lancement, injection, réaction du noyau).
Voir [PROGRESS.md](../../PROGRESS.md).
