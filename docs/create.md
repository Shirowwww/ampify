---
title: Create
description: Make a theme, a snippet or an extension for Ampify, see it live in Apple Music, and share it in the catalog.
---

# Create for Ampify

A theme is a folder with a few text files. An extension is one JavaScript file and a manifest. You need Ampify
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
   it shows in the Marketplace of every Ampify and in [the gallery](themes.html).
{: .steps}

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
