# Your first extension

You need Ampify installed, Apple Music open, and a terminal in a working folder. The extensions in
[examples/](../../examples) are short: start from `hello-extension` or `react-panel`; `sleep-timer`, `keyboard-skip`,
`discord-presence` and `scrobbler` show the player, shortcuts and web services; `ui-palette` shows the grid, tabs and menu.

## 1. Create it

```bash
ampify new extension my-extension
```

| File | What it holds |
|---|---|
| `manifest.json` | Id, name, author, version, the permissions the code needs, its settings |
| `main.js` | The code, run by Ampify's JavaScript engine inside Apple Music |

The starter adds a button to the playback bar that shows the song playing in a Windows notification.

## 2. See it live

```bash
ampify dev my-extension
```

Leave the terminal open: the extension restarts at every save. Click the "i" button at the right of the playback bar
to see the notification.

## 3. Change the code

`main.js` calls the `Ampify` object:

```js
Ampify.ui.playbar.addButton({
  label: "What is playing?",
  icon: "", // a Segoe Fluent Icons glyph (Info)
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

`show-album` is a setting declared in `manifest.json`. It shows under the extension in Ampify's settings page of Apple
Music's settings, and changing it restarts the extension:

```json
"settings": [
  {"key": "show-album", "label": "Show the album", "type": "toggle", "default": true}
]
```

Each call needs its permission in the manifest: here `ui.playbar`, `player.read` and `notifications`. A call without
it throws `PermissionDenied`, and the log names the missing permission.

| To | Call | Permission |
|---|---|---|
| Add a button to the playback bar | `Ampify.ui.playbar.addButton({ label, icon, onClick })` | `ui.playbar` |
| Mark a stretch of the progress line for that button | `Ampify.ui.playbar.mark(id, from, to)` | `ui.playbar` |
| Add an entry to a song's, album's or playlist's menu | `Ampify.ui.menu.addItem("context", { label, onClick })` | `ui.menu` |
| Read the song playing | `Ampify.player.state()` | `player.read` |
| React when the song changes | `Ampify.player.on("trackchange", fn)` | `player.read` |
| Play, pause, skip | `Ampify.player.play()`, `pause()`, `next()` | `player.control` |
| Keep a value | `Ampify.storage.get(key, fallback)`, `Ampify.storage.set(key, value)` | `storage` |
| Call a web service | `Ampify.http.get(url)` | `network:<host>` |
| Show a notification | `Ampify.notify({ title, text })` | `notifications` |
| Show text in the user's language | `Ampify.i18n.define({ en: {...}, de: {...} })`, then `Ampify.i18n.t(key)` | none |

Everything else is in [the API reference](api.md). `ampify types` writes `ampify.d.ts` for completion in your editor.

To write it in TypeScript or JSX, keep the manifest's `"main": "main.js"` and put a `main.ts`, `main.tsx` or `main.jsx`
next to it. `ampify dev` and `ampify build` turn it into `main.js` with esbuild, which they find in the folder's
`node_modules` (`npm install --save-dev esbuild`), in npm's global folder (`npm install -g esbuild`) or on the PATH,
and say so when it is missing. In JSX, tags become
`Ampify.React.createElement` calls. `main.js` is rebuilt only when a `.ts`, `.tsx`, `.jsx`, `.js` or `.json` file of the folder is newer than it (a file
imported from outside the folder is not watched), and a folder without a `main.ts`, `main.tsx` or `main.jsx` is left
as it is.

## 4. Follow what it does

```bash
ampify logs --follow
ampify test my-extension
```

`ampify logs --follow` shows each `Ampify.log` line with the extension's id, its errors and refused calls.
`ampify test` runs `main.js` without Apple Music, with a fake player and network, prints each call, and fails when a
call lacks its permission. Check the result in Apple Music too.

## 5. Share it

Replace `your-github-username` in `manifest.json`, then:

```bash
ampify publish my-extension
```

It checks and tests the extension, then opens a pull request on the catalog. See [Publishing](catalog.md).

## An app

```bash
ampify new app my-app
ampify dev my-app
```

An app has a page of its own. The starter adds an entry at the bottom of Apple Music's sidebar that opens `/my-app`.
As in a Spicetify custom app, the page shows what the global `render()` returns, built with `Ampify.React`:

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

The building blocks (`stack`, `row`, `text`, `button`, `card`, `image`...) are drawn with Apple Music's own WinUI
controls, so the page follows the theme and the light or dark mode ([API, section 5.5](api.md#55-interface-stable)).
`Ampify.components` wraps them as React components: `Button`, `Toggle`, `Checkbox`, `Slider`, `TextField`,
`Dropdown`, `ProgressBar`, `Spinner`, `Divider`, `Grid`, `Tabs`, `Menu`, `Card` and a few more
([section 5.9](api.md#59-react-and-components-stable)).

## From Spicetify

| Spicetify | Ampify |
|---|---|
| `Spicetify.Player` | `Ampify.player` |
| `Spicetify.Playbar.Button` | `Ampify.ui.playbar.addButton` |
| `Spicetify.ContextMenu.Item` | `Ampify.ui.menu.addItem("context", ...)` |
| `Spicetify.showNotification` | `Ampify.notify` |
| `Spicetify.LocalStorage` | `Ampify.storage` |
| `Spicetify.React`, `Spicetify.ReactDOM` | `Ampify.React`, `Ampify.ReactDOM` |
| `Spicetify.ReactComponent` | `Ampify.components` |
| Waiting for `Spicetify.Player` | `await Ampify.ready` |

The full comparison is in [spicetify-parity.md](../spicetify-parity.md).
