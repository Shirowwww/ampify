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
- After the merge, a bot packs the element with `tools/pack.py`, writes `packages/<id>-<version>.zip` and its preview,
  and updates `catalog.json` with the archive's address and SHA-256. A published archive never changes.

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
| `translations` | no | A copy of the manifest's. Left out of the public catalog for now, since Ampify 1.1.0 refuses a catalog with a field it does not know |

Ampify reads a catalog and leaves aside any field it does not know. `kit.manage` is refused in the catalog. Next to it,
`versions.json` lists the Apple Music versions tested after a release; `catalog.json` pins its address and SHA-256, and
`ampify versions update` reads it. Schema: [schemas/catalog.schema.json](../../schemas/catalog.schema.json).

## What install checks

`ampify install`, and the Marketplace page that calls it, check in this order:

1. The id is valid and in the catalog, and the address is `https://`.
2. The name, author, description and permissions are shown. Without `--yes`, the answer must be "y".
3. The downloaded archive must have the catalog's SHA-256, or it is thrown away.
4. Each file name is checked (no absolute path, no `..`, no `\`, no control character), then the archive is extracted
   to a work folder. A symbolic link refuses everything.
5. The manifest must match the catalog's id, kind and version.
6. Only then is the folder put in place; an older version stays until this succeeds.

Nothing downloaded runs during an install, and the new element starts off. In the Marketplace, an element that asks for
permissions opens its detail view after install, where switching it on is a second click under its permissions. An
update keeps the element's settings.
