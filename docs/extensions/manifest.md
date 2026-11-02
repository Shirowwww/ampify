# Manifest format

Each extension, theme, app or snippet is a folder with a `manifest.json` file at its root. The folder name is the id of
the element. The schema is in [schemas/manifest.schema.json](../../schemas/manifest.schema.json). The API these files
use is described in [api.md](api.md).

## Example

The folder [examples/hello-extension](../../examples/hello-extension/) is a complete example:

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

| Field | Required | Rule | Purpose |
|---|---|---|---|
| `id` | yes | `[a-z0-9][a-z0-9-]{1,39}`, equal to the folder name | Stable id. It is used for setting keys. |
| `name` | yes | 1 to 60 characters | Name shown in the interface |
| `version` | yes | `MAJOR.MINOR.PATCH`, for example `1.2.0` | Version of the element, not of the API |
| `kind` | yes | `extension`, `theme`, `app` or `snippet` | What the element brings |
| `author` | yes | A GitHub id | Author shown in the catalog |
| `description` | yes | 1 to 300 characters | Summary shown in the catalog |
| `translations` | no | Object keyed by language, `fr` only for now (section "Translations") | Name and description shown instead of the English ones when Windows is in that language |
| `kitApi` | yes | `MAJOR.MINOR`, for example `1.0` | Version of the targeted API. The major version must be supported. |
| `homepage` | no | `https://` URL | Project page |
| `license` | no | 1 to 64 characters | The author's license |
| `tags` | no | List of 5 at most, unique, each `[a-z0-9][a-z0-9-]{0,19}` | Short lowercase words (`dark`, `layout`, `glass`, `lyrics`) shown on the Marketplace card |
| `preview` | no | A `.png`, `.jpg` or `.jpeg` file name at the root of the folder, 2 MB at most | Picture shown on the Marketplace card. The file must exist. |
| `appVersions` | no | List of Apple Music versions, 16 at most | Versions where the element was tested. The kernel also applies its own list. |
| `permissions` | no | List of 32 at most, values from section 4 of [api.md](api.md) | What the element needs. Shown before activation. |
| `processes` | no | One or two values: `ui` (AppleMusic.exe), `engine` (AMPLibraryAgent.exe), no duplicate | Where the element runs. Default `["ui"]`: the interface only, once per launch. An element that acts on sound declares `engine`. With both, it runs twice and must avoid writing the same storage. |

## Translations

`name` and `description` are in English. An element can also give them in the languages Ampify speaks, which is French
only for now:

```json
"translations": {
  "fr": {"name": "À l'écoute", "description": "Un bouton de la barre de lecture qui ouvre le lecteur plein écran."}
}
```

| Member | Rule |
|---|---|
| language key | `fr`. Any other key is refused. |
| `name` | Required, 1 to 60 characters, like `name` |
| `description` | Required, 1 to 300 characters, like `description` |

The Marketplace (cards and detail view), the Ampify card in Apple Music's Settings and `ampify market --json` use the
translation that matches Windows' display language, and the English `name` and `description` when there is none. The
catalog entry repeats `translations` (see [catalog.md](catalog.md)). The id, the folder name and the messages of the
command line never change with the language.

## Fields by `kind`

| Field | Extension | App | Theme | Snippet | Rule |
|---|---|---|---|---|---|
| `main` | required | required | optional | forbidden | A `.js` script at the root of the folder |
| `type` | optional | optional | optional | n/a | `script` (default) or `module`: `main` is then an ES module, which can import other files of the folder ([api.md](api.md), section 3.6) |
| `icon` | n/a | optional | n/a | n/a | An `.svg` icon for the sidebar |
| `colors` | n/a | n/a | required, or `resources` | n/a | An `.ini` file of color schemes |
| `defaultScheme` | n/a | n/a | optional | n/a | Section of `colors.ini` used by default |
| `resources` | n/a | n/a | required, or `colors` | required, or a `user.css` in the folder | A `.xaml` resource dictionary |
| `settings` | optional | forbidden | forbidden | forbidden | Settings shown in the Ampify card (next section) |

Fields that do not apply to the `kind` are ignored by the kernel. The validator only checks the fields required for each
`kind`.

## Settings

An extension can list its settings. The "Ampify" card, in Apple Music's Settings > Playback, shows them under the
extension's switch, and `Ampify.config.get(key)` gives their value: the user's choice, else the `default` below, with its
type (a boolean for a toggle, a number for a number). A change in the card restarts the extension, which reads its
settings again.

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
| `key` | `[a-z0-9][a-z0-9-]{0,31}`, unique, other than `enabled`. The value is stored as `extensions.<id>.<key>` in `settings.txt` |
| `label` | 1 to 60 characters, shown in the card |
| `type` | `toggle`, `choice`, `text` or `number` |
| `default` | `true` or `false` for a toggle, one of the `options` for a choice, a text of 200 characters at most on one line, or a number between `min` and `max` |
| `options` | A choice's values, and only a choice's: 2 to 20 different texts of 1 to 40 characters |

`settings.txt` keeps one value per line and trims the spaces around it, so an option or a text default must be on one
line, without spaces at either end.
| `min`, `max` | A number's bounds, optional. A number typed outside them is not kept |

There are 16 settings at most. A text or a number is written when the field is left or when Enter is pressed.

Files declared in the manifest are at the root of the folder. A subfolder, a relative path like `../x.js`, or a name that
starts with a dot is refused in these fields. The folder can contain subfolders for the files that `main` imports
(`import x from "./lib/x.js"`). A symbolic link, anywhere in the folder, is refused.

## What the validator checks

[tools/validate.py](../../tools/validate.py) checks:

1. the schema: types, patterns, unknown fields refused;
2. the file name, which must be `manifest.json`;
3. `id`, equal to the folder name;
4. the major version of `kitApi`;
5. the fields required by `kind` (table above);
6. that each declared file is present in the folder;
7. that the folder contains no symbolic link (the kernel refuses to import through a link);
8. that each setting's `default`, `options`, `min` and `max` fit its `type`, and that no key is used twice.

`ampify validate` applies the same rules (`src/cli/element.cpp`), and the kernel checks them again at load time
(`src/core/manifest.cpp`), without relying on the validator. Unknown permissions are refused by the schema and by the kernel.

```bash
python3 tools/validate.py manifest examples/hello-extension/manifest.json
```

## How the format evolves

- `kitApi` is the format version. An incompatible change raises the major version.
- A field added in a minor version is optional. A manifest valid for the old version stays valid.
- A removed field first becomes "deprecated" in a minor version, with a warning in the log.
