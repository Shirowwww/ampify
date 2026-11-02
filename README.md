<div align="center">

<img src="docs/assets/logo.svg" width="112" alt="">

# Ampify

**Themes, extensions and a Marketplace for Apple Music on Windows.**

[![Latest release](https://img.shields.io/github/v/release/Shirowwww/ampify?display_name=tag&sort=semver&style=flat-square&color=fa233b)](https://github.com/Shirowwww/ampify/releases/latest)
![Windows 10 | 11](https://img.shields.io/badge/Windows-10%20%7C%2011-0078D6?logo=windows&style=flat-square)
[![CI](https://img.shields.io/github/actions/workflow/status/Shirowwww/ampify/ci.yml?branch=dev&style=flat-square&label=CI)](https://github.com/Shirowwww/ampify/actions/workflows/ci.yml)

</div>

Ampify does for the official Apple Music app what Spicetify does for Spotify. You change how it looks and add
features, and Apple Music keeps everything it already does: lossless, Dolby Atmos, lyrics, your library and sync.

> [!WARNING]
> **Experimental.** Ampify changes a running Apple Music in memory. It is a personal project tested on a few PCs, so
> expect rough edges. `ampify restore` turns it off at any time, and uninstalling puts everything back as it was.

Ampify works with Apple Music 1.1792 and 1.1540 from the Microsoft Store. On any other version it stays inactive
until that version has been tested, so an Apple update never breaks the app. What changed: [CHANGELOG.md](CHANGELOG.md).

## Install

Close Apple Music, then paste this line in PowerShell:

```powershell
irm https://raw.githubusercontent.com/Shirowwww/ampify/main/scripts/install.ps1 | iex
```

It downloads the latest release, checks its SHA-256 and installs it for your Windows account (no administrator
rights). Then open Apple Music as usual, from its own icon: Ampify loads with it, now and every time it starts.

You can also download `ampify-<version>-windows-x64.zip` from the [Releases](https://github.com/Shirowwww/ampify/releases)
page, extract it and run `ampify_setup.exe`.

Microsoft Defender or SmartScreen may warn about `ampify_setup.exe` or `ampify_launcher.exe` (`Trojan:Win32/Cloxer`,
`Trojan:Win32/Sabsik.FL.A!ml`). This is a false positive: the files are not code-signed yet, and the launcher loads
Ampify's DLL into Apple Music, as some malware loads code into other programs. If you would rather not allow them,
build Ampify from source (below).

To uninstall, open Settings > Apps > Installed apps > Ampify for Apple Music. Your settings stay in
`%USERPROFILE%\Ampify` unless you choose to delete them.

## Use it

Click **Ampify** at the bottom of Apple Music's sidebar to open the Marketplace. Pick a theme and a color scheme, or
switch an extension on; the change shows at once. "Restore Apple Music's look" removes the theme without a restart.

The same from a terminal, where the installer has put `ampify`:

```
ampify theme set glass --scheme Night     # use a theme
ampify theme clear                        # back to Apple Music's look
ampify market                             # what the Marketplace offers
ampify install sleep-timer                # install from the Marketplace
ampify restore                            # stop loading with Apple Music (ampify apply turns it back on)
ampify doctor                             # what is wrong, if something is
```

The Marketplace offers five themes (Greenroom, Glass, Artwork, Midnight, Hide chrome), four color palettes and a few
example extensions. Ampify itself adds a Now Playing button that opens Apple Music's own full-screen player, and an
Atmos leveler, off until you switch it on, that brings Dolby Atmos songs to the loudness of stereo ones without
moving the volume bar. Every command: [docs/cli.md](docs/cli.md). A longer walk-through:
[docs/getting-started.md](docs/getting-started.md).

## Make your own

| To make | Start with | Guide | Reference |
|---|---|---|---|
| A theme: colors and CSS | `ampify new theme my-theme` | [Themes in five minutes](docs/themes/guide.md) | [Theme format](docs/themes/format.md), [element names](docs/themes/names.md) |
| An extension: JavaScript | `ampify new extension my-extension` | [Extensions in five minutes](docs/extensions/guide.md) | [The Ampify API](docs/extensions/api.md), [manifest](docs/extensions/manifest.md) |
| An app: a page of its own | `ampify new app my-app` | [Extensions guide, "An app"](docs/extensions/guide.md#an-app) | [The Ampify API](docs/extensions/api.md) |
| A snippet: a small style | `ampify new snippet my-snippet` | [Themes guide, "A snippet"](docs/themes/guide.md#a-snippet) | [Theme format](docs/themes/format.md) |

`ampify dev <folder>` copies your work into Apple Music and reloads it at every save. Spicetify `color.ini` palettes
work as they are (`ampify theme import <github address>`). The [themes/](themes) and [examples/](examples) folders
hold working elements to read and copy, and [Publishing](docs/extensions/catalog.md) explains how to put yours in the
Marketplace.

## How it works

Apple Music for Windows is a native WinUI 3 app, not a web page, so Ampify cannot inject CSS the way Spicetify does.
Apple Music's files are also read-only. So a small launcher starts with Windows, waits for Apple Music, and loads
`ampify_kernel.dll` into it as it opens. The kernel translates your theme's CSS into XAML properties on Apple Music's
own controls, and runs extensions in a small JavaScript engine (QuickJS). Removing a theme puts every value back.
Details: [docs/how-it-works.md](docs/how-it-works.md).

Ampify never hooks FairPlay or anything related to DRM, and never records the audio output. After two launches in a
row that do not answer, the next one starts without Ampify (safe mode).

## In this repository

| Folder | What it holds |
|---|---|
| [src/](src) | The C++ code: the kernel loaded into Apple Music, the launcher, the installer, the `ampify` command line |
| [themes/](themes) | The themes, palettes and snippets |
| [extensions/](extensions), [apps/](apps) | What ships with Ampify: Now Playing, the Atmos leveler, the Marketplace |
| [examples/](examples) | Example extensions to learn from |
| [docs/](docs) | The documentation, for users and for authors |
| [tools/](tools), [schemas/](schemas) | The validator, the packer and the JSON schemas of manifests and catalogs |
| [tests/](tests), [bench/](bench) | Unit tests, and the bench that runs Ampify in a real Apple Music in CI |

The Marketplace's catalog lives in its own repository,
[ampify-catalog](https://github.com/Shirowwww/ampify-catalog).

## Build from source

Windows 10 or 11 x64, Visual Studio 2022 or later (C++ workload) and the Windows SDK:

```
pwsh -File scripts\build-msvc.ps1           # release build and unit tests, in build-msvc\release
build-msvc\release\ampify_tests.exe
```

Everything else (bench build, packaging, CI): [docs/development/building.md](docs/development/building.md).
Contributing: [CONTRIBUTING.md](CONTRIBUTING.md).

## Credits

Ampify builds on QuickJS-ng and on what AMExclusive learned about Apple Music's player. Full list:
[CREDITS.md](CREDITS.md). Apple Music is a trademark of Apple Inc. Ampify is not affiliated with Apple.
