# Manifest

Every extension, theme, app or snippet is a folder with a `manifest.json` at its root. The folder name is the
element's id. Schema: [schemas/manifest.schema.json](../../schemas/manifest.schema.json).

```json
{
  "$schema": "../../schemas/manifest.schema.json",
  "id": "hello-extension",
  "name": "Hello",
  "version": "0.1.0",
  "kind": "extension",
  "author": "Shirowwww",
  "description": "Minimal example: writes to the log and keeps a launch counter.",
  "kitApi": "1.0",
  "main": "main.js",
  "permissions": ["storage"]
}
```

## Common fields

| Field | Required | Rule |
|---|---|---|
| `id` | yes | `[a-z0-9][a-z0-9-]{1,39}`, equal to the folder name |
| `name` | yes | 1 to 60 characters |
| `version` | yes | `MAJOR.MINOR.PATCH`, the element's own version |
| `kind` | yes | `extension`, `theme`, `app` or `snippet` |
| `author` | yes | A GitHub id |
| `description` | yes | 1 to 300 characters |
| `kitApi` | yes | `MAJOR.MINOR`, such as `1.0`: the API version it targets |
| `translations` | no | Name, description and setting labels in other languages (below) |
| `homepage` | no | An `https://` URL |
| `license` | no | 1 to 64 characters |
| `tags` | no | Up to 5 unique words, each `[a-z0-9][a-z0-9-]{0,19}` (`dark`, `glass`, `lyrics`) |
| `preview` | no | A `.png`, `.jpg` or `.jpeg` at the root of the folder, 2 MB at most, shown on the Marketplace card |
| `appVersions` | no | Up to 16 Apple Music versions the element was tested on |
| `permissions` | no | Up to 32 values from [the API](api.md#4-permissions), shown before activation |
| `processes` | no | `ui`, `engine`, or both. Default `["ui"]` ([API, section 2](api.md#2-processes)) |

## Fields by kind

| Field | Extension | App | Theme | Snippet | Rule |
|---|---|---|---|---|---|
| `main` | required | required | optional | forbidden | A `.js` file at the root |
| `type` | optional | optional | optional | | `script` (default) or `module` ([ES modules](api.md#31-several-files-es-modules-preview)) |
| `icon` | | optional | | | An `.svg` icon |
| `colors` | | | required, or `resources` | | An `.ini` file of color schemes |
| `defaultScheme` | | | optional | | The scheme used by default |
| `resources` | | | required, or `colors` | required, or a `user.css` | A `.xaml` resource dictionary |
| `settings` | optional | forbidden | optional | forbidden | Settings shown in Ampify's settings page (below) |

Files named in the manifest sit at the root of the folder (no subfolder, no `../`, no leading dot). The folder may hold
subfolders for files that `main` imports. A symbolic link anywhere in the folder is refused.

## Settings

An extension, a theme or a snippet can declare settings. Ampify's settings page shows them under the
extension's switch, or under the extensions for the active theme and the snippets switched on.

```json
"settings": [
  {"key": "show-album", "label": "Show the album", "type": "toggle", "default": true},
  {"key": "position", "label": "Position", "type": "choice", "options": ["Left", "Right"], "default": "Left"},
  {"key": "greeting", "label": "Greeting", "type": "text", "default": "Hello"},
  {"key": "size", "label": "Text size", "type": "number", "min": 8, "max": 32, "default": 14}
]
```

| Member | Rule |
|---|---|
| `key` | `[a-z0-9][a-z0-9-]{0,31}`, unique, not `enabled`. Stored as `extensions.<id>.<key>` (or `themes.<id>.<key>`) |
| `label` | 1 to 60 characters |
| `type` | `toggle`, `choice`, `text` or `number` |
| `default` | `true`/`false` for a toggle, one of the `options`, a one-line text of 200 characters at most, or a number within `min` and `max` |
| `options` | For a choice only: 2 to 20 different texts of 1 to 40 characters |
| `min`, `max` | For a number, optional |
| `group` | Optional, 1 to 40 characters. The heading the setting sits under; settings with the same `group` share it |
| `description` | Optional, 1 to 200 characters. One line shown under the label |

16 settings at most. Option values and text defaults are one line, with no space at either end. An extension reads
its settings with `Ampify.config.get(key)`; a theme with `var(--setting-<key>)` and `@setting`
([format](../themes/format.md#theme-settings)).

Without any `group`, the settings are listed one after the other, as before. With groups, the settings without one
come first, then each group under its heading, in the order its first setting is declared.

The card of an element with six settings or more also has a search box (it matches the label and the description,
whatever the case and the accents), a **Reset** per setting (its manifest `default`, off while the setting already has
it), a **Reset all**, and **Export** and **Import**. A reset takes the key out of `settings.txt`, so the setting
follows its `default` if a later version changes it. Export
writes `<ampify>\exports\<kind>-<id>.json` and copies the same text to the clipboard; Import reads the clipboard. The
file looks like this:

```json
{"format": "ampify-settings", "version": 1, "kind": "theme", "id": "night",
 "values": {"show-album": false, "size": 16}}
```

Each value keeps its type: `true` or `false` for a toggle, a number for a number, a text for a choice or a text. Import
refuses the whole file, and changes nothing, when it is bigger than 64 KB, is for another element or another kind, has
a key the element does not declare, or has a value of the wrong type or outside what the setting accepts. A key the
file leaves out keeps its value.

## Translations

`name`, `description` and labels are in English. `translations` gives them in the other languages Ampify speaks:
`fr`, `de`, `es`, `pt`, `ru`, `ja` and `zh`.

```json
"translations": {
  "fr": {"name": "À l'écoute", "description": "Un bouton qui ouvre le lecteur plein écran."},
  "de": {
    "name": "Läuft gerade",
    "description": "Eine Taste, die den Vollbild-Player öffnet.",
    "settings": {"position": "Position"},
    "options": {"Left": "Links", "Right": "Rechts"}
  }
}
```

| Member | Rule |
|---|---|
| language key | One of `fr`, `de`, `es`, `pt`, `ru`, `ja`, `zh`. Any other key is refused |
| `name`, `description` | Required, same lengths as the English ones |
| `settings` | Optional: a label per setting `key`, 1 to 60 characters |
| `options` | Optional: a text per choice value, 1 to 60 characters. The stored value stays the English one |
| `groups` | Optional: a heading per group name, 1 to 40 characters |
| `settingDescriptions` | Optional: a description per setting `key`, 1 to 200 characters |

The Marketplace and Ampify's settings page show the translation that matches the language Ampify uses, and English
otherwise. Ids, folder names and command-line messages never change with the language.

> [!NOTE]
> The public catalog's index leaves `translations` out, because the test builds that came before the 1.0.0 release
> refuse a catalog with a field they do not know; the archive's manifest keeps them.

## Validation

`ampify validate <folder>` and [tools/validate.py](../../tools/validate.py) check, with the same rules: the schema
(unknown fields refused), `id` equal to the folder name, the `kitApi` major version, the fields required by `kind`,
that declared files exist, that there is no symbolic link, and that each setting fits its `type`. Apple Music checks
everything again when it loads the element. A key written twice in one object is not refused, but `ampify validate`
and Apple Music's log warn about it: write each key once.

```bash
python3 tools/validate.py manifest examples/hello-extension/manifest.json
```
