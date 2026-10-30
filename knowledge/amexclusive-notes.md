# Ce qu'AMExclusive nous apprend (relevé du 2026-10-30)

[AMExclusive](https://github.com/AntiOblivionis/AMExclusive) (MIT) ajoute la sortie WASAPI exclusive à Apple Music
pour Windows. Il est testé sur la même version que le PC de l'utilisateur (`1.1540.23042.0`), et il ajoute des réglages
dans la page Lecture des Réglages d'Apple Music. Relu en lecture seule au commit `91f589d` ; les références sont des
chemins de son dossier `mod/`.

## Ce qui marche chez lui et que nous reprenons

- **La fenêtre XAML** : `Window::Current()` renvoie null en WinUI 3 desktop. AMExclusive appelle l'export de
  `AMP.Utils.dll` `?GetAppProperty@SharedUtils@@YA?AUIInspectable@Foundation@Windows@winrt@@AEBUhstring@5@@Z`
  (`IInspectable(hstring const&)`) avec `"MainWindow"` ou `"MiniPlayerWindow"`, puis `try_as<Window>()`
  (`src/UiAdapter.cpp:315-364`). `MiniPlayerWindow` absente renvoie `E_BOUNDS`.
- **Le fil d'interface** : `window.DispatcherQueue().TryEnqueue(Low, ...)` depuis un fil MTA, avec un ticket par
  fenêtre contre les doublons, `HasThreadAccess()` à l'entrée (`UiAdapter.cpp:836-856`, `:1259-1293`). Pas de hook
  `WH_GETMESSAGE` (le nôtre ne s'exécutait pas sur Windows Server 2025).
- **La découverte** : `SetWinEventHook(EVENT_OBJECT_CREATE..SHOW, WINEVENT_OUTOFCONTEXT)` sur le processus, sondage
  toutes les 5 s, puis `Loaded`/`Unloaded`/`Content` sur Frame, Page et UserControl, `Tapped`/`KeyDown` avec
  `handledEventsToo`, `LayoutUpdated` jusqu'au montage, et un numéro de révision qui invalide les rappels périmés.
  Parcours en largeur limité à 512 nœuds, sans entrer dans ItemsControl, ItemsRepeater ni ButtonBase.
- **Les pages se reconnaissent par leur classe** : `get_class_name` contient `PlaybackSettingsPage`. Dans la page,
  le `StackPanel` qui contient le plus de `ToggleSwitch` sert d'ancre ; son dernier enfant sert de modèle de ligne.
- **L'aspect natif** : copie du `Style`, des marges et du padding des contrôles voisins ; à défaut, les ressources
  `CardBackgroundFillColorDefaultBrush` et `CardStrokeColorDefaultBrush`. `AutomationProperties` toujours renseignés.
- **L'idempotence** : chaque élément ajouté porte un nom préfixé ; s'il existe déjà, on le rafraîchit au lieu de le
  recréer. Tout est construit avant d'être inséré, avec retour arrière si un ajout échoue.
- **Les noms réels d'Apple Music** : `TransportControl_PlayPauseStop`, `TransportControl_SkipBack`,
  `TransportControl_SkipForward`, `LCDScrubber` (`x:Name` ou AutomationId), `Scrubber` (AutomationId), lignes de
  piste en `ListViewItem`.
- **Le lecteur, en lecture** : interfaces WinRT privées d'`AMP.Services.Player` déclarées à la main
  (`src/ApplePlayback.h:16-152`) : `IPlayerStatics` {34D100F9-0F0A-5980-8F04-7AF57FDB29B0} `get_Current`,
  `IPlayer`, `IPlayerState`, `INowPlayingItem` (dont `PlayQueueItemID` et le format audio), avec
  `INotifyPropertyChanged` sur `PlayerState`, `NowPlayingItem` et `PlayQueueItemID`.
- **L'injection** : même méthode que la nôtre (`CreateRemoteThread` + `LoadLibraryW`), après vérification de
  l'identité du paquet (`GetPackagesByPackageFamily`, x64, famille `AppleInc.AppleMusicWin_nzyj5cx40ttqa`). Le
  dossier des DLL reçoit l'ACL `S-1-15-2-1` (ALL APPLICATION PACKAGES) en lecture et exécution. Un compagnon démarre
  par une tâche planifiée sur l'événement `Microsoft-Windows-AppModel-Runtime/Admin` 201 (lancement d'Apple Music),
  ce qui marche aussi quand l'utilisateur ouvre Apple Music sans notre raccourci.
- **Les métadonnées WinUI** : Apple Music 1.1540 est aligné sur Windows App SDK 1.8. AMExclusive projette depuis
  `microsoft.windowsappsdk.winui` 1.8.251222000, avec le SHA-256 de `Microsoft.UI.Xaml.winmd` épinglé
  (`A824AB1E380E384C382CEC14AC5DAD9D4C1B91F519D512082F0E7E5D6AC109DE`, `scripts/Get-WinUIMetadata.ps1`).

## Ce que nous ne reprenons pas

- **Le verrou de qualité** : il modifie la sélection des variantes HLS, y compris le sous-type protégé `qlac`, dans
  CoreMedia, par une adresse brute. Ce n'est pas un contournement de DRM, mais c'est collé au chemin des flux
  protégés : hors de nos règles.
- Les hooks CoreAudioToolbox, le remplacement de l'`IAudioClient` d'Apple, le faux `IAudioStreamVolume`, le hook
  global de `CoCreateInstance`.

## Cohabitation avec le kit

Avec AMExclusive actif, Apple écrit dans un render client logiciel, et le vrai rendu part d'un client WASAPI
exclusif en PCM entier. Notre nivellement ne verrait plus le bon flux et casserait le bit-perfect. Règle : si
`am-exclusive-hook.dll` est chargé dans `AMPLibraryAgent.exe`, ou si un `IAudioClient::Initialize` arrive en mode
exclusif, le nivellement se met en pause et le dit. AMExclusive ne gère pas l'Atmos : pas de conflit sur les flux
spatiaux.
