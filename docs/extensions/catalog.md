# Publishing

The Marketplace lists what is in the public catalog,
[Shirowwww/ampify-catalog](https://github.com/Shirowwww/ampify-catalog): its `catalog.json`, the archives, and the
readable sources of every element.

## Publish an element

```bash
ampify validate my-theme       # the manifest and files
ampify test my-extension       # main.js against a fake host (extensions and apps)
ampify publish my-theme        # checks again, then opens the pull request
```

`ampify publish` needs the [GitHub CLI](https://cli.github.com) (`gh`), installed and signed in. It forks
ampify-catalog (or reuses your fork), puts the folder in `sources/<id>/` on a branch `add-<id>-<version>`, and opens
the pull request with the name, kind, version, permissions and description filled in. You need no clone and no `git`.
Without `gh`, it prints the steps to do in the browser. `--dry-run` shows the plan without any network call.

For an update, raise `version` in the manifest and publish again.

## Review

- A check runs on every pull request and comments with the result. It refuses an element whose manifest is not valid,
  whose `version` is not higher than the catalog's, whose code calls an API without declaring its permission, whose
  script looks minified or obfuscated, or whose pull request touches `packages/` or `catalog.json`.
- A maintainer then reads the code before merging. That review is the main protection against a malicious element.
- After the merge, a bot packs the element with `tools/pack.py`, writes `packages/<id>-<version>.zip`, its preview and
  its README, and updates `catalog.json` with the archive's address and SHA-256 (a later version of a listed element
  goes to its `newer` list, see below). A published archive never changes.

A `README.md` at the root of the element's folder is shown on its page in the Marketplace, below what it can do: what
it is for, how to use it, its settings. It is published next to the archive as `packages/<id>-<version>.md`, with no
field in `catalog.json`. The page shows headings, paragraphs, lists, bold text, code and links; images are left out,
the preview is the picture.

`ampify build <folder>` writes a zip and its catalog entry for a catalog of your own (`--catalog`). The public
catalog's archives are always packed by the bot.

## Catalog format

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

| Field | Required | Rule |
|---|---|---|
| `id`, `name`, `version`, `kind`, `author`, `description`, `kitApi` | yes | As in the [manifest](manifest.md). `id` is unique in the catalog |
| `url` | yes | A `.zip` on `https://`, with the element's folder content and `manifest.json` at its root |
| `sha256` | yes | SHA-256 of the archive, 64 lowercase characters |
| `homepage`, `license`, `tags`, `permissions` | no | Copies of the manifest fields |
| `preview` | no | An `https://` `.png`/`.jpg`/`.jpeg` address on the catalog's host or the archive's host |
| `translations` | no | A copy of the manifest's. Not written by `tools/pack.py` nor `ampify build` (see Compatibility) |
| `minAmpify` | no | The oldest Ampify release that takes this version, like `1.3.0`. Read from Ampify 1.3.0 on |
| `newer` | no | Later versions of the same element (see below) |

### Several versions of one element

An entry can list later versions of its element in `newer`. Each item is a full entry with the same `id`, `kind` and
`author`, a higher `version`, and a `minAmpify`. Ampify 1.3.0 and later take the newest version whose `minAmpify` they
reach, and leave out an element when they reach none. Ampify 1.2.1 and older do not know `newer` and read only the
entry itself, so the entry keeps the version they understand.

```json
{
  "id": "glass", "version": "1.2.1", "url": "https://.../glass-1.2.1.zip", "...": "...",
  "newer": [
    {"id": "glass", "version": "1.3.0", "minAmpify": "1.3.0", "url": "https://.../glass-1.3.0.zip", "...": "..."}
  ]
}
```

The bot writes this: the first version of an element becomes its entry, and every later one goes to `newer`, with
the release of Ampify whose `tools/pack.py` packed it as `minAmpify`. Old archives stay where they are.

`kit.manage` is refused in a catalog entry, by the clients and by the public check alike: only Ampify's own Marketplace
holds it. Next to the entries, `versions.json` lists the Apple Music versions tested after a release; `catalog.json`
pins its address and SHA-256, and `ampify versions update` reads it. Schema:
[schemas/catalog.schema.json](../../schemas/catalog.schema.json), which refuses a field it does not list.

### Compatibility

- Ampify 1.0.0 and later read a catalog and leave aside any field they do not know. Builds before 1.0.0 refuse the
  whole catalog for one. That is why the entries written by `tools/pack.py` and `ampify build` leave `translations`
  out; the archive's manifest keeps them. Once no build before 1.0.0 is in use, the entries can carry them.
- An Ampify release reads one catalog address and cannot be told apart from another by it, so a version that only a
  newer release understands goes to `newer`, never into the entry itself.
- `ampify build` prints the fields every entry needs. The bot, through `tools/pack.py`, also writes `homepage`,
  `license`, `preview` and the real address.
- Both tools write the same archive: files sorted by name, hidden files and `node_modules` left out, every file stored
  (not compressed), the date 1980-01-01. The same folder gives the same SHA-256 with either tool and any zlib. Archives
  already published keep their SHA-256 and are never packed again.

## What install checks

`ampify install`, and the Marketplace page that calls it, check in this order:

1. The id is valid and in the catalog, and the address is `https://`.
2. The name, author, description and permissions are shown. Without `--yes`, the answer must be "y".
3. The downloaded archive must have the catalog's SHA-256, or it is thrown away.
4. Each file name is checked (no absolute path, no `..`, no `\`, no control character), then the archive is extracted
   to a work folder. A symbolic link refuses everything.
5. The manifest must match the catalog's id, kind and version, and ask for the same permissions as the entry that was
   shown (in any order). A permission reserved for Ampify's Marketplace (`kit.manage`) is refused in either.
6. Only then is the folder put in place; an older version stays until this succeeds.

Nothing downloaded runs during an install, and `ampify install` leaves the new element off. The Marketplace puts it
on right after: a theme in use, anything else turned on. For an element that asks for permissions, the card's
**Install…** opens its detail view first, which lists what it may do. An update keeps the element's settings.

An element tagged `developer` (with `example`, for the examples of `examples/`) is listed in the Marketplace's
**Developers** tab instead of the tab of its kind.
