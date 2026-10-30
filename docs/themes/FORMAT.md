# Thèmes : format et règles

Un thème est un dossier. Il contient au moins un des fichiers ci-dessous. Le format suit la section 6.1 de
[`docs/extensions/API.md`](../extensions/API.md), avec une couche de règles en plus (`user.css`).

```
mon-theme/
├── colors.ini        couleurs par schéma (optionnel)
├── resources.xaml    dictionnaire de ressources XAML (optionnel)
├── user.css          règles pour des éléments précis (optionnel)
├── images/           images référencées par url(...)
└── templates/        modèles de contrôle référencés par url(...)
```

Le nom du dossier sert d'identifiant. Le dossier d'un thème installé se trouve dans
`%USERPROFILE%\AppleMusicModKit\themes\`.

## colors.ini

Une section par schéma, par exemple `[Base]` et `[Dark]`. Chaque clé est le nom d'une brosse de l'app. La valeur
est une couleur hexadécimale, avec ou sans `#` : `FA2D48`, `#FA2D48` ou `#80FA2D48` (alpha en tête).

```ini
[Base]
AccentFillColorDefaultBrush = 0A84FF

[Dark]
AccentFillColorDefaultBrush = 0A84FF
SolidBackgroundFillColorBase = 1C1C1E
```

- Le premier schéma est celui par défaut. Le réglage `theme.scheme` en choisit un autre.
- Les mêmes noms servent de couleurs nommées dans `user.css` : `background: $SolidBackgroundFillColorBase;`.
- Un nom qui n'existe pas dans l'app est signalé dans le journal (« ressource absente ») et ne fait rien d'autre.

## resources.xaml

Un dictionnaire de ressources XAML. Chaque `x:Key` doit figurer dans la liste des clés autorisées. Une seule clé
hors liste refuse tout le thème, et le journal dit laquelle.

La liste vient de l'inspecteur : le fichier `resource-keys.txt` du dossier `%USERPROFILE%\AppleMusicModKit\`
contient une clé par ligne. Sans ce fichier, la vérification est désactivée et le journal le signale.

## user.css

Un sous-ensemble de CSS. Une règle associe des sélecteurs à des déclarations.

```css
/* commentaire */
Grid#NowPlayingBar {
    background: #1C1C1E;
}

Grid#Bar > TextBlock {
    color: $TextFillColorPrimaryBrush;
    font-size: 18px;
}

#AdvertisingBanner {
    visibility: collapsed !important;
}
```

### Sélecteurs

| Écriture | Cible |
|---|---|
| `Grid` | tous les `Grid` (nom court du type) |
| `Microsoft.UI.Xaml.Controls.Grid` | le même type, avec son nom complet |
| `#Bar` | l'élément dont le `x:Name` est `Bar` |
| `*` | tous les éléments |
| `A B` | un `B` situé quelque part dans un `A` |
| `A > B` | un `B` dont le parent direct est un `A` |

On peut combiner : `Grid#Bar > TextBlock`. Les attributs (`[x=y]`) et les pseudo-classes (`:hover`) ne sont pas pris
en charge. Une règle qui en contient est ignorée, avec une ligne dans le journal.

### Priorité

Quand plusieurs règles visent le même élément et la même propriété, la plus précise gagne : un `#nom` bat un type, et
un type bat `*`. À précision égale, la règle écrite en dernier gagne. `!important` bat tout le reste.

### Propriétés

| CSS | XAML | Valeurs acceptées |
|---|---|---|
| `background` | `Background` | couleur, `$nom`, `transparent`, `url(images/fichier.png)` |
| `color` ou `foreground` | `Foreground` | couleur, `$nom` |
| `border-color` | `BorderBrush` | couleur, `$nom` |
| `border-width` | `BorderThickness` | 1 à 4 longueurs, ordre CSS : haut droite bas gauche |
| `corner-radius` | `CornerRadius` | une seule longueur, appliquée aux quatre coins |
| `font-family` | `FontFamily` | nom de police, entre guillemets ou non |
| `font-size` | `FontSize` | nombre, `px` facultatif |
| `font-weight` | `FontWeight` | `normal`, `bold` ou un nombre de 1 à 1000 |
| `margin` | `Margin` | 1 à 4 longueurs |
| `padding` | `Padding` | 1 à 4 longueurs |
| `width`, `height` | `Width`, `Height` | longueur ou `auto` |
| `opacity` | `Opacity` | nombre de 0 à 1 |
| `visibility` | `Visibility` | `visible` ou `collapsed` (`hidden` et `none` valent `collapsed`) |
| `template` | `Template` | `url(templates/fichier.xaml)`, un `ControlTemplate` complet |

Une propriété qui n'existe pas sur un élément est refusée sans gravité. Le journal la signale une seule fois.

## Vérifier un thème

Sur n'importe quel PC, sans Apple Music :

```
amk_theme_check.exe C:\chemin\vers\mon-theme [Dark]
```

L'outil affiche les schémas, chaque règle retenue avec sa ligne, puis les problèmes. Il sort avec le code 0 seulement
si le thème est utilisable et sans problème.

## Parties du thème

Trois réglages, dans `settings.txt`, permettent d'éteindre une partie d'un thème sans le supprimer. Tous valent `true`
par défaut.

| Réglage | Effet quand il vaut `false` |
|---|---|
| `theme.parts.colors` | `colors.ini` n'écrit aucune ressource de couleur. Les couleurs restent utilisables dans `user.css` avec `$nom`. |
| `theme.parts.resources` | `resources.xaml` et les règles de `user.css` sont lus, mais rien n'est appliqué. |
| `theme.parts.assets` | Les images et les polices du dossier sont refusées. |

Une partie éteinte est quand même lue : les problèmes sont signalés comme d'habitude.

## Commandes du kit

Dans un terminal, depuis le dossier du kit :

```
amk theme list
amk theme set <id|none> [--scheme nom]
amk watch <dossier-du-thème> [--scheme nom] [--polls N]
```

- `theme list` : chaque thème du dossier `themes` avec son état, utilisable ou refusé.
- `theme set` : choisit le thème dans les réglages. Un thème refusé n'est pas choisi, et les réglages restent
  inchangés (code 1). `none` remet les couleurs d'origine. Si Apple Music est ouvert, le changement s'applique dans les
  3 secondes.
- `watch` : vérifie le dossier au lancement, puis le revérifie à chaque changement de fichier. Pratique pour écrire un
  thème en voyant les règles retenues. `--polls N` arrête après N vérifications (utile pour les tests).

Le dossier du kit est `%USERPROFILE%\AppleMusicModKit`. L'option `--kit <dossier>` le remplace. Code de sortie 2 : mauvais
usage (argument absent, thème introuvable).

## Journal

Dans `%USERPROFILE%\AppleMusicModKit\journal\AppleMusic.log` :

- `Appliqué : Grid#Bar Background = #FF1C1C1E (user.css ligne 3)` : une valeur écrite.
- `Refusé : …` : l'élément ou la valeur a été refusé. Une seule ligne par cause.
- `Thème retiré : N valeur(s) restaurée(s)` : les valeurs d'origine sont revenues.

## Limites connues

- Les noms d'éléments viennent de l'app. Ils peuvent changer à chaque mise à jour d'Apple Music. Relevez-les avec
  l'inspecteur (réglage `theme.inspect`) avant d'écrire les règles.
- Pas de JavaScript (`main.js`) : c'est l'objet de M7.
- Une ressource lue en `StaticResource` garde sa valeur d'origine jusqu'à la recréation de l'élément. Le
  comportement reste à mesurer sur l'app réelle (procédure dans [`docs/tests/M8-themes.md`](../tests/M8-themes.md)).
- Les images et les polices doivent être dans le dossier du thème.
