# Community catalog and Marketplace

The catalog is the public list of installable extensions, themes, apps and snippets. It lives in its own public
repository, [Shirowwww/ampify-catalog](https://github.com/Shirowwww/ampify-catalog), with the archives and the readable
sources of every element. The tests of this repository check it when a clone of it sits next to this one. The schema is in [schemas/catalog.schema.json](../../schemas/catalog.schema.json). The format of the
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
| `tags` | no | A copy of the manifest tags: 5 at most, unique, each `[a-z0-9][a-z0-9-]{0,19}`. Shown on the card. |
| `preview` | no | An `https://` URL ending in `.png`, `.jpg` or `.jpeg`, 512 characters at most. The page shows it only when its host is the catalog's own host or the host of the archive `url`; any other address is left out. A catalog read from a local file only has the archive `url` host. |
| `permissions` | no | A copy of the manifest permissions. It lets the consent be shown before the download. |
| `translations` | no | A copy of the manifest's `translations` (`fr` only: `{"fr": {"name": "...", "description": "..."}}`, same length rules as `name` and `description`). The Marketplace shows it when Windows is in French. Not written for now: Ampify 1.1.0 refuses the whole catalog for a field it does not know, so `tools/pack.py` and `ampify build` leave it out and the archive's manifest keeps it. |

A field this list does not name is left aside (Ampify 1.1.0 and older refused the catalog instead), so the catalog can
grow without cutting off the versions that read it.

The permission `kit.manage` is refused in the catalog: only the Marketplace that ships with Ampify holds it.

Next to the catalog, `versions.json` lists the Apple Music versions tested after a release, with the verified
exports of each. `catalog.json` pins its address and SHA-256. `ampify versions update` reads it (see [../cli.md](../cli.md)).

## Publish an element

Three steps, with no clone of any repository:

1. **Make it.** `ampify new <kind> <id>` gives a starter. For an update, raise `version` in the manifest.
2. **Test it.** `ampify test <folder>` runs `main.js` against a fake host. `ampify validate <folder>` checks the manifest.
3. **Publish it.** `ampify publish <folder>` (`--dry-run` to only see the plan). It runs the checks of `validate` and
   `test`, then, with the GitHub CLI (`gh`) installed and signed in, forks ampify-catalog, puts the folder in
   `sources/<id>/` on a branch `add-<id>-<version>`, and opens the pull request with the name, kind, version,
   permissions and description filled in. It prints the address of the pull request. Without `gh`, it prints the steps
   to follow in the browser. See [../cli.md](../cli.md).

The pull request only adds or changes `sources/<id>/`. It does not touch `packages/` or `catalog.json`.

**Review.** A check runs on every pull request (it uses `tools/validate.py` and `tools/pack.py` of this repository) and
comments with the result. It refuses an element when its manifest is not valid, when its `version` is not higher than
the one in the catalog, when its code calls an API whose permission is not declared (a heuristic that reads the code),
when a script looks minified or obfuscated (a line of more than 400 characters, names like `_0x1f3a`), or when the pull
request changes `packages/` or `catalog.json`. A person then reads the code before the merge. This is the main protection
against a malicious element.

**After the merge.** A bot (`github-actions[bot]`) packs each changed element with `tools/pack.py`, writes
`packages/<id>-<version>.zip` and the preview picture next to it, replaces the entry in `catalog.json` (address
`https://raw.githubusercontent.com/Shirowwww/ampify-catalog/main/packages/<id>-<version>.zip`, SHA-256 of the archive),
validates the catalog and commits it to `main`. An archive never changes: if a version already published is changed
without a new `version`, the bot stops with an error. Removing an element is done by a maintainer.

By hand, without `ampify publish`: put the folder in `sources/<id>/` and open the pull request yourself; the bot does the
rest. `ampify build <folder>` still writes a zip and its catalog entry for a catalog of your own (`--catalog`); the
public catalog's archives are always packed by the bot, so publish there with `tools/pack.py` only.

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
- The catalog is read once per page opening. A "Refresh" button reads it again, along with what is installed and the
  theme in use: a theme changed elsewhere while the page is open (`ampify theme set`, `settings.txt`) shows after it.
- The page reads everything from `ampify market --json`: `catalogOk`, the `catalog` entries, the `installed` elements
  (with `valid`, `enabled`, a local `preview`, and for a theme its `schemes` and `defaultScheme`), the active `theme`
  and its `scheme`. Entries and installed elements carry `translations` when their manifest has it; the page shows
  `translations[Ampify.i18n.language]` for the name and description (cards, detail view and search), else the English ones.

**Screen**

1. **Tabs and cards.** Tabs Themes, Extensions, Apps, Snippets and Installed, a search field (names, authors,
   descriptions, tags) and "Refresh". The cards stand three to a row, twelve to a page: the preview picture (or a band
   with the icon of the kind), name, author and version, two lines of description, the state and tags, the permissions
   in one line, the main action and "Details". On the Themes and Installed tabs, a bar names the theme in use, with its
   scheme menu and "Use Apple Music's look".
2. **Detail.** Name, id, author (link to the GitHub profile), version, license, project page (`homepage`), full
   description. The **permissions** are listed with a plain sentence for each (for example `network:api.example.com`:
   "sends requests to api.example.com"); the ones an update adds are marked "new in version ...".
3. **Actions** by state: "Install"; "Update"; "Turn on" or "Turn off" and "Uninstall"; for a theme, "Use" and the
   choice of scheme, plus "Use Apple Music's look". Destructive actions ("Uninstall") ask for confirmation. A backup is
   made first, as on the command line. The status line under the tabs says what the last action did, or ampify's error
   as it is.

**Rules**

- **Nothing installs without a click in the page**, and the permissions are shown before that click.
- **The same engine as `ampify install`.** HTTPS download, SHA-256 checked before any extraction, file list check,
  symbolic links refused, manifest compared with the catalog. The page reimplements nothing. It calls `ampify.exe`.
- **An element that asks for permissions is installed off.** The page then opens its detail view, where switching it
  on is a second click under the list of its permissions. An element that asks for none is installed and put on (a
  theme put in use) in one click.
- **A card never grants a permission.** An action that would ("Turn on", "Use", or an "Update" that adds permissions
  to an element that is on) ends with an ellipsis on the card and opens the detail view instead.
- **Nothing is run during the install.** The code only runs after activation, and only with the permissions the user saw.
- **An update keeps the settings.** An element that was on stays on, unless its new version asks for a new permission
  that the user refuses.
- **No network beyond the catalog and the releases.** The page loads no image or script from another address. An icon
  comes from the archive. A preview of an installed element is its own file; a preview in the catalog is used only when
  it is on the host of the catalog or of the archive.

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
   display is checked on a PC ([testing-on-a-pc.md](../development/testing-on-a-pc.md)).

**Open questions**

- An icon per element: SVG only, or PNG too (heavier, more cases to check).

## Tests

```bash
python3 -m unittest discover -s tools -v
```

The tests cover the repository's catalog, valid and invalid entries, manifests, and the schema keywords that the
validator refuses.
