---
title: Create
description: Make a theme, a snippet or an extension for Ampify, see it live in Apple Music, and share it in the catalog.
---

# Create for Ampify

A theme is a folder with a few text files. An extension is one JavaScript file (or TypeScript, compiled for you) and a manifest. You need Ampify
installed, Apple Music, and a terminal open in a working folder. Nothing to compile, no account to create.

1. **Start from a working element.**
   `ampify new theme my-theme` (or `extension`, `snippet`, `app`) writes a complete starter in the current folder.
   It already runs: you change something that works instead of starting from a blank page.

2. **See it live.**
   `ampify dev my-theme` copies it into Ampify, turns it on, and reloads it each time you save. Apple Music changes
   within a second, with no restart. Leave the terminal open.

3. **Find what to style.**
   Themes use stable names for Apple Music's zones: `.sidebar`, `.playbar`, `.lcd`, `.track-row`, `.card` and more than
   a hundred others ([the full list](themes/names.md)). For anything else, `ampify devtools` then `ampify inspect`
   show the live interface tree with each element's selector.

4. **Shape it.**
   Colors go in `colors.ini`, one section per color scheme. Shapes, spacing, hover effects, transitions and animations
   go in `user.css`, with CSS you already know. A theme can declare its own settings (a slider, a switch, a choice)
   that people change in Apple Music's settings. See [the theme format](themes/format.md).

5. **Or write behaviour.**
   Extensions use the `Ampify` API: the player, the track, notifications, menus, panels, storage, and calls to the web
   the manifest declares. Each permission is shown to people before they install. See
   [the extension guide](extensions/guide.md) and [the API](extensions/api.md).

6. **Check it.**
   `ampify validate my-theme` checks the manifest and the files with the same rules as the catalog.

7. **Share it.**
   `ampify publish my-theme` checks it again, then opens a pull request on the
   [public catalog](https://github.com/Shirowwww/ampify-catalog) for you, without cloning anything. Once it is merged,
   it shows in the Marketplace of every Ampify and in [the gallery](themes.html). The whole path, preview included,
   is in [five steps](extensions/catalog.md#propose-an-element-in-five-steps).
{: .steps}

## One way for each thing

Themes, snippets and extensions share their tools. When two kinds of element can do the same thing, they do it the
same way, so what you learn for one works for the others.

| To... | Use | Works in |
|---|---|---|
| Offer settings | The `settings` field of the manifest ([manifest](extensions/manifest.md#settings)): CSS reads them with `@setting` and `--setting-<key>`, JavaScript with `Ampify.config.get(key)` | Themes, snippets, extensions, apps |
| Translate the name, the description and the settings | The `translations` field of the manifest | Every element |
| Know where the playback bar is | `@setting bar-position`, set by the user for every theme, or by the theme's `"playbar"` | Themes, snippets |
| Follow the panels' corners | `--panel-radius` (and `--bar-radius`, `--lcd-radius` for the bar and the display) | Themes and snippets set it; snippets and extensions' panels follow it |
| Use the colours of the cover playing | `--art-1` to `--art-3` and their kin in CSS, `Ampify.color.playing()` in JavaScript | All |
| Draw bars that move with the music | `content: bars(n, shape, height)` | Themes, snippets |
| Blur, recolour or put glass on an element | `filter` and `backdrop` | Themes, snippets |
| Style a row, a toggle or a control by its state | `:hover`, `:active`, `:focus`, `:current`, `:playing`, `:checked`, `:selected`, `:disabled` | Themes, snippets |
| Mark a stretch of the progress line | `Ampify.ui.playbar.mark(id, from, to)` | Extensions |
| Build an interface | The nodes of `Ampify.ui`, or `Ampify.React` with `Ampify.components` (buttons, toggles, check boxes, sliders, fields, menus, tabs, progress bars, spinners, dividers...) | Extensions, apps |

[Theme format](themes/format.md#what-a-theme-tells-snippets-and-extensions) lists what a theme passes on to the other
elements, and [the API](extensions/api.md#55-interface-stable) lists every node and component.

## Coming from Spicetify

A Spicetify `color.ini` works as it is: put it in a theme folder and its colors dress Apple Music. The `user.css`
needs Ampify's names instead of Spotify's classes. [What carries over, and what does not](themes/spicetify.md).

## Licenses

Ampify is under the PolyForm Noncommercial License 1.0.0: free for any use except commercial, with credit kept. That
license does not reach what you make: themes, snippets, extensions and apps that only use Ampify's documented formats
and API are yours, under the license you choose, commercial or not. The starters made by
`ampify new` and the examples are under the MIT license, so you can copy from them freely.

## Go further

- [Make a theme in five minutes](themes/guide.md)
- [Theme format: every property and selector](themes/format.md)
- [Stable names of Apple Music's interface](themes/names.md)
- [Write an extension](extensions/guide.md) and [its manifest](extensions/manifest.md)
- [The command line](cli.md)
