# Banc de test Windows

Le banc vérifie sur un vrai Windows, avec Apple Music installé, ce que le kit fait : lancement, injection du noyau dans
`AppleMusic.exe` et `AMPLibraryAgent.exe`, mode sans échec, retour au lancement normal. Il tourne sans personne : c'est
le moyen de tester sans demander à l'utilisateur de lancer l'app.

Il ne remplace pas le test M3 sur le PC de l'utilisateur. Il couvre les étapes qui se vérifient toutes seules. Lecture de
musique, Atmos, paroles et synchronisation exigent le compte Apple Music : ces tests restent sur le PC de l'utilisateur.

## Lancer le banc

**Automatique.** Le workflow `.github/workflows/windows-bench.yml` tourne à chaque push sur une branche `**`
qui touche au code du kit. Il a deux jobs :

1. **Compiler les deux builds** (Ubuntu) : tests unitaires, puis compilation croisée pour Windows x64 de deux builds.
   - `diffusion` : le build qui sera livré. Il n'injecte que sur les versions de `core/version_gate.hpp`.
   - `banc` : le même code, compilé avec `AMK_BENCH=ON`. Il accepte toute version d'Apple Music (voir plus bas).
2. **Banc Windows** (`windows-latest`) : installe Apple Music avec `winget` (source Microsoft Store), puis lance
   `bench/windows/run-bench.ps1`.

Les résultats sont dans le résumé du job (tableau par test) et dans l'artefact `banc-windows-resultats` : `rapport.md`,
`results.json`, `winget.log`, `preparation.txt`, et pour chaque test son dossier avec les journaux du kit et les
captures d'écran.

**À la main.** Onglet Actions, workflow « Banc de test Windows », bouton « Run workflow ».

**Sur un PC Windows.** Déconseillé sans prévenir l'utilisateur. Le banc ferme Apple Music de force, et il vide le dossier
`%USERPROFILE%\AppleMusicModKit\journal` et le fichier `state.txt` (il ne touche pas à `settings.txt`). Commande :

```powershell
pwsh -File bench\windows\run-bench.ps1 -KitRoot <dossier avec diffusion\ et banc\> -RepoRoot <dépôt> -OutputRoot <dossier>
```

Options : `-Only <id>` pour ne lancer qu'un test, `-SkipInstall` pour ne pas tenter d'installer Apple Music.

## Ce que dit le résultat

| Statut | Sens |
|---|---|
| `PASS` | Vérifié. |
| `FAIL` | Ne marche pas. Le job échoue, et le détail dit ce qui manque. |
| `INFO` | Constat sans verdict, par exemple la version installée ou le lancement suspendu indisponible. |
| `SKIP` | Test impossible sur ce runner, avec la raison (par exemple Apple Music absent). Ne fait pas échouer le job. |

Si Apple Music ne s'installe pas sur le runner, les tests d'application sont sautés et le job reste vert : regarder le
résumé et `winget.log`. Les tests qui n'ont pas besoin d'Apple Music tournent quand même.

## Les tests actuels

| Id | Ce qu'il vérifie | Étape du test M3 |
|---|---|---|
| `lancement-normal` | Apple Music lancé comme d'habitude, sans le lanceur : pas de journal du noyau, pas de module `amk_kernel.dll`, fenêtre qui répond. | 3 |
| `version-diffusion` | Build de diffusion : injection si la version installée est dans la liste testée, refus sinon. L'app démarre dans les deux cas. | 1 et 2 |
| `injection-banc` | Build de banc : signature de `AVFoundationCF.dll` reconnue, noyau chargé dans `AppleMusic.exe` et `AMPLibraryAgent.exe`, lancement suspendu (nombre de threads repris), application saine après 20 s, fermeture propre du lanceur. | 1, 2 et 3 |
| `mode-sans-echec` | Compteur à 2 : le lanceur démarre sans noyau, puis remet le compteur à zéro après 20 s. | 4 |
| `m8-interface` (banc MSVC, build M8) | Fenêtre obtenue par AMP.Utils, travail exécuté par la DispatcherQueue, panneau de `react-panel` monté après un démarrage à chaud (`reload.txt`), éléments `AMK_…` trouvés par UI Automation, clic « Plus un » mis à jour sur place, thème minuit, carte Mods de Réglages > Lecture si la navigation automatique y arrive. Une capture PNG par étape (`m8-avant-montage`, `m8-panneau-monte`, `m8-panneau-clic`, `m8-theme-applique`, `m8-reglages-mods`). Voir `docs/tests/M8-interface.md`. | M8 |

Les tests `lancement-normal`, `version-diffusion` et `injection-banc` vérifient aussi qu'Apple Music n'a pas planté
(événements « Application Error » de Windows).

## Build de banc (`AMK_BENCH`)

`cmake -DAMK_BENCH=ON` compile un build qui accepte toute version d'Apple Music non vide, au lieu de la liste de
`core/version_gate.hpp`. Il sert à tester le mécanisme d'injection sur la version installée sur le runner, qui n'est pas
forcément `1.1540.23042.0`. Il désactive les tests unitaires, qui vérifient la politique de diffusion.

**Ne jamais diffuser ce build.** Le message `AMK_BENCH=ON` s'affiche pendant la compilation. Le `BUILD_INFO.txt` de
l'artefact le dit aussi.

Le test `version-diffusion` garde la politique de diffusion : sur une version inconnue, il vérifie que le build de
diffusion refuse l'injection.

## Signature du build (`AVFoundationCF.dll`)

Le lanceur et le noyau lisent la table d'exports de `AVFoundationCF.dll`, dans le dossier du paquet, à chaque
lancement. Les noms requis sont dans `core/build_signature.hpp`. Le noyau ne stocke aucune adresse : il ne résout
que par nom. Une version testée dont la DLL n'exporte pas ces noms est refusée. Le journal du lanceur écrit une ligne
`signature reconnue : 5 exports trouvés dans AVFoundationCF.dll`, ou `signature non reconnue : exports absents ...`.
Le test `injection-banc` vérifie cette ligne sur la version installée sur le runner.

Pour marquer une nouvelle version comme testée, il faut les deux : le banc injecte proprement sur elle, et sa signature
est reconnue. Ensuite on l'ajoute à `kKnownAppVersions`.

## Ajouter un test

Un test est un fichier `bench/windows/cases/NN-nom.ps1`. Les fichiers sont lancés dans l'ordre de leur nom.

```powershell
# Ce que le test vérifie, en une phrase.

Register-Case -Id 'mon-test' -Title 'Phrase qui dit ce qui est vérifié' -Requires 'apple-music' -Action {
    param($Ctx)

    Reset-KitState
    $launcher = Join-Path $script:KitRoot 'banc\amk_launcher.exe'
    $launcherProcess = Start-Launcher -LauncherPath $launcher

    $launched = Wait-Journal -FileName 'launcher.log' -Text 'Apple Music démarré' -TimeoutSeconds 120
    Add-Check -Ctx $Ctx -Name 'Apple Music démarré' -Passed $launched

    Close-AppleMusic | Out-Null
    Add-Check -Ctx $Ctx -Name 'lanceur arrêté' -Passed ($launcherProcess.WaitForExit(15000))
}
```

- `-Id` : identifiant court, en minuscules, avec des tirets. Il sert à `-Only` et au nom du dossier de résultats.
- `-Requires` : liste de prérequis. Aujourd'hui seul `apple-music` existe. Pour en ajouter un, l'ajouter dans
  `Test-Requirement` (`run-bench.ps1`). Un test sans `-Requires` tourne même sans Apple Music.
- `Add-Check` : PASS ou FAIL selon `-Passed`. `Add-Info` : constat. `Add-Skip` : test impossible, avec la raison.
- Ne pas dépendre de l'état laissé par un autre test : commencer par `Reset-KitState`.
- Fermer Apple Music à la fin (`Close-AppleMusic`). Le banc le fait de toute façon entre deux tests, mais le test doit
  dire s'il s'est fermé proprement.
- Les journaux sont lus tels que le C++ les écrit, accents compris. Dans une chaîne entre apostrophes, doubler l'apostrophe
  (`'l''app'`), ou utiliser des guillemets doubles.

Helpers disponibles (`bench/windows/lib/harness.ps1`) :

| Helper | Rôle |
|---|---|
| `Reset-KitState` | Arrête tout, vide les journaux et `state.txt`. |
| `Start-Launcher -LauncherPath` | Lance `amk_launcher.exe`. Renvoie le processus. |
| `Start-AppNormally` | Lance Apple Music comme depuis le menu Démarrer (sans noyau). |
| `Wait-Journal -FileName -Text -TimeoutSeconds` | Attend qu'une ligne apparaisse dans un journal. |
| `Test-JournalContains -FileName -Text` | Vrai si le texte est déjà dans le journal. |
| `Find-JournalMatch -FileName -Pattern` | Expression régulière sur un journal, pour lire un chiffre. |
| `Get-KernelState -ProcessName` | Combien de processus ont `amk_kernel.dll` chargé. |
| `Get-AppCrashEvents -Since` | Plantages d'Apple Music depuis une date. |
| `Save-Screenshot -Ctx -Name` | Capture de l'écran principal, dans le dossier du test. |
| `Close-AppleMusic` | Ferme la fenêtre, puis de force si besoin. Renvoie `$true` si elle est partie seule. |
| `Set-SafeModeCount` / `Get-SafeModeCount` | Lire et écrire le compteur du mode sans échec. |

**Tests d'un autre fil.** Un autre fil peut ajouter ses tests de la même façon : un fichier dans `cases/`, sans
`-Requires` s'il n'a pas besoin d'Apple Music. Si son test a besoin d'un exécutable, il l'ajoute au job « Compiler les
deux builds » (dans `dist/`), puis le lit depuis `$script:KitRoot`.

## Ce que le banc ne couvre pas

- Lecture de musique, Atmos, paroles, synchronisation : il faut le compte Apple Music. La lecture stéréo et Atmos avec
  le kit est couverte par le banc connecté (section suivante), avec un compte de test. Le ±1 LU, les paroles et la
  synchronisation restent sur le PC de l'utilisateur.
- Tout ce qui touche à FairPlay (`CoreFP`, `CoreADI64`, chemins FairPlay) : jamais, ni en CI ni ailleurs.
- Les versions d'Apple Music autres que celle installée sur le runner. La version installée est écrite dans
  `preparation.txt` et dans le résumé.
- Un vrai Windows de bureau : `windows-latest` est une image serveur. Le Microsoft Store peut refuser l'installation
  d'Apple Music sur cette image : dans ce cas le banc le signale, et il faudra un autre moyen.
- Les mises à jour d'Apple Music : le banc ne teste qu'une installation fraîche.

## Banc connecté (compte Apple Music de test)

Workflow `.github/workflows/windows-live.yml` (« Banc Windows connecté »). Il fait ce que le banc normal ne peut pas :
se connecter à Apple Music, jouer de la musique avec le kit injecté, et relever les vrais noms des éléments de
l'interface. Il utilise un compte Apple **jetable** qui appartient au propriétaire du dépôt, jamais son compte principal.

Ce qu'il fait, dans l'ordre :

1. **Sortie audio virtuelle.** Les runners n'ont pas de carte son. `bench/windows/live/setup-audio.ps1` installe le
   pilote virtuel VB-CABLE (Pack43, la même méthode que le CI de Mozilla cubeb sur `windows-2025`), ou Scream 4.0 en
   secours. Les archives sont vérifiées par SHA-256. Le script vérifie qu'une sortie par défaut existe, puis essaie
   d'activer le son spatial Windows Sonic. Aucun son n'est enregistré : le câble ne fait que consommer le flux.
2. **Connexion** (`bench/windows/live/connexion.ps1`), **sans le kit** : restauration de la session en cache si elle
   existe, sinon connexion par UI Automation avec l'identifiant, le mot de passe et le code de double authentification.
3. **Tests** (`run-bench.ps1 -CasesDir live/cases`) :
   - `sortie-audio` : sortie par défaut, objets spatiaux, flux à métadonnées (celui de l'Atmos d'Apple Music).
   - `interface-arbre` : arbre UI Automation de la fenêtre principale et de Réglages > Lecture dans le journal du job
     (400 premières lignes chacun), plus un court arbre de chaque page des Réglages.
   - `lecture-kit` : build de banc injecté, nivellement Atmos activé (`extensions.atmos-leveler.enabled = 1`). Le test
     règle Dolby Atmos sur « Toujours activé » dans les Réglages d'Apple Music, comme un utilisateur. Il joue environ
     45 s de « Take Five » (Dave Brubeck, stéréo), puis 45 s de « bad guy » (Billie Eilish, Dolby Atmos), puis quelques
     secondes de « Take Five » pour clore la mesure Atmos. Ensuite il lit `AMPLibraryAgent.log` : mesure stéréo et LUFS,
     variante Atmos, flux et hooks ForMetadata, gain, aucun hook sur CoreFP, CoreADI64 ni FairPlay. Il vérifie aussi que
     le curseur de volume d'Apple Music n'a pas bougé.

Les lignes du journal du noyau et les vérifications sont écrites dans le journal du job. Les captures d'écran sont dans
l'artefact `banc-windows-connecte-resultats` : `connexion/02-connecte.png`, `lecture-kit/03-lecture-stereo.png`,
`lecture-kit/04-lecture-atmos.png`, `interface-arbre/10-reglages-lecture.png`, chacune avec sa version `-fenetre.png`
(la fenêtre d'Apple Music seule).

### Ajouter les deux secrets (une seule fois)

1. Sur GitHub, ouvrir le dépôt, puis **Settings** > **Secrets and variables** > **Actions**.
2. **New repository secret**. Nom : `AMK_APPLE_ID`. Valeur : l'adresse e-mail du compte Apple de test. **Add secret**.
3. **New repository secret**. Nom : `AMK_APPLE_PASSWORD`. Valeur : le mot de passe de ce compte. **Add secret**.

Sans ces deux secrets, le workflow ne fait rien (un avertissement le dit). Le compte doit avoir un abonnement Apple
Music actif (ou un essai), sinon les morceaux ne se jouent pas en entier.

### Lancer un run avec un code de double authentification

Le compte a la double authentification : au premier run, Apple demande un code.

1. Sur GitHub : onglet **Actions**, workflow **Banc Windows connecté**, bouton **Run workflow**. Choisir la branche.
   Laisser le formulaire ouvert.
2. Sur un iPhone, iPad ou Mac connecté au compte de test : **Réglages** > ton nom > **Connexion et sécurité** >
   **Obtenir un code de validation**. Un code à 6 chiffres s'affiche.
3. Le copier dans le champ **code_2fa**, puis cliquer sur **Run workflow** tout de suite : le code expire vite. Le job
   se connecte avant même que le kit soit compilé, pour utiliser le code au plus tôt.

Si Apple demande un code et qu'aucun n'a été donné, le job s'arrête tout de suite avec un message clair : refaire les
étapes ci-dessus. Les champs `storefront` (pays du compte, `fr` par défaut) et `play_seconds` (45 par défaut) peuvent
rester tels quels.

### Session gardée entre deux runs

Après une connexion réussie, les dossiers du paquet d'Apple Music (`LocalState`, `LocalCache`, `Settings`,
`RoamingState` sous `%LOCALAPPDATA%\Packages\AppleInc.AppleMusicWin_nzyj5cx40ttqa`) sont archivés, puis chiffrés
(AES-GCM, clé dérivée du secret `AMK_APPLE_PASSWORD`), et gardés par `actions/cache`. Au run suivant, l'archive est
restaurée avant le premier lancement. Si Apple Music est encore connecté, aucun mot de passe n'est tapé. Sinon, il faut
un nouveau run manuel, avec un code si Apple en demande un. Le journal ne montre que des nombres de fichiers et des
tailles, jamais le contenu. Rien d'autre que les dossiers d'Apple Music n'est gardé.

Sur un push (branche `**` qui touche au banc connecté), le workflow ne tourne qu'avec une session en cache. Il ne
tape jamais de mot de passe dans ce cas : chaque tentative de connexion alerte les appareils du compte.

### Limites et risques connus

- **Session liée à la machine.** Apple Music peut lier sa session au PC (chaque runner est une machine neuve). Dans ce
  cas le cache ne suffit pas : chaque run manuel demande une connexion, et souvent un code.
- **Nombre d'appareils.** Chaque runner peut compter comme un nouvel appareil du compte. Apple limite le nombre
  d'appareils et d'ordinateurs associés. Si Apple refuse, retirer les anciens appareils du compte de test
  (account.apple.com > Appareils).
- **Noms des éléments.** La connexion et la lecture cherchent les boutons par leur nom (anglais sur le runner, français
  aussi reconnu). Si l'interface d'Apple Music change, le job écrit ce qui est à l'écran et l'arbre UIA dans son
  journal, pour corriger les noms.
- **Atmos sur une sortie virtuelle.** Si la sortie n'offre pas le flux à métadonnées, Apple Music joue la version
  stéréo de « bad guy ». Le test le note (INFO) au lieu d'échouer, et le journal du noyau dit ce qui a été joué.
- **Secrets.** Ils sont masqués dans le journal (GitHub, plus `::add-mask::` pour le code). Le kit n'est jamais chargé
  pendant la connexion. Aucune capture n'est prise entre la saisie de l'identifiant et la connexion confirmée. Les
  arbres UIA écrits dans l'artefact passent par le même masquage.

### Fichiers

| Fichier | Rôle |
|---|---|
| `.github/workflows/windows-live.yml` | Le workflow (lancement manuel, ou push avec une session en cache). |
| `bench/windows/lib/uia.ps1` | UI Automation : `Find-Element`, `Wait-ForElement`, `Invoke-Click`, `Set-Text`, `Send-Key`, `Get-TreeDump`, `Write-TreeDump`, `Show-AppleMusicWindow`, masquage des secrets. |
| `bench/windows/lib/audio.ps1` | Sorties audio (Core Audio), état de `ISpatialAudioClient`, son spatial (WinRT). |
| `bench/windows/live/setup-audio.ps1` | Pilote audio virtuel, sortie par défaut, Windows Sonic. |
| `bench/windows/live/connexion.ps1` | Installation, restauration de session, connexion, double authentification. |
| `bench/windows/live/session-cache.ps1` | Archive chiffrée de la session (Save / Restore). |
| `bench/windows/live/live-common.ps1` | Morceaux de test, motifs de noms, ouverture des pages et des Réglages. |
| `bench/windows/live/cases/*.ps1` | Les tests connectés. Le banc normal ne les lance pas. |
