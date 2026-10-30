# Créer son thème en 5 minutes

Il faut Apple Music lancé avec le kit (`amk launch`) et une console ouverte dans un dossier de travail.

## 1. Le thème de départ

```bash
amk new theme mon-theme
```

Le dossier `mon-theme` contient trois fichiers :

| Fichier | Ce qu'il fait |
|---|---|
| `manifest.json` | le nom, l'auteur, la version, le schéma par défaut |
| `colors.ini` | les couleurs, un schéma par section (`[Nuit]` et `[Jour]`) |
| `user.css` | les formes et les effets : coins arrondis, survol, tailles |

Le thème est déjà complet : fond prune, accent corail, coins arrondis, pochette qui grossit au survol.

## 2. Le voir dans Apple Music

```bash
amk dev mon-theme
```

`amk dev` copie le thème dans le kit, le choisit et le recharge à chaque sauvegarde. Laisse cette console ouverte :
chaque fois que tu enregistres un fichier, Apple Music change dans la seconde, sans redémarrer.

Pour l'autre schéma : `amk theme set mon-theme --scheme Jour`.

## 3. Changer les couleurs

Ouvre `colors.ini`. Les noms sont ceux de Spicetify :

| Clé | Ce qu'elle colore dans Apple Music |
|---|---|
| `main` | le fond des pages |
| `sidebar` | la barre latérale |
| `player` | la barre de lecture (en haut dans Apple Music) |
| `card` | l'écran du morceau en cours, au centre de la barre |
| `text`, `subtext` | le texte et le texte secondaire |
| `button` | l'accent : boutons, barre de progression |
| `main-elevated`, `highlight` | les boutons et le survol |

Une valeur s'écrit en hexadécimal, avec ou sans `#` : `FF7A85`, `#FF7A85`, ou `#80FF7A85` avec la transparence
devant. Le mode clair ou sombre d'Apple Music suit la couleur de `main`.

## 4. Changer les formes

`user.css` vise les éléments d'Apple Music par leur type et leur nom, comme en CSS : `Grid#PaneRoot` est la grille
nommée `PaneRoot`. Les couleurs de `colors.ini` s'y écrivent `$main` ou `var(--spice-main)`.

```css
/* Une barre latérale en verre dépoli. */
Grid#PaneRoot {
    backdrop: acrylic($sidebar, 0.6);
}

/* Un dégradé sur le fond des pages. */
ContentPage {
    background: linear-gradient(135deg, $main 0%, #2A1F45 100%);
}

/* Les boutons de lecture grossissent au survol. */
TransportControl#TransportControl AppBarButton:hover {
    transform: scale(1.15);
}
```

Les éléments les plus utiles :

| Sélecteur | Élément |
|---|---|
| `Grid#PaneRoot` | la barre latérale |
| `Grid#TransportBar` | la barre de lecture |
| `BackDrop` | le fond de la barre de lecture |
| `NowPlayingControl#LCD > Grid#LCD` | l'écran du morceau en cours |
| `ArtworkImage#ActualArtwork` | la pochette dans la barre |
| `TransportControl#TransportControl AppBarButton` | précédent, lecture, suivant |
| `SplitView#ContentSplitView > Grid > Grid#ContentRoot` | la zone des pages |
| `ContentPage` | une page (Accueil, Bibliothèque...) |
| `TextBlock#AppName` | le nom « Apple Music » en haut à gauche |

L'arbre complet est dans [knowledge/apple-music-xaml-tree.md](../../knowledge/apple-music-xaml-tree.md). Pour voir
celui de ta version d'Apple Music : `amk devtools`, puis le fichier `journal\inspector.txt` du kit.

Trois pièges :
- un nom comme `Grid#ContentRoot`, `Border#Root` ou `Grid#RootGrid` revient des dizaines de fois dans les gabarits de
  WinUI. Vise-le toujours sous un parent nommé, sinon la règle touche aussi la barre de lecture ou les boutons ;
- ne mets pas de `z-index` sous 10 sur `Grid#TransportBar` : elle passerait sous les pages ;
- une taille de texte posée sur tous les `TextBlock` écrase aussi les titres des pages.

Toutes les propriétés (couleurs, dégradés, images, acrylique, marges, coins, polices, transformations, ombres,
survol, placement dans la grille) sont dans [FORMAT.md](FORMAT.md).

## 5. Le vérifier et le partager

```bash
amk theme check mon-theme
amk build mon-theme
```

`amk theme check` lit le thème comme le noyau et signale chaque règle refusée, avec sa ligne. `amk build` vérifie
le manifeste, écrit le `.zip` et donne l'entrée à ajouter au catalogue (voir
[CATALOG.md](../extensions/CATALOG.md)).

## Reprendre un thème Spicetify

```bash
amk theme import https://github.com/catppuccin/spicetify
amk theme set catppuccin --scheme mocha
```

Le `color.ini` marche tel quel. Le détail est dans [SPICETIFY.md](SPICETIFY.md).
