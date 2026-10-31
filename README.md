# Ampify

Un système qui fait pour l'app officielle **Apple Music pour Windows** ce que Spicetify fait pour Spotify :
des extensions, des thèmes et des modes qu'on active ou désactive à volonté, sans remplacer l'app.

On garde toutes les fonctions d'Apple Music (lossless, Dolby Atmos, paroles, bibliothèque, synchronisation).
On ajoute et on corrige, on ne remplace pas.

> **État : intégration en cours.** Toutes les branches de travail sont réunies. Le lanceur et le noyau sont validés
> sur le PC de l'utilisateur (lancement, injection, mode sans échec). Le nivellement Atmos, le runtime JavaScript, les thèmes,
> la CLI `ampify`, l'installateur et le kit de test sont testés sous Linux et sous Wine, et attendent leur passage sur
> Windows. Procédures : [M3](docs/tests/M3-injecteur.md), [M4](docs/tests/M4-atmos.md),
> [M5 + M6](docs/tests/M5-M6-kit-de-test.md). Voir [PROGRESS.md](PROGRESS.md).

## Ce qui est en place

| Partie | Dossier | État |
|---|---|---|
| Réglages persistants (fichier texte lisible à la main) | `core/settings.*` | fait, testé |
| Journal | `core/journal.*` | fait, testé |
| Mode sans échec : après deux lancements sans réponse, le suivant démarre sans extension | `core/safe_mode.*` | fait, testé |
| Liste des versions d'Apple Music testées : sur une autre version, le noyau reste inactif | `core/version_gate.*` | fait, testé |
| Hôte d'extensions : chargement, activation une par une, arrêt, isolement des pannes | `core/extension.*` | fait, testé |
| API d'extensions, format du manifeste, catalogue, parité avec Spicetify | `docs/extensions/` | écrit ; implémenté en partie (voir `docs/extensions/API.md`, section 12) |
| Runtime JavaScript : moteur QuickJS vendu, objet `Ampify`, minuteries, permissions à l'exécution | `core/js_runtime.*`, `core/ampify_prelude.*`, `core/script_extension.*`, `vendor/quickjs/` | fait, testé (Linux, Wine) |
| Lecture du JSON et validation des manifestes côté noyau | `core/json.*`, `core/manifest.*` | fait, testé |
| Chargeur d'éléments : dossiers, activation, rechargement, isolement des pannes | `core/extension_loader.*`, `win/win_host.*`, `kernel/` | fait, testé ; branché au noyau, à tester sur le PC |
| Exemples d'extensions (5) | `examples/` | fait, validés et exécutés par les tests |
| Schémas JSON et validateur (catalogue, manifestes) | `schemas/`, `tools/validate.py` | fait, testé |
| Mesure de loudness BS.1770 (LUFS) | `extensions/atmos-leveler/loudness_meter.*` | fait, testé |
| Politique du nivellement Atmos : cible stéréo, mémoire par morceau, plafond de crête | `extensions/atmos-leveler/leveler.*` | fait, testé |
| Identité du paquet, décision d'injection, garde DRM | `core/apple_music.*`, `core/injection_policy.*`, `core/drm_guard.*` | fait, testé |
| Lanceur : lancement d'Apple Music, pause du premier processus pendant l'injection, suivi du paquet | `launcher/`, `core/package_watch.*` | écrit, compilé, à tester sur Windows |
| Noyau chargé dans les deux processus (DLL) | `kernel/` | écrit, compilé, à tester sur Windows |
| Hooks sur `AppleMusic.exe` et `AMPLibraryAgent.exe` | à faire (M4) | |
| CLI `ampify` : réglages, extensions, thèmes, snippets, `apply`, `launch`, `restore`, `watch`, sauvegardes, `build`, `inspect`, `dev`, `test`, `types`, `doctor`, `logs`, `profile`, `export`, `import` | `cli/`, `docs/cli/COMMANDES.md` | fait, testé (135 tests unitaires, 63 tests de bout en bout) |
| Installation et mise à jour depuis le catalogue, SHA-256 vérifié, archives contrôlées | `cli/marketplace.cpp` | fait, testé sous Linux (`unzip`). Sous Windows : `tar.exe` et `curl.exe` |
| Catalogue, schémas, validateur, empaquetage | `catalog/`, `schemas/`, `tools/` | fait, testé |
| Spécification de la page Marketplace dans l'app | `docs/marketplace/PAGE.md` | écrite, la page reste à faire (M8) |
| Installateur et désinstallation propre : copie trois fichiers, raccourci du menu Démarrer, entrée dans Paramètres > Applications. Ne touche jamais au paquet d'Apple Music | `setup/`, `win/` | écrit, testé sous Wine, à tester sur Windows |
| Kit de test en un double-clic (`Ampify-Test.exe`) : installe, lance Apple Music, enregistre une trace ETW, collecte les journaux, écrit un rapport anonyme | `testkit/`, `core/run_report.*` | écrit, testé sous Wine, à tester sur Windows |
| Banc de test Windows automatique : Apple Music installé, lancement, injection, mode sans échec | `bench/windows/`, `.github/workflows/windows-bench.yml` | écrit, lancé en CI, voir [docs/tests/banc-windows.md](docs/tests/banc-windows.md) |

Le nivellement Atmos est la première extension. Un élément de la communauté s'installe avec `ampify install <id>` depuis
le catalogue, et reste désactivé tant que l'utilisateur ne l'a pas activé (voir [docs/cli/COMMANDES.md](docs/cli/COMMANDES.md)). Il mesure le son décodé et règle le gain sur les
échantillons, jamais sur le curseur de volume : le curseur d'Apple Music ne bouge pas.

## Règles qui ne changent pas

- **Pas de contournement de DRM.** Aucune extraction de clé FairPlay, aucun déchiffrement, aucune
  capture de flux protégé. Les modifications portent sur l'interface, sur le comportement, et sur le son
  *après* décodage.
- **Aucun binaire Apple dans le dépôt**, aucun identifiant ni mot de passe.
- **Aucun enregistrement de la sortie audio sur disque.** Les mesures restent en mémoire.
- Les tests qui demandent le compte Apple Music de la personne se font sur son PC, jamais dans le cloud.

## Compiler et tester

Tests sous Linux (ou tout système avec CMake 3.20+ et un compilateur C++20) :

```bash
cmake -S . -B build
cmake --build build
./build/ampify_tests
```

Tests de la CLI, de la marketplace et de la validation du catalogue et des manifestes (Python 3, et le binaire `ampify`
compilé, chemin absolu) :

```bash
AMPIFY_BIN=$PWD/build/ampify python3 -m unittest discover -s tools -v
python3 tools/validate.py catalog catalog/catalog.json
python3 tools/validate.py manifest examples/hello-extension/manifest.json
node --test tools/test_examples.mjs
```

Compilation croisée pour Windows x64 (produit `ampify_tests.exe`, `ampify_kernel.dll`, `ampify_launcher.exe`, `ampify_setup.exe`, `Ampify-Test.exe` et `ampify.exe`) :

```bash
sudo apt install g++-mingw-w64-x86-64
cmake -S . -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake
cmake --build build-win
```

Essais de l'installateur et du kit de test sous Wine (depuis le dossier `build-win`) :

```bash
scripts/wine-e2e.sh build-win <dossier de préfixe Wine>
```

## Organisation

```
cli/                       commande `ampify` : JSON, SHA-256, manifestes, catalogue, kit, sauvegardes, commandes, plateforme
catalog/                   catalogue public des éléments (JSON, vide pour l'instant)
schemas/                   schémas JSON du manifeste et du catalogue
tools/                     validateur, empaquetage (pack.py), tests Python et Node des outils et de la CLI
docs/cli/                  référence des commandes de `ampify`
docs/marketplace/          spécification de la page Marketplace dans l'app
core/                      noyau portable : réglages, journal, mode sans échec, versions, extensions, injection
kernel/                    DLL chargée dans AppleMusic.exe et AMPLibraryAgent.exe (Windows)
launcher/                  lanceur : paquet MSIX, lancement suspendu, injection (Windows)
win/                       aides Windows partagées par le lanceur et le noyau
extensions/atmos-leveler/  mesure BS.1770 et politique du nivellement Atmos
docs/extensions/           API d'extensions, format du manifeste, catalogue, parité avec Spicetify
schemas/                   schémas JSON du manifeste et du catalogue
catalog/                   catalogue communautaire (vide pour l'instant)
examples/                  exemple d'extension complet
tools/                     validateur des manifestes et du catalogue, avec ses tests
tests/                     tests unitaires sans dépendance (même code pour Linux et Windows)
setup/                     installateur et désinstallateur (ampify_setup.exe, Windows)
testkit/                   kit de test M5 (Ampify-Test.exe, Windows) et trace ETW
bench/windows/             banc de test Windows (PowerShell) : installe Apple Music, lance, vérifie les journaux
docs/tests/                procédures de test sur le PC, une par jalon, et le banc Windows
cmake/                     fichier de compilation croisée pour Windows
vendor/quickjs/             moteur JavaScript quickjs-ng 0.17.0, non modifié (MIT)
knowledge/                 relevés sur l'app : architecture, API, pièges observés
reference/                 mod Windhawk de référence et outil de mesure LUFS
```

Pour comprendre le fonctionnement interne d'Apple Music, lire
[knowledge/apple-music-internals.md](knowledge/apple-music-internals.md). Le tour des projets
existants est dans [knowledge/ecosystem-research.md](knowledge/ecosystem-research.md).

## Crédits

La méthode de pondération K de BS.1770 (recalculée pour chaque fréquence d'échantillonnage) suit
libebur128. Les constantes viennent de la recommandation UIT-R BS.1770-4. Le nivellement reprend les
valeurs du mod Windhawk de référence.
