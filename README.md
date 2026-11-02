<div align="center">

<img src="docs/assets/logo.svg" width="96" alt="">

# Ampify

**Themes, extensions and a Marketplace for Apple Music on Windows.**

Ampify does for the official Apple Music app what Spicetify does for Spotify. Change how it looks, add what it lacks,
and keep everything Apple Music already does: lossless, Dolby Atmos, lyrics, your library and sync.

[![Latest release](https://img.shields.io/github/v/release/Shirowwww/ampify?display_name=tag&sort=semver&style=flat-square&color=fa233b)](https://github.com/Shirowwww/ampify/releases/latest)
[![Downloads](https://img.shields.io/github/downloads/Shirowwww/ampify/total?style=flat-square&color=2ea043)](https://github.com/Shirowwww/ampify/releases)
![Windows 10 | 11](https://img.shields.io/badge/Windows-10%20%7C%2011-0078D6?logo=windows&style=flat-square)
[![License](https://img.shields.io/badge/license-GPL--3.0-blue?style=flat-square)](LICENSE)

### [Download](https://github.com/Shirowwww/ampify/releases/latest) &nbsp;·&nbsp; [Website](https://shirowwww.github.io/ampify/) &nbsp;·&nbsp; [Themes](https://shirowwww.github.io/ampify/themes.html) &nbsp;·&nbsp; [Docs](https://shirowwww.github.io/ampify/documentation.html)

<img src="docs/assets/banner.webp" width="100%" alt="Apple Music with the Glass, Greenroom, Dribbblish, Paper light, Catppuccin, Artwork, Rose gold and Tokyo Night themes">

</div>

## What it does

<table>
<tr>
<td width="50%" valign="top">

**🎨 Themes**<br>
Sixteen themes, light and dark: glass panels, the artwork of the song behind everything, a Spotify-style play bar,
Catppuccin, Dracula, Nord and more. Each one applies at once, without a restart, and a theme can declare its own
settings.

</td>
<td width="50%" valign="top">

**🧩 Extensions**<br>
Your Discord status with the cover and buttons, Last.fm scrobbling, a sleep timer, global shortcuts. Each extension
says which permissions it needs before you switch it on.

</td>
</tr>
<tr>
<td valign="top">

**🛍️ Marketplace**<br>
Click **Ampify** at the bottom of Apple Music's sidebar: themes, schemes, extensions and snippets, with previews,
installed and removed in one click. French or English, as Windows is.

</td>
<td valign="top">

**🎧 Built in**<br>
A Now Playing button that opens Apple Music's own full-screen player with karaoke lyrics, and an Atmos leveler that
brings Dolby Atmos songs to the loudness of stereo ones.

</td>
</tr>
<tr>
<td valign="top">

**🛠️ Made to be made**<br>
A theme is CSS on Apple Music's own elements, with GIFs and decorations. An extension is JavaScript with the `Ampify`
object. `ampify dev` reloads your work at every save, and `ampify publish` sends it to the Marketplace.

</td>
<td valign="top">

**🔒 Apple Music stays Apple Music**<br>
Nothing is written into Apple Music's package and copy protection is never touched. On an Apple Music version that was
not tested, Ampify stays off. Uninstalling puts everything back.

</td>
</tr>
</table>

<div align="center">
<table>
<tr>
<td align="center"><img src="docs/assets/screens/marketplace.png" height="240" alt="The Marketplace inside Apple Music"><br><sub>Marketplace</sub></td>
<td align="center"><img src="docs/assets/screens/settings-card.png" height="240" alt="The Ampify card in Apple Music's settings"><br><sub>Ampify in Apple Music's settings</sub></td>
</tr>
</table>

<sub>16 themes · 6 extensions · 3 snippets · Spicetify <code>color.ini</code> palettes work as they are ·
[how it compares to Spicetify and Cider](https://shirowwww.github.io/ampify/spicetify-parity.html)</sub>
</div>

## Install

Close Apple Music, then paste this line in PowerShell:

```powershell
irm https://raw.githubusercontent.com/Shirowwww/ampify/main/scripts/install.ps1 | iex
```

It downloads the latest release, checks its SHA-256 and installs it for your Windows account, with no administrator
rights. Open Apple Music as usual, from its own icon: Ampify loads with it, now and every time it starts.

| | |
|---|---|
| **Or** | Download `ampify-<version>-windows-x64.zip` from the [latest release](https://github.com/Shirowwww/ampify/releases/latest), extract it and run `ampify_setup.exe` |
| **Needs** | Windows 10 or 11 x64, Apple Music 1.1792 or 1.1540 from the Microsoft Store |
| **Data** | `%USERPROFILE%\Ampify` (settings, themes, extensions, logs) |
| **Turn off** | `ampify restore`, or uninstall from Settings > Apps > Installed apps > Ampify for Apple Music |
| **Guide** | [Getting started](https://shirowwww.github.io/ampify/getting-started.html) |

<details>
<summary><b>Windows Defender or SmartScreen warns about a file</b></summary>
<br>

The programs are not code-signed yet, and the launcher loads Ampify's DLL into Apple Music, which is also what some
malware does to other programs. Each release is scanned with Microsoft Defender before it is published, and its
SHA-256 is on the release page. If a warning shows anyway, it is a false positive: allow the file Defender names, or
build Ampify from source (below). Never switch protection off.

</details>

## Use it

Pick a theme in the Marketplace, or from a terminal, where the installer has put `ampify`:

```
ampify theme set glass --scheme Night     # use a theme
ampify theme clear                        # back to Apple Music's look
ampify install sleep-timer                # install from the Marketplace
ampify report                             # an anonymous diagnostic report, to attach to an issue
```

Every command: [the command line](https://shirowwww.github.io/ampify/cli.html).

## Make your own

| To make | Start with | Guide | Reference |
|---|---|---|---|
| A theme | `ampify new theme my-theme` | [Themes in five minutes](https://shirowwww.github.io/ampify/themes/guide.html) | [Format](https://shirowwww.github.io/ampify/themes/format.html), [element names](https://shirowwww.github.io/ampify/themes/names.html) |
| A snippet | `ampify new snippet my-snippet` | [A snippet](https://shirowwww.github.io/ampify/themes/guide.html#a-snippet) | [Format](https://shirowwww.github.io/ampify/themes/format.html) |
| An extension | `ampify new extension my-extension` | [Extensions in five minutes](https://shirowwww.github.io/ampify/extensions/guide.html) | [API](https://shirowwww.github.io/ampify/extensions/api.html), [manifest](https://shirowwww.github.io/ampify/extensions/manifest.html) |
| An app | `ampify new app my-app` | [An app](https://shirowwww.github.io/ampify/extensions/guide.html#an-app) | [API](https://shirowwww.github.io/ampify/extensions/api.html) |

`ampify dev <folder>` copies your work into Apple Music and reloads it at every save, `ampify devtools` shows the
element tree of the page you are on, and `ampify publish` opens the pull request to the
[catalog](https://github.com/Shirowwww/ampify-catalog). The [themes/](themes) and [examples/](examples) folders hold
working elements to read and copy.

## Documentation

<table align="center">
<tr><th>Start</th><th>Create</th><th>Understand</th><th>Contribute</th></tr>
<tr valign="top">
<td><a href="https://shirowwww.github.io/ampify/getting-started.html">Getting started</a><br><a href="https://shirowwww.github.io/ampify/cli.html">Command line</a><br><a href="https://shirowwww.github.io/ampify/themes.html">Theme gallery</a></td>
<td><a href="https://shirowwww.github.io/ampify/create.html">Create</a><br><a href="https://shirowwww.github.io/ampify/themes/spicetify.html">From Spicetify</a><br><a href="https://shirowwww.github.io/ampify/extensions/catalog.html">Publishing</a></td>
<td><a href="https://shirowwww.github.io/ampify/how-it-works.html">How it works</a><br><a href="https://shirowwww.github.io/ampify/spicetify-parity.html">Spicetify and Cider</a><br><a href="CHANGELOG.md">Changelog</a></td>
<td><a href="CONTRIBUTING.md">Contributing</a><br><a href="https://shirowwww.github.io/ampify/development/building.html">Building</a><br><a href="https://github.com/Shirowwww/ampify/issues">Issues</a></td>
</tr>
</table>

**Build from source:** Windows 10 or 11 x64, Visual Studio 2022 or later (C++ workload) and the Windows SDK.
`pwsh -File scripts\build-msvc.ps1` builds everything and runs the tests in `build-msvc\release`.

## How it works

Apple Music for Windows is a native WinUI 3 app, not a web page, so Ampify cannot inject CSS the way Spicetify does,
and Apple Music's files are read-only. A small launcher starts with Windows, waits for Apple Music, and loads
`ampify_kernel.dll` into it as it opens. The kernel translates your theme's CSS into XAML properties on Apple Music's
own controls and runs extensions in a small JavaScript engine (QuickJS). Removing a theme puts every value back, and
after two launches in a row that do not answer, the next one starts without Ampify. Details:
[how it works](https://shirowwww.github.io/ampify/how-it-works.html).

<details>
<summary><b>What is in this repository</b></summary>
<br>

| Folder | What it holds |
|---|---|
| [src/](src) | The C++ code: the kernel loaded into Apple Music, the launcher, the installer, the `ampify` command line |
| [themes/](themes) | The themes and snippets |
| [extensions/](extensions), [apps/](apps) | What ships with Ampify: Now Playing, the Atmos leveler, the Marketplace |
| [examples/](examples) | Example extensions to learn from (MIT) |
| [docs/](docs) | The documentation and the website |
| [knowledge/](knowledge) | What was learned about Apple Music's internals |
| [tools/](tools), [schemas/](schemas) | The validator, the packer and the JSON schemas |
| [tests/](tests), [bench/](bench) | Unit tests, and the bench that runs Ampify in a real Apple Music in CI |

</details>

## Credits and license

Ampify is free software under the [GNU GPL v3](LICENSE). What you make for it is yours: as an additional permission
under section 7, themes, snippets, extensions and apps that only use Ampify's documented formats and `Ampify` API are
not covered by the GPL, and their authors choose their own license. The [examples](examples) and the starters made by
`ampify new` are under the [MIT license](examples/LICENSE).

Ampify builds on QuickJS-ng and on what AMExclusive learned about Apple Music's player; full list in
[CREDITS.md](CREDITS.md). Apple Music is a trademark of Apple Inc. Ampify is not affiliated with Apple.
