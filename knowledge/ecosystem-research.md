# État de l'art (recherche du 2026-10-30)

## Un « Spicetify pour Apple Music » ?

N'existe pas. Aucun cadre de thèmes, d'extensions ou de marketplace qui modifie l'app officielle Windows.
Les 830 mods du dépôt `ramensoftware/windhawk-mods` ne ciblent pas `AppleMusic.exe` ni `AMPLibraryAgent.exe`
(un seul, `island-media-controls`, la pilote par UI Automation depuis explorer.exe).

Raison : Spotify desktop est une coquille CEF qui charge une app web (`xpui.spa`) ; Spicetify réécrit ce paquet
(CSS, JS injecté, « custom apps », marketplace). Apple Music Windows est une app WinUI 3 native : pas de bundle
web à patcher. L'équivalent passe par l'injection de code natif et, pour l'apparence, par l'arbre XAML
(technique des « stylers » Windhawk : `windows-11-taskbar-styler`, `windows-11-settings-styler`, qui utilisent
l'API XAML Diagnostics / `VisualTreeWatcher` ; jamais tentée sur Apple Music).

À étudier : le code de Spicetify (https://github.com/spicetify/cli et spicetify/marketplace) pour son modèle
d'extensions, de thèmes, de réglages et de marketplace ; les stylers XAML de windhawk-mods pour la technique.

## Compagnons existants (s'attachent à l'app sans la modifier)

| Projet | Fonction | Accroche |
|---|---|---|
| PKBeam/AMWin-RP (~1 000 étoiles, actif) | Discord RPC, Last.fm, ListenBrainz | UI Automation |
| dagkan/lastfm-scrobbler | scrobbling | SMTC |
| Eplorr/LyricsBlossom | paroles en surimpression | SMTC + UI Automation |
| lid9en/cadence-apple-music | lecteur compagnon thémable | SMTC + journaux ETL |
| mochrido/apm-lyrics | paroles flottantes | paroles déjà téléchargées par l'app |

## Clients alternatifs

- **Cider** (Cider Collective, cider.sh) : payant (3,49 $ minimum), code fermé. Sur Windows : .NET + WebView2, avec
  son propre moteur au-dessus de MusicKit. Atmos depuis la 4.0 (juin 2026), encore instable. **Pas de lossless**
  (plafond AAC 256). Points forts : thèmes, plugins (PluginKit), égaliseur, Automix, Discord/Last.fm intégrés,
  paroles communautaires, AirPlay/Chromecast, télécommande.
- **Sidra** (wimpysworld/sidra) : open source, Electron autour de music.apple.com, annonce le lossless via
  Widevine VMP (non confirmé).
- Tous les clients tiers passent par le web (MusicKit JS + Widevine) : pas de lossless, Atmos limité. Seul le moteur
  officiel offre Atmos et lossless fiables. D'où l'approche retenue : **modifier l'app officielle**, pas la remplacer.
