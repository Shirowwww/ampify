# State of the art

## Is there a "Spicetify for Apple Music"?

No. No theme, extension or marketplace framework modifies the official Windows app. None of the 830 mods in the
`ramensoftware/windhawk-mods` repository target `AppleMusic.exe` or `AMPLibraryAgent.exe` (one, `island-media-controls`,
drives it through UI Automation from explorer.exe).

Why: Spotify desktop is a CEF shell that loads a web app (`xpui.spa`). Spicetify rewrites that package (CSS, injected JS,
custom apps, marketplace). Apple Music for Windows is a native WinUI 3 app: there is no web bundle to patch. The equivalent
goes through native code injection and, for the look, through the XAML tree (the technique of the Windhawk "stylers",
`windows-11-taskbar-styler` and `windows-11-settings-styler`, which use the XAML Diagnostics API / `VisualTreeWatcher`; not
attempted on Apple Music before).

Ampify takes a direct route instead of XAML Diagnostics: the kernel is already inside `AppleMusic.exe`, so it finds the
window and walks the tree on the UI thread. That avoids depending on a diagnostics tool.

To study: Spicetify's code (https://github.com/spicetify/cli and spicetify/marketplace) for its model of extensions, themes,
settings and marketplace; the XAML stylers of windhawk-mods for the technique. Check the license of each repository before
taking any file or code, and add the attribution it asks for. Ampify copies ideas, not code.

## Existing companions (they attach to the app without modifying it)

| Project | Function | Hook |
|---|---|---|
| PKBeam/AMWin-RP (~1,000 stars, active) | Discord RPC, Last.fm, ListenBrainz | UI Automation |
| dagkan/lastfm-scrobbler | scrobbling | SMTC |
| Eplorr/LyricsBlossom | lyrics overlay | SMTC + UI Automation |
| lid9en/cadence-apple-music | themeable companion player | SMTC + ETL logs |
| mochrido/apm-lyrics | floating lyrics | lyrics already downloaded by the app |

## Alternative clients

- **Cider** (Cider Collective, cider.sh): paid (minimum $3.49), closed source. On Windows: .NET + WebView2, with its own
  engine on top of MusicKit. Atmos since 4.0 (June 2026), still unstable. **No lossless** (AAC 256 ceiling). Strengths:
  themes, plugins (PluginKit), equalizer, Automix, built-in Discord and Last.fm, community lyrics, AirPlay/Chromecast, remote.
- **Sidra** (wimpysworld/sidra): open source, Electron around music.apple.com, claims lossless through Widevine VMP
  (not confirmed).
- All third-party clients go through the web (MusicKit JS + Widevine): no lossless, limited Atmos. Only the official engine
  offers reliable Atmos and lossless. Hence the approach: **modify the official app**, do not replace it.

## Licenses

- Spicetify (cli and marketplace) and Windhawk (mods and stylers): check the repository's license before reusing a file or
  code, and add the attribution it asks for. Nothing is copied so far. Ideas taken: a theme split into colors and rules,
  named colors, restoration by original value.
- The reference Windhawk mod in this repository declares no license, only its author.
- A theme contains only values and rules, never files taken from Apple's package. No Apple font is shipped.
