# Arbre XAML réel d'Apple Music (relevé du 2026-10-30)

Relevé par l'inspecteur du kit (`theme.inspect = 1`) sur le banc CI, Apple Music `1.1792.39682.0`, sans compte
connecté, page d'accueil ouverte. 482 éléments. Le fichier complet est dans l'artefact du banc
(`banc-windows-msvc-resultats-interface`, `m8-inspecteur/inspector.txt`, run 41125490630). Les sélecteurs sont ceux que
`user.css` accepte.

## Grandes zones

```
MainPage (AMP.UI.Views.MainPage)                      contenu de la fenêtre « MainWindow »
└─ Grid#PageRoot
   ├─ Sidebar#MainSidebar (AMP.UI.Views.Sidebar)
   │  └─ NavigationView#NavView                        barre latérale et tout le reste de la fenêtre
   │     ├─ Grid#PaneRoot (largeur 290, repliée à 48)   volet de gauche
   │     │  ├─ Grid#PaneHeaderGrid                     logo, TextBlock#AppName, MenuButton#MoreButton
   │     │  ├─ Grid#AutoSuggestArea                    recherche (AutoSuggestBox, TextBox#TextBox)
   │     │  ├─ ItemsRepeater#MenuItemsHost             NavigationViewItem (AMP.UI.Views.NavigationViewItem) × 4
   │     │  └─ AccountControl (AMP.UI.Views.AccountControl)
   │     └─ contenu du NavigationView
   │        ├─ BackDrop (AMP.UI.Controls.BackDrop)      fond de la barre du haut (hauteur 6x)
   │        ├─ Grid#TransportBar                       barre de lecture, en haut
   │        │  ├─ TransportControl#TransportControl    ToggleButton#ShuffleButton, précédent / lecture / suivant
   │        │  │                                       (AppBarButton), Button#RepeatButton
   │        │  ├─ NowPlayingControl#LCD                l'écran « LCD » au centre
   │        │  │  └─ Grid#LCD : HoverGrid#ThumbnailHoverGrid (ArtworkImage#ActualArtwork, Image#StaticArtwork),
   │        │  │     StackPanel#TrackTitleAndArtist (TickerTapeControl#TrackTitleTickerTape,
   │        │  │     TickerTapeControl#ArtistTitleTickerTape, TextBlock#CurrentTime, TextBlock#Duration),
   │        │  │     Image#Apple, ScrubberControl#LCDScrubber, AudioBadgeControl#AudioBadgeControl
   │        │  ├─ StackPanel#NowPlayingToggleButtons   VolumeControl#VolumeControl, AirPlayButton,
   │        │  │                                       ToggleButton#LyricsToggleButton, ToggleButton#PlayQueueToggleButton
   │        │  └─ Border#CaptionButtonsOcclusionBorder
   │        ├─ SplitView#ContentSplitView              zone des pages (le volet sert à la file d'attente et aux paroles)
   │        │  └─ Grid#ContentRoot > Frame#ContentFrame > ContentPage (AMP.UI.Views.ContentPage)
   │        │     ├─ Grid#ContentPageShadowGrid
   │        │     └─ Spinner#Spinner
   │        └─ InfoBar#topInfoBar, InfoBar#bottomInfoBar
   ├─ Canvas#DraggableTitlebarRegion (repliée)
   ├─ Border#Titlebar
   └─ Rectangle#Frost (replié)
```

## Ce qu'il faut savoir pour les thèmes et les extensions

- **La barre de lecture est en haut**, dans le contenu du `NavigationView`, pas en bas : `Grid#TransportBar`.
- **Couleurs** : le texte est `#D8000000` en mode clair (TextFillColorPrimary), les fonds sont des pinceaux de thème
  (« présent » dans le relevé : un pinceau qui n'est pas une couleur unie, souvent acrylique ou Mica).
- **Ressources** : le dictionnaire de l'application ne contient qu'une clé à lui (`ContentPageShadow`). Les couleurs
  sont dans ses dictionnaires fusionnés et de thème ; l'inspecteur les liste depuis le commit 7c7f167.
- **Les pages** passent par `Frame#ContentFrame` ; une page chargée est une `ContentPage`. Le contenu de l'accueil
  n'était pas encore chargé au moment du relevé.
- **Noms en double** : `Grid#RootGrid`, `Grid#LayoutRoot`, `Border#Root`, `ContentPresenter#ContentPresenter` viennent des
  gabarits WinUI et existent des dizaines de fois. Viser plutôt le parent nommé (`Grid#TransportBar Button`).
- **MainPage n'est pas un panneau** : son contenu est `Grid#PageRoot`. Apple Music y retire les enfants qu'il ne
  connaît pas (un hôte ajouté par le kit y disparaissait en quelques secondes) : les panneaux du kit passent par un
  Popup de la fenêtre.
- **Réglages** : `PlaybackSettingsPage` (page Lecture), repérée par son nom de classe, comme chez AMExclusive.
