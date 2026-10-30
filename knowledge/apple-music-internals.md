# Apple Music pour Windows : ce qu'on sait (relevé le 2026-10-30)

Relevé sur la machine de l'utilisateur, version du paquet `AppleInc.AppleMusicWin_1.1540.23042.0_x64__nzyj5cx40ttqa`
(Windows 11 26200). Tout ce qui est marqué « vérifié » a été observé ; le reste est déduit.

## Paquet et processus

- Paquet MSIX installé dans `C:\Program Files\WindowsApps\AppleInc.AppleMusicWin_<version>_x64__nzyj5cx40ttqa\`
  (dossier en lecture seule, protégé : impossible d'y déposer une DLL proxy). Lancement :
  `explorer shell:AppsFolder\AppleInc.AppleMusicWin_nzyj5cx40ttqa!App`. Liste des binaires : `package-binaries.txt`.
- Deux processus (vérifié) :
  - `AppleMusic.exe` : l'interface. **WinUI 3 / Windows App SDK embarqué** (`Microsoft.UI.Xaml.dll` du paquet),
    vues et modèles en C++/WinRT : `AMP.UI.Views.dll`, `AMP.UI.Controls.dll`, `AMP.UI.ViewModels.dll`,
    `AMP.Services.dll`. Contient aussi V8 / `JavaScriptCoreV8.dll`, `JetEngine32.dll` (moteur « Jet » d'Apple,
    pages du store) et WebView2.
  - `AMPLibraryAgent.exe` : bibliothèque, lecture, téléchargements, DRM. Charge les frameworks Apple portés :
    `CoreFoundation`, `Foundation`, `AVFoundationCF`, `CoreMedia`, `CoreAudioToolbox`, `CoreFP` (FairPlay),
    `CoreADI64`, `libdispatch`, `objc`.
- L'interface parle au moteur par COM hors processus (vérifié dans `AppxManifest.xml`, voir `manifest-com.txt`) :
  `com:ExeServer AMPLibraryAgent.exe`, classe `AMP.Core.IAMPMusicLibrary` {68E7097C-F969-4006-AAC3-95115F0ED1C4},
  proxy/stub `AMPLibraryAgent.Proxies.dll`. Interfaces : `IAMPLibrary`, `IAMPMusicLibrary`, délégués
  `PlaybackEventDelegate`, `LibraryEventDelegate`, etc. Les commandes passent surtout par
  `MediaAppExecAgentCommandAsync` / `MediaAppExecFetchRequestAsync` (journal : `kExecAgentCommand`).
- **Métadonnées complètes des composants** : le paquet contient `AMP.Core.winmd` (moteur) et `AMP.Services.winmd`
  (1 035 types : lecture, file d'attente, égaliseur, paroles, badges Atmos/lossless, réglages…). Leur contenu
  (types, méthodes, champs) est dans `winmd/*.txt`. C'est la carte de l'app : une interface ou un mod peut
  appeler ces services au lieu de deviner.
- Protocoles gérés (`protocols.txt`) : `music:`, `musics:`, `itms:`, `itmss:`… Un lien `music://music.apple.com/...`
  ouvre la page dans l'app, mais ne lance pas la lecture.

## Lecture (moteur `AMPLibraryAgent.exe`)

- Lecteur : API C `AVCFPlayer*` exportée par `AVFoundationCF.dll` (liste : `exports/AVFoundationCF.txt`).
  Apple Music crée un nouveau lecteur à chaque contexte de lecture.
- Volume de l'app : `AVCFPlayerSetVolume(player, float)` (linéaire 0..1, curseur à 18 % ≈ 0,178). L'app relit
  le volume par `AVCFPlayerGetVolume`.
- Sound Check : l'app appelle `AVCFPlayerItemSetLoudnessInfo` (« adjust to album loudness ») à la création
  des items. **Aucun niveau n'est appliqué aux items Atmos** : mesuré 5 dB plus bas que la même chanson en
  stéréo (Damso, 0-90 s : stéréo -31,6 LUFS, Atmos -36,6 LUFS en sortie).
- Effets : un « tap » audio (`play> effects> create tap`, `AVCFAudioMixInputParametersSetAudioTapProcessor`)
  sert à l'égaliseur intégré (`EqualizerViewModel` dans AMP.Services).
- Détection Atmos fiable (vérifié) : description CoreFoundation du format de la piste de l'item.
  Atmos = `mediaSubType:'qec3'` (E-AC-3 chiffré) avec une couche `'qc+3'` à 16 canaux ;
  stéréo = `'qaac'` (AAC) ou `'qlac'` (ALAC). `AVCFPlayerItemIsRenderingSpatialAudio` reste à 0 : inutilisable.
  Identifiant stable par variante : `AVCFPlayerItemCopyCurrentStableVariantID` (hash, stable d'une session à l'autre).
- Sortie audio (vérifié par un mod Windhawk qui hooke WASAPI dans `AMPLibraryAgent.exe`) :
  - **stéréo** : `IAudioClient` / `IAudioRenderClient` en mode partagé, float32 2 canaux 96 kHz. Apple recrée le
    flux à chaque changement de format. Le volume du lecteur est déjà appliqué dans les échantillons : on mesure
    -15,95 LUFS avant volume pour un morceau nivelé par Sound Check, soit environ la cible de Sound Check ;
  - **Atmos** : **aucun** flux `IAudioRenderClient`. Le son passe par l'API audio spatiale de Windows
    (`ISpatialAudioClient` → `ISpatialAudioObjectRenderStream`, des objets mono rendus par Dolby Atmos for
    Headphones). Pour agir sur le son Atmos, il faut hooker `ActivateSpatialAudioStream`,
    `Begin/EndUpdatingAudioObjects` et `ISpatialAudioObject::GetBuffer` (v2.3 du mod de référence, à valider).
  - La sortie par défaut de l'utilisateur est un casque USB stéréo 96 kHz float, avec Dolby Atmos for Headphones.
- **L'interface affiche le volume du lecteur** : si un mod change `AVCFPlayerSetVolume`, le curseur d'Apple Music
  bouge (observé : 55 % en stéréo, 71,67 % en Atmos avec +5 dB). Ne jamais régler le gain par le volume du
  lecteur : il faut agir sur les échantillons.

## Ce qui est déjà construit (dans `../reference/`)

- `apple-music-atmos-gain.wh.cpp` : mod Windhawk (v2.3) injecté dans `AMPLibraryAgent.exe`. Il observe
  `AVCFPlayerSetVolume` (sans le modifier), détecte l'Atmos par le format, mesure le son décodé (BS.1770) sur le
  flux stéréo WASAPI et sur les objets spatiaux, apprend la cible sur les morceaux stéréo nivelés par Sound Check,
  puis applique à chaque morceau Atmos un gain sur les échantillons, avec une rampe et un limiteur doux. Il
  mémorise la valeur par variante. La partie spatiale reste à valider en lecture réelle.
- `loudness-meter/` : outil C# .NET 8 qui mesure les LUFS de la sortie par boucle WASAPI, en mémoire seulement.

## Pièges observés

- Le dossier du paquet est en lecture seule. Les écritures d'Apple Music dans AppData et HKCU sont redirigées
  dans `%LOCALAPPDATA%\Packages\AppleInc.AppleMusicWin_nzyj5cx40ttqa\LocalCache\` (virtualisation MSIX).
- Journaux : ETW dans `...\LocalState\Logs\*.etl`, lisibles avec `tracerpt <fichier> -o x.csv -of CSV`. Le fichier
  actif est vide tant qu'il n'a pas tourné (une rotation par lancement).
- Préférences : `...\LocalCache\Roaming\Apple Computer\Preferences\AMPLibraryAgent.exe.plist` (plist binaire ;
  `volumeWSG`, `losslessEnabled`, `downloadDolbyAtmos`, `crossfadeSeconds`…).
- Injection : Windhawk (injection de DLL au démarrage du processus) est toléré par Apple Music, et le recharger à
  chaud aussi. Un essai Frida a coïncidé avec une fermeture d'Apple Music, mais celle-ci venait d'une autre cause
  (un autre outil l'a fermé) : rien n'indique une détection d'anti-débogage, sans que ce soit prouvé.
- Après une relance, la file de lecture est vide : la lecture ne peut reprendre que par un clic de l'utilisateur.
  Les commandes multimédia de Windows (SMTC) gèrent lecture, pause, suivant, précédent, mais pas le choix d'un
  morceau ni la position.
- Un blocage d'environ 7 s vers 15 s sur le premier morceau Atmos après un lancement existe sans aucun mod (une
  demande de clé FairPlay expire à 20 s, puis le nouvel essai réussit). Ce n'est pas un bug à attribuer aux mods.
