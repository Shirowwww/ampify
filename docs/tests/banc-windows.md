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

- Lecture de musique, Atmos, paroles, synchronisation : il faut le compte Apple Music. Tests sur le PC de l'utilisateur seulement.
- Tout ce qui touche à FairPlay (`CoreFP`, `CoreADI64`, chemins FairPlay) : jamais, ni en CI ni ailleurs.
- Les versions d'Apple Music autres que celle installée sur le runner. La version installée est écrite dans
  `preparation.txt` et dans le résumé.
- Un vrai Windows de bureau : `windows-latest` est une image serveur. Le Microsoft Store peut refuser l'installation
  d'Apple Music sur cette image : dans ce cas le banc le signale, et il faudra un autre moyen.
- Les mises à jour d'Apple Music : le banc ne teste qu'une installation fraîche.
