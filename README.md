# Apple Music Mod Kit

Un système qui fait pour l'app officielle **Apple Music pour Windows** ce que Spicetify fait pour Spotify :
des extensions, des thèmes et des modes qu'on active ou désactive à volonté, sans remplacer l'app.

On garde toutes les fonctions d'Apple Music (lossless, Dolby Atmos, paroles, bibliothèque, synchronisation).
On ajoute et on corrige, on ne remplace pas.

> **État : premiers jalons.** Le noyau et le nivellement Atmos sont écrits et testés hors de Windows.
> Il n'y a encore aucun injecteur : rien ne touche Apple Music pour l'instant. Voir [PROGRESS.md](PROGRESS.md).

## Ce qui est en place

| Partie | Dossier | État |
|---|---|---|
| Réglages persistants (fichier texte lisible à la main) | `core/settings.*` | fait, testé |
| Journal | `core/journal.*` | fait, testé |
| Mode sans échec : après deux lancements sans réponse, le suivant démarre sans extension | `core/safe_mode.*` | fait, testé |
| Liste des versions d'Apple Music testées : sur une autre version, le noyau reste inactif | `core/version_gate.*` | fait, testé |
| Hôte d'extensions : chargement, activation une par une, arrêt, isolement des pannes | `core/extension.*` | fait, testé |
| API d'extensions, format du manifeste, catalogue, parité avec Spicetify | `docs/extensions/` | écrit, pas encore implémenté |
| Schémas JSON et validateur (catalogue, manifestes) | `schemas/`, `tools/validate.py` | fait, testé |
| Mesure de loudness BS.1770 (LUFS) | `extensions/atmos-leveler/loudness_meter.*` | fait, testé |
| Politique du nivellement Atmos : cible stéréo, mémoire par morceau, plafond de crête | `extensions/atmos-leveler/leveler.*` | fait, testé |
| Injecteur et lancement du paquet MSIX | à faire | |
| Hooks sur `AppleMusic.exe` et `AMPLibraryAgent.exe` | à faire | |
| Kit de test en un double-clic | à faire | |

Le nivellement Atmos est la première extension. Il mesure le son décodé et règle le gain sur les
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
./build/amk_tests
```

Validation du catalogue et des manifestes (Python 3, bibliothèque standard) :

```bash
python3 -m unittest discover -s tools -v
python3 tools/validate.py catalog catalog/catalog.json
python3 tools/validate.py manifest examples/hello-extension/manifest.json
```

Compilation croisée pour Windows x64 :

```bash
sudo apt install g++-mingw-w64-x86-64
cmake -S . -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake
cmake --build build-win
```

## Organisation

```
core/                      noyau portable : réglages, journal, mode sans échec, versions, extensions
extensions/atmos-leveler/  mesure BS.1770 et politique du nivellement Atmos
docs/extensions/           API d'extensions, format du manifeste, catalogue, parité avec Spicetify
schemas/                   schémas JSON du manifeste et du catalogue
catalog/                   catalogue communautaire (vide pour l'instant)
examples/                  exemple d'extension complet
tools/                     validateur des manifestes et du catalogue, avec ses tests
tests/                     tests unitaires sans dépendance (même code pour Linux et Windows)
cmake/                     fichier de compilation croisée pour Windows
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
