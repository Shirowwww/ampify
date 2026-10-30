# M8 : thèmes XAML et réglage intégré

Étude du 2026-10-30. Rien n'est implémenté. Ce document dit ce qu'on peut faire, ce qui reste incertain
et dans quel ordre travailler. l'utilisateur a précisé le périmètre : un équivalent de Spicetify pour Apple Music,
personnalisable au maximum.

## En bref

- Le XAML permet déjà de changer les couleurs, les polices, les marges, les images, de masquer des éléments
  et de remplacer des modèles de contrôle. C'est la plus grande part de ce que fait un thème Spicetify.
- Il ne permet ni les scripts ni les pages entièrement nouvelles. Pour s'en rapprocher, il faut embarquer un
  moteur JavaScript dans le noyau. C'est le plus gros chantier, à faire après M8.
- Le point faible est le ciblage. Spicetify désigne un élément avec du CSS. Ici, on le désigne par type, nom
  et clé de ressource, et ces noms peuvent changer avec une mise à jour d'Apple Music. Le premier livrable doit
  donc être un inspecteur de l'arbre XAML.
- Rien de cela ne se vérifie sous Linux. Tout se teste sur le PC de l'utilisateur, dans le kit de test (M5).

## Sources lues et non lues

Lues : `MASTER_PROMPT.md`, `milestones.md`, `PROGRESS.md`, `knowledge/` (notes, et les `winmd` des espaces de noms
`AMP.UI.Views`, `AMP.UI.Controls`, `AMP.UI.ViewModels` et `AMP.Services`) et
`reference/apple-music-atmos-gain.wh.cpp`.

Non lues : les stylers de windhawk-mods (`windows-11-taskbar-styler`, `windows-11-settings-styler`), ainsi que
le code de spicetify/cli et de spicetify/marketplace. Ces dépôts ne font pas partie de ceux auxquels cette
session a accès. Ce qui est dit d'eux ici vient de `knowledge/ecosystem-research.md` et de la connaissance
générale. Ces passages sont marqués « à vérifier ».

## Comment un styler XAML agit

Une app WinUI 3 construit à l'exécution un arbre d'objets, le *visual tree*. Chaque élément a un type, parfois
un nom (`x:Name`), des propriétés, un style et un modèle de contrôle (`ControlTemplate`).

Les couleurs et les polices viennent de dictionnaires de ressources, identifiés par une clé. Une ressource lue
avec `ThemeResource` est relue quand le thème change. Une ressource lue avec `StaticResource` est fixée au
chargement. Donc, changer une ressource pendant l'exécution ne met pas à jour un élément qui l'a lue en
`StaticResource` : il faut alors modifier l'élément lui-même. Ce comportement reste à mesurer sur l'app réelle.

Deux voies pour atteindre l'arbre depuis l'extérieur :

- l'API XAML Diagnostics, celle du « Live Visual Tree » de Visual Studio. D'après `knowledge/ecosystem-research.md`,
  les stylers Windhawk s'en servent (à vérifier) ;
- un accès direct depuis le processus. Le noyau est déjà dans `AppleMusic.exe` (M3) : il peut trouver
  l'application et parcourir l'arbre, mais seulement sur le thread de l'interface. Je recommande cette voie,
  pour ne pas dépendre d'un outil de diagnostic.

Rien n'est écrit dans le dossier du paquet MSIX, qui est en lecture seule. Les modifications restent en
mémoire. Pour pouvoir les retirer, un thème doit noter la valeur d'origine de chaque propriété avant de la
changer.

## Ce que fait Spicetify

D'après la connaissance générale, à vérifier sur le dépôt :

- Il réécrit le paquet web de Spotify. Un thème tient en un fichier de couleurs (`color.ini`, avec des sections
  nommées), une feuille `user.css` et un `theme.js` facultatif.
- Les extensions sont des scripts qui appellent une API : lecteur, plateforme, React, menus, fenêtres.
- Les pages personnalisées sont des pages web ajoutées à la barre latérale.
- Une marketplace communautaire diffuse thèmes et extensions. Les commandes `apply`, `backup` et `restore`
  appliquent ou défont les changements.

Sa force vient du DOM et du CSS : chaque élément se désigne par un sélecteur, et le rendu passe par un
navigateur, où une page entière peut être remplacée.

## Comparaison capacité par capacité

| Capacité Spicetify | Équivalent XAML sur Apple Music | Verdict |
|---|---|---|
| Schéma de couleurs (`color.ini`) | Surcharge des pinceaux du thème dans les dictionnaires de ressources, plus propriétés directes | Réalisable pour les pinceaux du thème. Partiel : les couleurs codées dans le C++ ou dans les modèles compilés ne suivent pas une ressource. À mesurer. |
| Polices | Ressource de police. Le winmd expose sur `LocalizationService` `SetOverrideFontFamily`, `SetFontSize`, `SetFontType` et `SetLineHeight`, ce qui est un point d'appui | Réalisable pour le texte qui passe par ce mécanisme. Charger une police fournie par l'utilisateur reste à valider. Aucune police d'Apple ne doit être embarquée. |
| Masquer un élément | `Visibility` à `Collapsed` sur un élément trouvé par type et nom | Réalisable. Cible fragile : un nom peut disparaître à une mise à jour. |
| Mise en page (marges, tailles) | Propriétés `Margin`, `Padding`, `Width`, `Height` | Partiel. On ajuste les propriétés, mais on ne réorganise pas l'arbre sans reconstruire les contrôles. |
| Fond et images | Pinceau image sur un élément ou une ressource | Réalisable. Charger une image venant de hors du paquet reste à valider. |
| Restyler un contrôle | Style avec un setter `Template` | Réalisable, mais lourd : il faut reproduire les parties nommées du modèle d'origine. Les modèles compilés (`.xbf`) ne sont pas du texte, donc on les lit à l'exécution. |
| Sélecteurs CSS | Aucun. On cible par type, nom, clé de ressource ou position dans l'arbre | Écart majeur. Il faut un inspecteur et une table de cibles par version. |
| Scripts (`theme.js`, extensions) | Aucun dans l'app | Impossible sans moteur embarqué. Le paquet contient déjà V8 et JavaScriptCore, mais mieux vaut ne pas dépendre des moteurs d'Apple. |
| Pages personnalisées | XAML créé à l'exécution (`XamlReader.Load`), relié au code | Possible en principe. Chaque page est un développement natif, sans l'aide du web. Difficile. |
| API du lecteur (`Spicetify.Player`) | Services et view models d'Apple, visibles dans les winmd | Réalisable en partie : lecture, file d'attente, réglages. Ce qui touche au DRM reste exclu. |
| Application sans redémarrage | Possible en XAML, à condition de bien restaurer | Avantage XAML. Spicetify demande `apply` puis un redémarrage. |
| Pages web | Selon `knowledge/apple-music-internals.md`, le paquet contient WebView2 et le moteur Jet, qui sert aux pages du store. Sur ces pages, du CSS et du JS s'injectent comme chez Spicetify | À étudier : quelles pages sont web. La parité ne vaudrait que pour elles. |

## Limites face au modèle CSS et JS de Spicetify

Par ordre d'importance :

1. Pas de sélecteurs. Cibler par nom est fragile, et les noms changent avec les versions. Sans inspecteur,
   les cibles s'écrivent à l'aveugle.
2. Les modèles de contrôle sont compilés. Leur source XAML n'est pas dans le paquet : il faut les lire à
   l'exécution.
3. Pas de script sans moteur ajouté.
4. Chaque page nouvelle est un développement natif. Le coût se multiplie avec le nombre de pages.
5. On s'appuie sur les données d'Apple : on lit les view models, on ne les réécrit pas.
6. Toute modification passe par le thread de l'interface. Une erreur peut figer l'app. Le mode sans échec doit
   donc désactiver les thèmes dès le premier plantage.
7. Une mise à jour d'Apple peut casser une cible. Spicetify a le même risque. Ici, le noyau reste inactif sur une
   version inconnue, comme décidé dans `PROGRESS.md`.

## Architecture proposée pour M8

Ce sont des propositions, pas encore des décisions.

- **Format de thème** : un dossier avec un manifeste (JSON), un fichier de couleurs (sections nommées), un
  fichier de règles (cible, propriété, valeur) et les ressources fournies par l'utilisateur (images, polices).
  Un script valide le manifeste, comme pour le catalogue (M7).
- **Moteur de règles** : chaque règle désigne un type, un nom facultatif, une propriété et une valeur. Elle
  s'applique sur le thread de l'interface et enregistre la valeur d'origine pour la restaurer.
- **Inspecteur** : un réglage qui écrit dans le journal la liste des éléments d'une page (types, noms,
  propriétés utiles, clés de ressources). C'est l'outil qui permet d'écrire les cibles, et la base des tests
  sur le PC de l'utilisateur.
- **Réglage intégré**, deux options :
  - A. une section dans la page Général (`GeneralSettingsPage`), réinsérée à chaque affichage de la page.
    Simple et réversible ;
  - B. une entrée dans la barre latérale et une page dédiée. Plus propre, mais cela touche à la navigation
    d'Apple (`Sidebar`, `SettingsPageType`).

  Je recommande A pour M8, puis B si la section gêne.
- **Stockage** : le réglage lit et écrit le fichier de réglages du noyau (M1). Il n'écrit pas dans `AppSettings`,
  donc les préférences d'Apple ne changent pas.

## Critère d'acceptation M8

Repris de `milestones.md` : un thème XAML s'applique à une ressource de test dans Apple Music, puis se retire via
un réglage, et ce réglage est visible dans les Réglages d'Apple Music.

Concrètement, sur le PC de l'utilisateur :

1. Apple Music démarre avec le noyau, et « Thème » apparaît dans Général.
2. Le thème de test est choisi : la ressource de test change de couleur.
3. « Aucun » est choisi : la valeur d'origine revient, sans redémarrage.
4. Le journal liste chaque règle appliquée et chaque restauration.

Côté Linux, on peut tester le lecteur du manifeste, le format des couleurs et la logique de sauvegarde et de
restauration, sur un arbre simulé.

## Licences

- Spicetify (cli et marketplace) et Windhawk (mods et stylers) : licence non vérifiée ici. Avant de reprendre un
  fichier ou du code, il faut lire la licence du dépôt concerné et ajouter l'attribution qu'elle demande.
- Pour l'instant, rien n'est copié. On reprend des idées : un thème séparé en couleurs et en règles, des
  couleurs nommées, une restauration par valeur d'origine.
- Le mod de référence du dépôt ne déclare pas de licence, seulement son auteur (Shirow).
- Un thème ne contient que des valeurs et des règles, jamais de fichiers tirés du paquet d'Apple. Aucune police
  d'Apple n'est embarquée.

## Vérifications à faire, dans l'ordre

1. Lire les deux stylers de windhawk-mods et `spicetify/cli` dès qu'ils seront accessibles, pour confirmer la
   technique et les licences de ce document.
2. Sur le PC de l'utilisateur, avec le noyau (M3) : lancer l'inspecteur sur la page Général. Noter les types et les
   noms, et distinguer les pinceaux qui sont des ressources de ceux qui sont codés en dur.
3. Vérifier si le store est une page web (WebView2) ou une page Jet.
4. Tester une police et une image venant de hors du paquet.
5. Mesurer le comportement d'une ressource modifiée pendant l'exécution (`StaticResource` contre `ThemeResource`).

Les points 2 à 5 se font dans un seul passage de test, avec le kit de M5.

## Décisions et questions

Décisions prises ici :

- le format de thème et le moteur de règles sont propres au projet, sans copie de code ;
- premier livrable : l'inspecteur, puis la surcharge des couleurs, puis le réglage dans Général ;
- pas de JavaScript dans M8.

Questions pour l'utilisateur : voir `QUESTIONS.md`, point 5.

## Mise en œuvre (2026-10-30)

Ce qui est fait, avec les décisions de cette étude :

- Format : celui de la parité v2 (`colors.ini`, `resources.xaml` avec liste de clés, `user.css` en plus). Voir
  [`docs/themes/FORMAT.md`](themes/FORMAT.md).
- Moteur de règles : sous-ensemble CSS, cascade par priorité, restauration des valeurs d'origine. Code dans
  `core/theme/`, tests dans `tests/theme_tests.cpp`.
- Inspecteur : un texte qui liste les ressources et l'arbre, avec un sélecteur pour chaque élément. Le réglage
  `theme.inspect` le déclenche.
- Rechargement à chaque sauvegarde : le travailleur compare une signature du dossier, puis le thème est retiré et
  appliqué de nouveau.
- Réglage intégré : une section « Thème » en tête de la page Général, écrite en code (pas en XAML). Non compilée.
- Thèmes d'exemple : `themes/examples/`.

Ajouts pour la parité Spicetify (même format, voir [`docs/themes/FORMAT.md`](themes/FORMAT.md)) : `flow-direction` et
`order` dans `user.css`, variables `:root` et rôles de police (`--font-title`, `font-role`), snippets appliqués après
le thème, schéma `auto` (mode de Windows) et `theme.schedule` (heure), `sidebar.json` et `home.json`, et l'export
`inspector.json`. Le travail côté fil de travail est dans `core/theme/worker.*`, portable et testé.

Ce qui reste ouvert : la liaison XAML n'est pas compilée (voir `win/xaml_backend.cpp`), aucun nom d'élément réel
n'est relevé. Les réglages `theme.parts.colors`, `.resources` et `.assets` sont faits ; `theme.parts.script` reste pour M7.
