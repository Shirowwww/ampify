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

Autour des thèmes, le dossier du kit contient aussi :

```
AppleMusicModKit/
├── settings.txt        réglages (theme.active, theme.scheme, theme.schedule, snippets.<id>.enabled…)
├── themes/<id>/        un dossier par thème
├── snippets/<id>/      un dossier par snippet (voir « Snippets »)
├── resource-keys.txt   clés autorisées dans resources.xaml
├── sidebar.json        barre latérale : entrées masquées et ordre (optionnel)
├── home.json           page d'accueil : sections masquées et ordre (optionnel)
└── journal/            AppleMusic.log, inspector.txt, inspector.json
```

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

- Le premier schéma est celui par défaut. Le réglage `theme.scheme` en choisit un autre. Il peut aussi suivre le mode
  de Windows ou l'heure : voir « Schéma automatique ».
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

Les règles viennent de plusieurs sources, appliquées dans cet ordre : le thème, puis chaque snippet actif (par ordre
alphabétique de leur identifiant), puis `sidebar.json` et `home.json`. Une source qui passe après gagne sur celles
d'avant, quelle que soit la précision de leurs sélecteurs. Seul `!important` passe devant : une règle `!important` du
thème bat une règle normale d'un snippet. Le classement complet est donc : `!important`, puis la source, puis la
précision, puis la place dans le fichier.

### Variables (`:root` et `var()`)

Un bloc `:root` définit des variables. Leur nom commence par `--`. Une valeur les reprend avec `var(--nom)`, ou
`var(--nom, valeur de secours)` si la variable peut manquer.

```css
:root {
    --fond: #1C1C1E;
    --marge: 8px;
    --marges: var(--marge) 16px;
}

Page {
    background: var(--fond);
    padding: var(--marges);
}
```

- Les noms ne tiennent pas compte des majuscules.
- Un bloc `:root` placé plus bas dans le fichier compte aussi. Dans `:root`, une variable peut reprendre celles
  définies avant elle.
- Une variable absente, sans valeur de secours, fait ignorer la déclaration, avec une ligne dans les problèmes.
- Dans `:root`, seules les variables sont lues. Une variable écrite hors de `:root` est ignorée.
- Un snippet voit les variables du thème actif.

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
| `flow-direction` ou `direction` | `FlowDirection` | `ltr` (gauche à droite) ou `rtl` (droite à gauche) ; `left-to-right` et `right-to-left` aussi |
| `order` | position (propre au kit) | nombre entier à partir de 0 : place de l'élément parmi ceux de son parent |
| `font-role` | `FontFamily`, `FontWeight`, `FontSize` | un rôle de police, par exemple `title` (voir « Rôles de police ») |

Une propriété qui n'existe pas sur un élément est refusée sans gravité. Le journal la signale une seule fois.

### Langues de droite à gauche

Spicetify a un réglage `remove_rtl_rule`, parce que le CSS de Spotify contient des règles pour l'arabe et l'hébreu.
Ici, il n'y a pas de telles règles à retirer. À la place, `flow-direction` tourne un élément et tout ce qu'il contient :

```css
Page {
    flow-direction: rtl;
}
```

`ltr` remet le sens habituel. Comme toute valeur, le sens d'origine revient quand le thème est retiré.

### Ordre des éléments

`order` déplace un élément parmi les éléments de son parent : `0` le met en premier. Les éléments sans `order` gardent
leur ordre, après ceux qui en ont un. Ce n'est pas une propriété XAML : le kit déplace l'élément dans la liste de son
parent (par exemple les entrées d'un `NavigationView`). Un parent qui ne sait pas réordonner refuse, sans gravité.
Les déplacements sont défaits, dans l'ordre inverse, quand le thème est retiré. C'est ce que `sidebar.json` utilise.

## Rôles de police

Un rôle de police donne un nom à la police d'un type de texte : titre, texte courant, légende… C'est l'équivalent de
`getFontStyle` dans Spicetify. Un thème règle un rôle dans `:root`, avec trois variables, toutes facultatives :

```css
:root {
    --font-title: "Segoe UI Variable Display";
    --font-title-weight: bold;
    --font-title-size: 30px;
    --font-body: Inter;
}

TextBlock#PageTitle {
    font-role: title;          /* famille, graisse et taille du rôle, en une fois */
}

TextBlock {
    font-family: var(--font-body);
}
```

- `--font-<rôle>` : la famille, comme `font-family`.
- `--font-<rôle>-weight` : la graisse, comme `font-weight` (`normal`, `bold` ou un nombre).
- `--font-<rôle>-size` : la taille, comme `font-size`.

Ce que le thème ne règle pas garde la valeur de Windows 11 :

| Rôle | Famille | Graisse | Taille |
|---|---|---|---|
| `caption` | Segoe UI Variable Small | 400 | 12 |
| `body` | Segoe UI Variable Text | 400 | 14 |
| `body-strong` | Segoe UI Variable Text | 600 | 14 |
| `subtitle` | Segoe UI Variable Display | 600 | 20 |
| `title` | Segoe UI Variable Display | 600 | 28 |
| `title-large` | Segoe UI Variable Display | 600 | 40 |
| `display` | Segoe UI Variable Display | 600 | 68 |

Les noms français sont acceptés : `titre` vaut `title`, `texte` vaut `body`, `sous-titre` vaut `subtitle`, `legende`
vaut `caption`. Un thème peut aussi créer ses propres rôles (`--font-paroles: …`).

Les extensions liront les rôles avec `AMK.fonts.style(nom)`. Côté noyau, la fonction `FontRolesJson` de
`core/theme/fonts.hpp` donne tous les rôles du thème appliqué (thème et snippets ensemble), en JSON :
`{"title":{"family":"…","weight":700,"size":30,"fromTheme":true},…}`.

## Snippets

Un snippet est un petit morceau de style que l'on active seul, comme dans Spicetify. Il se place dans
`%USERPROFILE%\AppleMusicModKit\snippets\<id>\` :

```
coins-carres/
├── manifest.json     "kind": "snippet", "resources": "resources.xaml"
├── resources.xaml    dictionnaire de ressources (demandé par le manifeste, il peut être vide)
└── user.css          règles, comme dans un thème (optionnel)
```

- On l'active avec `amk snippet enable <id>`, qui écrit `snippets.<id>.enabled = 1` dans `settings.txt`.
  `amk snippet disable <id>` le retire.
- Il s'applique par-dessus le thème actif, ou seul s'il n'y a pas de thème. Ses règles gagnent sur celles du thème
  (voir « Priorité »). Plusieurs snippets passent dans l'ordre alphabétique de leur identifiant.
- Ses règles voient les couleurs (`$nom`), les variables et les rôles de police du thème actif. Un snippet n'a pas de
  `colors.ini` : s'il en a un, il est ignoré et signalé.
- Son `resources.xaml` suit la même liste de clés autorisées que les thèmes. Une clé hors liste refuse le snippet,
  pas le thème. Le dictionnaire du snippet est fusionné après celui du thème, il gagne donc sur lui.
- Désactiver un snippet remet ce qu'il y avait dessous (le thème, ou la valeur d'origine), sans redémarrage.
- Les problèmes d'un snippet vont dans le journal avec son nom : `Snippet « coins-carres » : …`.
- Les parties du thème (`theme.parts.*`) ne touchent pas les snippets : chaque snippet s'active seul.

Exemple : [`themes/snippets/coins-carres/`](../../themes/snippets/coins-carres/). `amk theme check <dossier>` vérifie
aussi un snippet.

## Schéma automatique

Le schéma de `colors.ini` peut suivre le mode de Windows (clair ou sombre), ou l'heure.

| Réglage | Effet |
|---|---|
| `theme.scheme = Dark` | ce schéma |
| `theme.scheme =` (vide) | le premier schéma de `colors.ini` |
| `theme.scheme = auto` | suit le mode de Windows |
| `theme.schedule = 07:00=Light,19:00=Dark` | suit l'heure locale ; passe avant `theme.scheme` |

**`auto`.** En mode sombre, le kit prend le premier schéma dont le nom est `Dark`, `Sombre`, `Night` ou `Nuit` (ou le
contient). En mode clair, il prend `Light`, `Clair`, `Day` ou `Jour` (ou un nom qui contient `light` ou `clair`), sinon
le premier schéma qui n'est pas sombre. Un thème avec `[Base]` et `[Dark]` marche donc tel quel. Sans schéma sombre,
le premier schéma reste, et le journal le dit.

**`theme.schedule`.** Une liste `HH:MM=Schéma`, séparée par des virgules. Le schéma en cours est celui de la dernière
heure passée. Avant la première heure de la journée, c'est le dernier schéma de la veille qui continue. Un schéma peut
valoir `auto` : `07:00=auto,22:00=Nuit` suit Windows le jour et force `Nuit` le soir. Une entrée mal écrite est
ignorée et signalée.

Le changement se fait tout seul : le noyau relit le mode et l'heure toutes les 3 secondes et recharge le thème quand le
schéma change. `amk theme set` garde `theme.schedule`.

## Barre latérale et accueil (`sidebar.json`, `home.json`)

Équivalent de `sidebar_config` et `home_config` dans Spicetify : masquer des entrées et changer leur ordre. Les deux
fichiers se placent dans le dossier du kit, à côté de `settings.txt`. Ils s'appliquent avec ou sans thème.

```json
{
  "version": 1,
  "reorder": true,
  "items": [
    {"name": "RadioItem", "hidden": false},
    {"name": "ListenNowItem", "hidden": false},
    {"name": "BrowseItem", "hidden": true}
  ]
}
```

- `name` est le `x:Name` de l'élément. Dans `sidebar.json`, la règle vise `NavigationViewItem#<name>` ; dans
  `home.json`, elle vise `#<name>`, quel que soit le type.
- `hidden: true` donne `visibility: collapsed`.
- Avec `"reorder": true`, la place dans la liste donne `order` : la première entrée vient en premier. Sans `reorder`
  (ou avec `false`), seul le masquage s'applique et l'ordre de l'app reste. Mettez `reorder` seulement si la liste est
  complète.
- Ces règles passent après le thème et les snippets. Les problèmes vont dans le journal (`Disposition : …`).

**Les vrais noms restent à relever.** Ceux de l'exemple sont inventés. Sur le PC, lancez l'inspecteur
(`theme.inspect = 1`) et lisez les `NavigationViewItem` dans `journal\inspector.txt` ou `journal\inspector.json`.

**Générateur.** `core/theme/layout.hpp` fournit de quoi écrire ces fichiers : `NamesMatching(arbre, NavigationViewItem)`
liste les entrées de l'arbre réel, `GenerateLayout(fichier existant, noms)` garde l'ordre et l'état des entrées déjà
listées et ajoute les nouvelles à la fin, visibles, et `WriteLayout` écrit le texte. Après une inspection, le noyau
crée `sidebar.json` s'il n'existe pas encore : la liste complète, rien de masqué, l'ordre actuel (câblé dans
`win/theme_service.cpp`, pas encore compilé). Il suffit ensuite de passer des entrées à `"hidden": true` ou de les
déplacer dans la liste.

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

- `Thème « minuit » chargé, schéma Dark (mode sombre de Windows)` : le thème et la raison du schéma.
- `Appliqué : Grid#Bar Background = #FF1C1C1E (user.css ligne 3)` : une valeur écrite. Pour un snippet, le fichier
  est `snippets/<id>/user.css` ; pour la disposition, `sidebar.json` ou `home.json` (la ligne est alors la place
  dans la liste).
- `Snippet « id » chargé`, `Snippet « id » : …`, `Disposition : …` : l'état et les problèmes de chaque source.
- `Refusé : …` : l'élément ou la valeur a été refusé. Une seule ligne par cause.
- `Thème retiré : N valeur(s) restaurée(s)` : les valeurs d'origine sont revenues.

## Inspecteur

Avec `theme.inspect = 1`, le noyau écrit l'arbre de l'interface au passage suivant, puis remet le réglage à 0 :

- `journal\inspector.txt` : à lire. Les ressources, puis un élément par ligne, avec un sélecteur et ses valeurs.
- `journal\inspector.json` : le même arbre pour les outils (fonction `InspectTreeJson` de
  `core/theme/inspector.hpp`). La commande `amk inspect --tree` le lira.

```json
{
  "version": 1,
  "truncated": false,
  "resources": [{"key": "AccentFillColorDefaultBrush", "value": "#FF0A84FF"}],
  "nodes": [
    {"index": 0, "id": "1", "type": "Page", "fullType": "", "name": "", "parent": -1, "depth": 0,
     "selector": "Page", "properties": {"Background": "#FF000000"}},
    {"index": 1, "id": "2", "type": "Grid", "fullType": "", "name": "Bar", "parent": 0, "depth": 1,
     "selector": "Grid#Bar", "properties": {"Background": "#FF0000FF"}}
  ]
}
```

`nodes` liste les parents avant leurs enfants ; `parent` est l'index du parent dans `nodes` (-1 pour une racine).
`id` est un texte. Les modèles et les objets valent `présent`. Après 5000 éléments, la liste s'arrête et `truncated`
vaut `true`.

## Côté Windows

Le moteur est portable (`core/theme/`) et testé sous Linux. Le service Windows (`win/theme_service.cpp`) l'appelle :

- sur son thread de travail, un `ThemeWorker` (`core/theme/worker.hpp`). À chaque passage : `SetSystemDark(sombre)`
  avec la valeur `AppsUseLightTheme` de `HKCU\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize` (0 =
  sombre), puis `Tick(réglages, minute)` avec l'heure locale en minutes depuis minuit. Le résultat est la demande à
  envoyer au thread d'interface ; sa génération change seulement quand il faut recharger ;
- sur le thread d'interface, `ThemeSession::Tick` applique la demande. Après une inspection, le service écrit aussi
  `inspector.json` et crée `sidebar.json` s'il manque.

Le moteur XAML (`win/xaml_backend.cpp`) doit savoir lire et écrire `FlowDirection` (valeur `LeftToRight` ou
`RightToLeft`) et la position `Order` (déplacer l'élément dans `MenuItems` ou `Children` de son parent). Ce câblage
n'est pas encore compilé : voir [`docs/tests/M8-themes.md`](../tests/M8-themes.md).

## Limites connues

- Les noms d'éléments viennent de l'app. Ils peuvent changer à chaque mise à jour d'Apple Music. Relevez-les avec
  l'inspecteur (réglage `theme.inspect`) avant d'écrire les règles.
- Pas de JavaScript (`main.js`) : c'est l'objet de M7.
- Une ressource lue en `StaticResource` garde sa valeur d'origine jusqu'à la recréation de l'élément. Le
  comportement reste à mesurer sur l'app réelle (procédure dans [`docs/tests/M8-themes.md`](../tests/M8-themes.md)).
- Les images et les polices doivent être dans le dossier du thème.
- Les noms de `sidebar.json` et `home.json` ne sont pas encore relevés sur l'app réelle, et la sidebar d'Apple Music
  n'est peut-être pas un `NavigationView`. Si ce n'est pas le cas, `sidebar.json` ne trouvera rien, sans autre effet.
- `auto` et `theme.schedule` sont relus toutes les 3 secondes : le changement de schéma n'est pas instantané.
