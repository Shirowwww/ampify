# Make an extension in five minutes

You need Apple Music started with Ampify (`ampify launch`) and a terminal open in a working folder.

## 1. The starting extension

```bash
ampify new extension my-extension
```

The `my-extension` folder holds two files:

| File | What it does |
|---|---|
| `manifest.json` | The id, name, author, version, the permissions the code needs, and its settings |
| `main.js` | The code, run by Ampify's JavaScript engine inside Apple Music |

The extension already works: it adds a button to the playback bar, and a click shows the song playing in a Windows
notification.

## 2. See it in Apple Music

```bash
ampify dev my-extension
```

`ampify dev` copies the extension into the Ampify folder, switches it on, and restarts it at every save. Keep the
terminal open. The button, an "i" in a circle, appears at the right of the playback bar. Click it: Windows shows the
title, the artist and the album.

## 3. Change the code

`main.js` calls the `Ampify` object:

```js
Ampify.ui.playbar.addButton({
  label: "What is playing?",
  icon: "", // a Segoe Fluent Icons glyph (Info)
  onClick: () => {
    const { track } = Ampify.player.state();
    const album = Ampify.config.get("show-album") && track ? " - " + track.album : "";
    Ampify.notify({
      title: track ? track.title : "Nothing is playing",
      text: track ? track.artist + album : "Start a song in Apple Music.",
    });
  },
});
```

`show-album` is a setting that `manifest.json` declares:

```json
"settings": [
  {"key": "show-album", "label": "Show the album", "type": "toggle", "default": true}
]
```

Apple Music's Settings > Playback shows it under the extension, in the "Ampify" card. Switch it off: the extension
restarts, and the notification leaves the album out. A setting can also be a list of choices, a text or a number
([manifest.md](manifest.md), "Settings").

Each function needs a permission in `manifest.json`. Here `ui.playbar` adds the button, `player.read` reads the song and
`notifications` shows the notification. A function called without its permission throws a `PermissionDenied` error,
and the log names the missing permission.

What extensions use most:

| To | Call | Permission |
|---|---|---|
| Add a button to the playback bar | `Ampify.ui.playbar.addButton({ label, icon, onClick })` | `ui.playbar` |
| Add an entry to the menu of a song, an album or a playlist | `Ampify.ui.menu.addItem("context", { label, onClick })` | `ui.menu` |
| Read the song playing | `Ampify.player.state()` | `player.read` |
| React when the song changes | `Ampify.player.on("trackchange", fn)` | `player.read` |
| Play, pause, skip | `Ampify.player.play()`, `pause()`, `next()` | `player.control` |
| Keep a value between sessions | `Ampify.storage.get(key, fallback)`, `Ampify.storage.set(key, value)` | `storage` |
| Call a web service | `Ampify.http.get(url)` | `network:<host>` |
| Show a notification | `Ampify.notify({ title, text })` | `notifications` |

`onClick` of a menu entry receives `{ title }`, the name of the item the menu was opened on. The whole API, with the
status of each function, is in [api.md](api.md). For completion in your editor, `ampify types` writes `ampify.d.ts`
in the current folder.

## 4. Follow what it does

```bash
ampify logs --follow
```

Each `Ampify.log` line shows up with the extension's id, for example `[my-extension] my-extension started`, along with
its errors and refused calls.

```bash
ampify test my-extension
```

`ampify test` runs `main.js` without Apple Music, in the same JavaScript engine, with a fake player and network. It prints
the log and each call the code made, and fails when a call lacks its permission. A test that passes still needs a look
in Apple Music.

## 5. Check it and share it

```bash
ampify build my-extension
```

`ampify build` checks the manifest, writes `my-extension-0.1.0.zip` and prints the entry to add to the catalog, with
its SHA-256. Replace `your-github-username` in `manifest.json` first. Publishing is described in
[the catalog](catalog.md).

## An app

```bash
ampify new app my-app
ampify dev my-app
```

An app has a page of its own. The starter adds an entry at the bottom of Apple Music's sidebar. The entry opens the page
`/my-app`, titled with the name in `manifest.json`, with a counter and a button. As in a Spicetify custom app, the page
shows what the global `render()` function returns. It is built with `Ampify.React`, which gives `h` and the hooks
(`useState`, `useEffect`...):

```js
const { h, useState } = Ampify.React;

function Page() {
  const [count, setCount] = useState(0);
  return h("stack", { gap: 12 },
    h("text", null, "Clicked " + count + " time(s)."),
    h("button", { label: "Click me", variant: "primary", onClick: () => setCount(count + 1) }));
}

globalThis.render = () => h(Page);
```

The building blocks (`stack`, `row`, `text`, `button`, `card`, `image`...) are drawn with the same WinUI controls as
Apple Music, so the page takes the theme's colors and the light or dark mode. They are listed in [api.md](api.md),
section 5.5.

## Coming from Spicetify

| Spicetify | Ampify |
|---|---|
| `Spicetify.Player` | `Ampify.player` |
| `Spicetify.Playbar.Button` | `Ampify.ui.playbar.addButton` |
| `Spicetify.ContextMenu.Item` | `Ampify.ui.menu.addItem("context", ...)` |
| `Spicetify.showNotification` | `Ampify.notify` |
| `Spicetify.LocalStorage` | `Ampify.storage` |
| `Spicetify.React`, `Spicetify.ReactDOM` | `Ampify.React`, `Ampify.ReactDOM` |

Waiting for `Spicetify.Player` to exist becomes `await Ampify.ready`. The full comparison is in
[spicetify-parity.md](../spicetify-parity.md).
