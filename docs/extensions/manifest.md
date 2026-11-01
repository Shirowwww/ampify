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
| `kitApi` | yes | `MAJOR.MINOR`, for example `1.0` | Version of the targeted API. The major version must be supported. |
| `homepage` | no | `https://` URL | Project page |
| `license` | no | 1 to 64 characters | The author's license |
| `tags` | no | List of 5 at most, unique, each `[a-z0-9][a-z0-9-]{0,19}` | Short lowercase words (`dark`, `layout`, `glass`, `lyrics`) shown on the Marketplace card |
| `preview` | no | A `.png`, `.jpg` or `.jpeg` file name at the root of the folder, 2 MB at most | Picture shown on the Marketplace card. The file must exist. |
| `appVersions` | no | List of Apple Music versions, 16 at most | Versions where the element was tested. The kernel also applies its own list. |
| `permissions` | no | List of 32 at most, values from section 4 of [api.md](api.md) | What the element needs. Shown before activation. |
| `processes` | no | One or two values: `ui` (AppleMusic.exe), `engine` (AMPLibraryAgent.exe), no duplicate | Where the element runs. Default `["ui"]`: the interface only, once per launch. An element that acts on sound declares `engine`. With both, it runs twice and must avoid writing the same storage. |

## Fields by `kind`

| Field | Extension | App | Theme | Snippet | Rule |
|---|---|---|---|---|---|
| `main` | required | required | optional | forbidden | A `.js` script at the root of the folder |
| `type` | optional | optional | optional | n/a | `script` (default) or `module`: `main` is then an ES module, which can import other files of the folder ([api.md](api.md), section 3.6) |
| `icon` | n/a | optional | n/a | n/a | An `.svg` icon for the sidebar |
| `colors` | n/a | n/a | required, or `resources` | n/a | An `.ini` file of color schemes |
| `defaultScheme` | n/a | n/a | optional | n/a | Section of `colors.ini` used by default |
| `resources` | n/a | n/a | required, or `colors` | required, or a `user.css` in the folder | A `.xaml` resource dictionary |

Fields that do not apply to the `kind` are ignored by the kernel. The validator only checks the fields required for each
`kind`.

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
7. that the folder contains no symbolic link (the kernel refuses to import through a link).

`ampify validate` applies the same rules (`cli/element.cpp`), and the kernel checks them again at load time
(`core/manifest.cpp`), without relying on the validator. Unknown permissions are refused by the schema and by the kernel.

```bash
python3 tools/validate.py manifest examples/hello-extension/manifest.json
```

## How the format evolves

- `kitApi` is the format version. An incompatible change raises the major version.
- A field added in a minor version is optional. A manifest valid for the old version stays valid.
- A removed field first becomes "deprecated" in a minor version, with a warning in the log.
