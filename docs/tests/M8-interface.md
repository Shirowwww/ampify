# Test M8 : interface native des extensions et carte « Mods »

Ce test vérifie que le kit dessine l'interface des extensions dans la fenêtre d'Apple Music, avec de vrais contrôles
WinUI, et qu'il ajoute la carte « Mods (Apple Music Mod Kit) » dans Réglages > Lecture. Le banc Windows le fait
déjà en partie (cas `63-m8-interface`, build MSVC avec la liaison XAML) ; ce document dit quoi regarder sur ton PC.

Rien de ce qui suit n'a encore tourné dans Apple Music : le code est écrit d'après l'API WinUI 3 et d'après ce
qu'AMExclusive a prouvé sur la version 1.1540.

## Comment le kit trouve la fenêtre

- `Window::Current()` ne marche pas dans une app WinUI 3 de bureau. Le noyau demande la fenêtre à `AMP.Utils.dll`
  (fonction exportée `SharedUtils::GetAppProperty`, avec `"MainWindow"` ou `"MiniPlayerWindow"`), comme AMExclusive.
- Tout ce qui touche à l'interface passe par la file de la fenêtre (`DispatcherQueue`), une demande à la fois par
  fenêtre. L'ancien crochet `WH_GETMESSAGE` n'est plus utilisé : sur Windows Server 2025, il ne s'exécutait jamais.
- Le noyau suit les pages avec les événements de fenêtre de Windows, un passage toutes les 5 secondes, et les
  événements `Loaded`, `Unloaded` et `Content` des cadres et des pages. Il ne parcourt jamais plus de 512 éléments,
  et n'entre jamais dans les listes (la bibliothèque peut avoir des milliers de lignes).

## Ce que l'extension voit à l'écran

| Primitive | Contrôle natif |
|---|---|
| `stack`, `list` | pile verticale |
| `row` | deux colonnes (texte à gauche, contrôle à droite) avec 2 enfants, sinon une ligne |
| `card` | carte aux couleurs du thème (ou copiées d'une vraie carte des Réglages), titre et sous-titre |
| `text` | texte en style titre, sous-titre, corps ou légende |
| `button` | bouton (`primary` en couleur d'accent, `tertiary` sans fond), avec son icône |
| `toggle` | interrupteur à droite de son libellé |
| `slider`, `textfield`, `dropdown` | curseur, zone de texte, liste déroulante, avec leur libellé au-dessus |
| `image`, `icon` | image ; icône dessinée en formes, qui suit le mode clair ou sombre |

- Chaque élément porte le nom `AMK_<extension>_<nœud>` (par exemple `AMK_react-panel_panel-react-panel.0`), visible
  dans Accessibility Insights ou Inspect. Le nœud racine d'un panneau est `panel-<id>`, puis la position de chaque
  enfant (`.0`, `.2.1`), ou sa clé (`key`).
- Quand l'extension renvoie son arbre, seuls les éléments changés sont modifiés ; un élément qui change de type est
  reconstruit.
- Le style vient des contrôles natifs : quand la page Lecture des Réglages a été ouverte une fois, les nouveaux
  interrupteurs, boutons et cartes copient le style, les marges et les couleurs de ses lignes. Avant, ce sont les
  ressources standard de WinUI (`CardBackgroundFillColorDefaultBrush`, `CardStrokeColorDefaultBrush`…).

## Emplacements (provisoires)

Ces placements sont des hypothèses tant que l'arbre réel d'Apple Music n'a pas été relevé (inspecteur du test M8) :

- **Panneaux** (`AMK.ui.panel`) : une colonne de 380 px à droite du contenu de la fenêtre, sous la barre de titre et
  au-dessus de la barre de lecture. Chaque panneau a un bouton « Fermer » ; il revient au prochain démarrage de
  l'extension.
- **Popups** : une boîte de dialogue native (`ContentDialog`), une à la fois, avec « Fermer ».
- **Pages des apps** : elles recouvrent le contenu quand l'app appelle `AMK.ui.navigate`, avec un bouton « Retour ».
- **Boutons de la barre de lecture** : après le bouton « Suivant » (`TransportControl_SkipForward`).
- **Boutons de la barre du haut** : à côté du champ de recherche, s'il est dans une pile horizontale.
- **Menus** : pas encore affichés.

## Étapes sur ton PC

1. Copie le dossier `examples\react-panel` du kit dans `%USERPROFILE%\AppleMusicModKit\extensions\`, puis active-le
   avec `amk extension enable react-panel`.
2. Lance Apple Music avec `amk launch`. Le panneau « Panneau React » doit apparaître à droite. Clique « Plus un » :
   le compteur passe à 1.
3. Ouvre Réglages > Lecture. En bas de la liste, la carte « Mods (Apple Music Mod Kit) » liste tes extensions avec
   un interrupteur chacune, et un bouton « Ouvrir le dossier du kit ». Coupe `react-panel` : le panneau disparaît
   tout de suite. Si AMExclusive est installé, ses cartes restent à leur place, la nôtre vient après.
4. Fais une capture d'écran de chaque étape.

## Lignes du journal (`journal\AppleMusic.log`)

- `Interface : fenêtre principale obtenue par AMP.Utils (GetAppProperty « MainWindow »)…` : la fenêtre est trouvée.
- `Interface : premier travail exécuté par la DispatcherQueue de la fenêtre principale (fil N)` : la file marche.
- `Interface : service démarré (crochet WinEvent posé, …)` puis `Interface : fenêtre « MainWindow » liée, premier
  rafraîchissement exécuté par sa DispatcherQueue (fil N)`.
- `Interface : découverte de « MainWindow » : N nœud(s) parcouru(s), P page(s), C conteneur(s) suivi(s)`.
- `Interface : hôte des panneaux ajouté à droite du contenu de la fenêtre (…)` ou `… ouvert dans un Popup …`.
- `Interface : panneau « react-panel/react-panel » monté (N élément(s), AutomationId …)`, puis
  `… mis à jour sur place (créés 0, modifiés 1, reconstruits 0, retirés 0)` après un clic.
- `Réglages : page PlaybackSettingsPage repérée, en attente de son chargement` puis
  `Réglages : carte « Mods (Apple Music Mod Kit) » ajoutée à la page Lecture (N extension(s), K valeur(s) de style
  natif copiée(s))`.
- `Réglages : extension « react-panel » coupée depuis la carte Mods (stop react-panel demandé)`.
- En cas de problème : `Interface : AMP.Utils.dll ou son export GetAppProperty est indisponible…`,
  `Interface : emplacement « playbar » introuvable…`, `Réglages : page Lecture trouvée, mais sans liste…`,
  `Interface : rendu de « … » en échec (0x…)`. Envoie-moi ces lignes.
