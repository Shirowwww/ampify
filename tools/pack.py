#!/usr/bin/env python3
"""Packs an element folder into the zip that the catalog points to, and pins its SHA-256.

    python3 tools/pack.py <folder> --out <archive.zip>
    python3 tools/pack.py <folder> --out <archive.zip> --catalog ../ampify-catalog/catalog.json --url https://...

The archive holds the files of the folder at its root, with manifest.json among them, as docs/extensions/catalog.md
requires. The zip is deterministic: entries are sorted and carry a fixed timestamp, so the same folder always gives
the same SHA-256. With --catalog, the entry for the element is added or replaced, and the catalog is validated. The
manifest's preview picture is copied next to the archive under the archive's name (demo-1.0.0.jpg), and the entry
points to it next to the archive's address. A README.md at the root of the folder is copied the same way
(demo-1.0.0.md), for the Marketplace's detail page: publish them all.
"""

import argparse
import hashlib
import json
import os
import re
import shutil
import sys
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import validate  # noqa: E402  (same folder: the schemas and rules of Ampify)

FIXED_TIMESTAMP = (1980, 1, 1, 0, 0, 0)


class PackError(Exception):
    pass


def check_folder(folder):
    """Runs the manifest checks of validate.py. Raises PackError with the reasons."""
    manifest = os.path.join(folder, "manifest.json")
    if not os.path.isfile(manifest):
        raise PackError(f"{folder}: manifest.json missing")
    check_json("manifest", manifest)
    with open(manifest, encoding="utf-8") as handle:
        return json.load(handle)


def check_json(kind, path):
    """Runs validate.py on a file (it also refuses links inside an element's folder). Raises PackError."""
    try:
        errors = validate.check_file(kind, path)
    except (OSError, ValueError, validate.SchemaError, re.error) as error:
        raise PackError(f"{path}: cannot check: {error}") from error
    if errors:
        raise PackError("\n".join(f"{path}: {message}" for message in errors))


def files_of(folder):
    """Every regular file under `folder`, as posix paths relative to it, sorted."""
    found = []
    for root, dirs, files in os.walk(folder):
        dirs.sort()
        for name in files:
            found.append(os.path.relpath(os.path.join(root, name), folder).replace(os.sep, "/"))
    return sorted(found)


def build_archive(folder, archive):
    """Writes the zip and returns the SHA-256 of its bytes."""
    with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED) as bundle:
        for relative in files_of(folder):
            info = zipfile.ZipInfo(relative, date_time=FIXED_TIMESTAMP)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            with open(os.path.join(folder, relative), "rb") as source:
                bundle.writestr(info, source.read())
    digest = hashlib.sha256()
    with open(archive, "rb") as handle:
        for chunk in iter(lambda: handle.read(65536), b""):
            digest.update(chunk)
    return digest.hexdigest()


def preview_target(manifest, archive):
    """Where the manifest's preview is published: next to the archive, under the archive's name. None without one."""
    if "preview" not in manifest:
        return None
    return os.path.splitext(archive)[0] + os.path.splitext(manifest["preview"])[1]


def preview_address(target, url):
    """The preview's address next to the archive's: the Marketplace shows a catalog preview only from the catalog's
    host or the archive's."""
    return url.rsplit("/", 1)[0] + "/" + os.path.basename(target)


def publish_readme(folder, archive):
    """Copies the element's README.md next to the archive, under the archive's name (demo-1.0.0.md). The catalog gains
    no field for it, which Ampify 1.1.0 would refuse: `ampify market readme` finds it from the archive's address."""
    readme = os.path.join(folder, "README.md")
    if os.path.isfile(readme):
        shutil.copyfile(readme, os.path.splitext(archive)[0] + ".md")


def catalog_entry(manifest, url, sha256, preview=None):
    entry = {
        key: manifest[key]
        for key in ("id", "name", "version", "kind", "author", "description", "kitApi")
    }
    # Not the translations: Ampify 1.1.0 refuses the whole catalog for a field it does not know. They stay in the
    # archive's manifest, which every version reads.
    for key in ("homepage", "license", "tags"):
        if key in manifest:
            entry[key] = manifest[key]
    if preview:
        entry["preview"] = preview
    entry["url"] = url
    entry["sha256"] = sha256
    entry["permissions"] = list(manifest.get("permissions", []))
    return entry


def update_catalog(catalog_path, entry):
    """Adds or replaces the entry. The new catalog is checked beside the old one and replaces it only when valid."""
    with open(catalog_path, encoding="utf-8") as handle:
        catalog = json.load(handle)
    entries = catalog["extensions"]
    entries[:] = [item for item in entries if item["id"] != entry["id"]] + [entry]
    entries.sort(key=lambda item: item["id"])
    candidate = catalog_path + ".new"
    try:
        with open(candidate, "w", encoding="utf-8", newline="\n") as handle:
            json.dump(catalog, handle, ensure_ascii=False, indent=2)
            handle.write("\n")
        check_json("catalog", candidate)
        os.replace(candidate, catalog_path)
    finally:
        if os.path.exists(candidate):
            os.remove(candidate)


def check_same_name(archive, url):
    """The preview and the README are published under the archive's name and the entry points to the address's:
    the two names must agree, or the entry points to files that are not there."""
    local = os.path.splitext(os.path.basename(archive))[0]
    remote = os.path.splitext(url.rsplit("/", 1)[-1])[0]
    if local != remote:
        raise PackError(f"--out is named {local}.zip but --url ends with {remote}: use the same name for both")


def pack(folder, archive, catalog=None, url=None):
    manifest = check_folder(folder)
    if catalog and not url:
        raise PackError("--catalog also needs --url, the https address where the archive will be published")
    if catalog:
        check_same_name(archive, url)
    sha256 = build_archive(folder, archive)
    if catalog:
        preview = preview_target(manifest, archive)
        entry = catalog_entry(manifest, url, sha256, preview and preview_address(preview, url))
        update_catalog(catalog, entry)
        if preview:
            shutil.copyfile(os.path.join(folder, manifest["preview"]), preview)
        publish_readme(folder, archive)
    return {"id": manifest["id"], "version": manifest["version"], "archive": archive, "sha256": sha256}


def main(argv=None):
    parser = argparse.ArgumentParser(description="Packs an Ampify element and pins its SHA-256.")
    parser.add_argument("folder", help="element folder, with its manifest.json")
    parser.add_argument("--out", required=True, help="the .zip archive to create")
    parser.add_argument("--catalog", help="catalog to update (catalog.json of ampify-catalog)")
    parser.add_argument("--url", help="https:// address where the archive will be published")
    args = parser.parse_args(argv)

    try:
        result = pack(args.folder, args.out, args.catalog, args.url)
    except (PackError, OSError, KeyError, ValueError) as error:
        print(f"pack: {error}", file=sys.stderr)
        return 1
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0

if __name__ == "__main__":
    sys.exit(main())
