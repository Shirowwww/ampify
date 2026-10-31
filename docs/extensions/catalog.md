# Community catalog and Marketplace

The catalog is the public list of installable extensions, themes, apps and snippets. It lives in its own public
repository, [Shirowwww/ampify-catalog](https://github.com/Shirowwww/ampify-catalog), with the archives and the readable
sources of every element. [catalog/](../../catalog) in this repository holds the same files, which the tests read; the
two stay identical. The schema is in [schemas/catalog.schema.json](../../schemas/catalog.schema.json). The format of the
elements is in [manifest.md](manifest.md).

## Format

```json
{
  "catalogVersion": 1,
  "extensions": [
    {
      "id": "hello-extension",
      "name": "Hello",
      "version": "0.1.0",
      "kind": "extension",
      "author": "Shirowwww",
      "description": "Minimal example: writes to the log and keeps a launch counter.",
      "kitApi": "1.0",
      "url": "https://example.com/releases/hello-extension-0.1.0.zip",
      "sha256": "0000000000000000000000000000000000000000000000000000000000000000",
      "permissions": ["storage"]
    }
  ]
}
```

The example above is not in the catalog: its SHA-256 is a placeholder.

| Field | Required | Rule |
|---|---|---|
| `id` | yes | Unique in the catalog. Same pattern as in the manifest. |
| `name`, `version`, `kind`, `author`, `description`, `kitApi` | yes | Same rules as in the manifest. The catalog repeats the values. |
| `url` | yes | A `.zip` archive on `https://`. Its content is the element's folder, with `manifest.json` at the root. |
| `sha256` | yes | SHA-256 of the archive, lowercase, 64 characters. |
| `homepage`, `license` | no | Same rules as in the manifest. |
| `permissions` | no | A copy of the manifest permissions. It lets the consent be shown before the download. |

The permission `kit.manage` is refused in the catalog: only the Marketplace that ships with Ampify holds it.

Next to the catalog, `catalog/versions.json` lists the Apple Music versions tested after a release, with the verified
exports of each. `catalog.json` pins its address and SHA-256. `ampify versions update` reads it (see [../cli.md](../cli.md)).

## Publish an element

1. Prepare the element's folder and check its manifest:
   `python3 tools/validate.py manifest <folder>/manifest.json` (or `ampify validate <folder>`).
2. Create the `.zip` archive of the folder, without the parent folder, and publish it on `https://` (for example in a
   GitHub release). `ampify build <folder>` does this and prints the catalog entry. Publish with one tool only:
   `tools/pack.py` writes a compressed zip with a different SHA-256.
3. Compute its hash: `sha256sum <archive>.zip` (or `certutil -hashfile <archive>.zip SHA256` on Windows).
4. Add the entry to the catalog in a pull request.
5. Validate the catalog: `python3 tools/validate.py catalog catalog/catalog.json`.
6. A person reviews the code before the merge. This is the main protection against a malicious element.

An update is a new entry with a new `version`, a new `url` and a new `sha256`. It goes through the same review. Removing
an element is also done through a pull request.

## Trust

The sandbox (see [api.md](api.md)) limits what an element can do to the permissions it declared. It does not make the
element safe: a granted permission is a real permission. The catalog therefore follows these rules:

- it lists only code that a maintainer has reviewed;
- it pins each archive by its SHA-256. An archive changed after the fact is refused at install;
- it shows the permissions before the install;
- it does not update an element without the user's agreement;
- it gives no access to native code. Native extensions stay built into the kernel (see [api.md](api.md), section 2).

## What install checks

`ampify install` (and the Marketplace page, which calls it) checks, in this order:

1. The id is valid and is in the catalog.
2. The address is `https://`. A remote catalog cannot point to a local file.
3. The name, author, description and permissions are shown. Without `--yes`, the answer must be "y".
4. The archive is downloaded. Its SHA-256 must be the catalog's, otherwise it is thrown away.
5. The list of files in the archive is checked: no absolute path, no `..`, no `\`, no control character. A single doubtful
   entry refuses everything.
6. The archive is extracted to a work folder. A symbolic link in the result refuses everything.
7. The manifest is validated: id, kind and version must match the catalog.
8. Only then is the folder put in place. The old version stays in place until it succeeds.

Nothing downloaded is run during the install. An installed element is off. You switch it on with
`ampify extension enable`, which shows its permissions.

## The Marketplace page

The Marketplace page inside Apple Music (the "Ampify" entry at the bottom of the sidebar) browses, installs, updates and removes extensions, themes, apps
and snippets without a terminal. A person who does not know what a terminal is should be able to install a theme in
three clicks, and see what an element can do before each install. It shows and triggers the same operations as the
command line, and bypasses no check.

**Data**

- Source: the same catalog as the command line (`catalog.url` in the settings, by default the `catalog.json` of
  Shirowwww/ampify-catalog). The last catalog read is kept in `cache\catalog.json`.
- Offline or remote catalog unavailable: the page shows the cached copy and says so at the top.
- Invalid catalog: the page refuses to display it and shows the reasons, like `ampify validate`. It does not keep a partial
  list.
- The catalog is read once per page opening. A "Refresh" button reads it again.

**Screen**

1. **List.** One row per element: name, author, kind (extension, theme, app, snippet), version, a one-line description,
   and a state: not installed, installed, update available, enabled, invalid. Search by text, filter by kind.
2. **Detail.** Name, id, author (link to the GitHub profile), version, license, project page (`homepage`), full
   description. The **permissions** are listed with a plain sentence for each (for example `network:api.example.com`:
   "sends requests to api.example.com").
3. **Actions** by state: "Install"; "Update" (shows the new permissions before confirming); "Enable" or "Disable" and
   "Remove"; for a theme, "Use this theme" and the choice of scheme, plus "Restore Apple Music's look". Destructive
   actions ("Remove") ask for confirmation. A backup is made first, as on the command line.

**Rules**

- **Nothing installs without a click in the page**, and the permissions are shown before that click.
- **The same engine as `ampify install`.** HTTPS download, SHA-256 checked before any extraction, file list check,
  symbolic links refused, manifest compared with the catalog. The page reimplements nothing. It calls `ampify.exe`.
- **An installed element is off.** Switching it on is a second click, on the detail view, which shows its permissions
  again.
- **Nothing is run during the install.** The code only runs after activation, and only with the permissions the user saw.
- **An update keeps the settings.** An element that was on stays on, unless its new version asks for a new permission
  that the user refuses.
- **No network beyond the catalog and the releases.** The page loads no image or script from another address. An icon
  comes from the archive.

**States and messages**

| Situation | Display |
|---|---|
| Loading the catalog | A progress indicator, not an empty list |
| Empty catalog | "The catalog is empty for now." |
| No network, cache present | The cached list, with a banner "Offline: last read on ..." |
| No network, no cache | "Can't read the catalog. Try again when you're online." |
| SHA-256 mismatch | "The downloaded file does not match the catalog. Nothing was installed." |
| Archive refused (unsafe path) | "This archive contains a file outside its folder. Nothing was installed." |
| API version not supported | The element is shown, the "Install" action is disabled, the reason is written |
| Apple Music version not tested | A banner on the detail view: "Not tested with this version of Apple Music. Ampify stays inactive." |

**Accessibility.** Full keyboard navigation, an accessible name on every button, announced states ("installed",
"disabled"), and enough contrast in light and dark mode (colors come from the resources of the default theme).

**Test criteria**

1. Install, enable, disable and remove an extension from the page, with no terminal. The result matches `ampify install`,
   `ampify extension enable`, `ampify extension disable` and `ampify remove`.
2. A catalog whose archive is changed by one byte is refused with the message of the table, and nothing is written.
3. The permissions on the detail view are exactly those of `ampify extension enable`.
4. The page closes and reopens without reading the network again if the cache is recent.
5. The logic of the page (filters, states, messages) is tested in `tools/test_examples.mjs` with a fake `Ampify`. The
   display is checked on a PC, with the test kit.

**Open questions**

- An icon per element: SVG only, or PNG too (heavier, more cases to check).

## Tests

```bash
python3 -m unittest discover -s tools -v
```

The tests cover the repository's catalog, valid and invalid entries, manifests, and the schema keywords that the
validator refuses.
