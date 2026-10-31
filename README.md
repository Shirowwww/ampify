# Ampify

Themes, extensions and apps for Apple Music on Windows. Ampify does for the official Apple Music app what
Spicetify does for Spotify: you change how it looks and add features, and Apple Music keeps everything it already
does (lossless, Dolby Atmos, lyrics, your library, sync).

> Early days: Ampify runs on a test PC with Apple Music 1.1792.39682.0 (from the Microsoft Store) and on the
> Windows CI bench. It is not released yet.

## What you get

- Themes that restyle the whole window: colors, fonts, corner radius, glass and acrylic panels, the artwork of
  the song playing behind the interface, a playback bar moved to the bottom. Spicetify `color.ini` palettes work as
  they are (`ampify theme import <github address>`).
- A Now Playing button in the playback bar that opens Apple Music's own full-screen player in one click, laid over
  the window, with its animated artwork and word-by-word lyrics.
- A Marketplace page inside Apple Music, where you browse, install, update and switch themes and extensions in two
  clicks.
- JavaScript extensions, with an API close to Spicetify's (`Ampify.player`, `Ampify.ui`, `Ampify.React`,
  `Ampify.components`, `Ampify.storage`...): panels, pages, notifications, Discord presence, Last.fm scrobbling,
  keyboard shortcuts, a sleep timer.
- An Atmos leveler. Dolby Atmos tracks play about 4 dB quieter than stereo ones in Apple Music; the leveler
  measures each track and corrects the gain on the decoded samples. The volume bar never moves.
- A command line, `ampify.exe`, for settings, themes, extensions, backups and author tools.

## Install

1. Close Apple Music.
2. Download the latest `ampify-<version>-windows-x64.zip`, right-click it, Extract all.
3. Run `ampify_setup.exe`. Windows SmartScreen may warn that the app is not signed yet: More info, Run anyway.
4. Start **Apple Music with Ampify** from the Start menu.

The usual Apple Music icon still starts Apple Music without Ampify. To uninstall, use Settings > Apps > Installed
apps > Ampify for Apple Music. Your settings stay in `%USERPROFILE%\Ampify` unless you choose to delete them.

## Use it

Click **Ampify** at the bottom of Apple Music's sidebar to open the Marketplace (or Settings > Playback > Ampify).
Pick a theme, choose a color scheme, done. "Turn off" or "Restore Apple Music's look" removes the theme without
restarting.

The same from a terminal:

```
ampify theme list
ampify theme set glass --scheme Night
ampify theme clear
ampify market
ampify install sleep-timer --yes
ampify extension enable sleep-timer
ampify doctor
```

All commands: [docs/cli.md](docs/cli.md).

## Make your own

- A theme is a folder with a `manifest.json`, a `colors.ini` and a `user.css`. Start with
  `ampify new theme my-theme` and follow the [five-minute guide](docs/themes/guide.md). Full reference:
  [docs/themes/format.md](docs/themes/format.md).
- An extension is a `manifest.json` and a `main.js`: [docs/extensions/api.md](docs/extensions/api.md).
- `ampify dev <folder>` reloads your element at each save.

## How it works

Apple Music for Windows is a native WinUI 3 app, not a web page, so Ampify cannot inject CSS the way Spicetify
does. The launcher starts Apple Music paused, loads `ampify_kernel.dll` into it, and lets it run. The kernel reads
your theme, translates its CSS into XAML properties on Apple Music's own controls, and runs extensions in a small
JavaScript engine (QuickJS). Nothing in the Apple Music package is modified, and removing a theme puts every value
back. Details: [docs/how-it-works.md](docs/how-it-works.md).

## Safety rules

- Ampify never hooks FairPlay or anything related to DRM, and never records the audio output.
- On an Apple Music version it has not been tested with, the kernel stays inactive: an Apple update cannot break
  the app.
- After two launches in a row that do not answer, the next one starts without extensions (safe mode).

## Build from source

Windows 10 or 11 x64, Visual Studio 2022 Build Tools (C++ workload) and the Windows SDK:

```
pwsh -File scripts\build-msvc.ps1 -Bench    # bench build, accepts any Apple Music version
pwsh -File scripts\build-msvc.ps1           # release build and unit tests
build-msvc\release\ampify_tests.exe
```

More: [docs/development/building.md](docs/development/building.md). Contribution rules:
[CONTRIBUTING.md](CONTRIBUTING.md).

## Credits

Ampify builds on QuickJS-ng and on what AMExclusive learned about Apple Music's player. Full list:
[CREDITS.md](CREDITS.md). Apple Music is a trademark of Apple Inc. Ampify is not affiliated with Apple.
