#!/usr/bin/env python3
"""Packs an element folder into the zip that the catalog points to, and pins its SHA-256.

    python3 tools/pack.py <folder> --out <archive.zip>
    python3 tools/pack.py <folder> --out <archive.zip> --catalog catalog/catalog.json --url https://...

The archive holds the files of the folder at its root, with manifest.json among them, as docs/extensions/catalog.md
requires. The zip is deterministic: entries are sorted and carry a fixed timestamp, so the same folder always gives
the same SHA-256. With --catalog, the entry for the element is added or replaced, and the catalog is validated. The
manifest's preview picture is copied next to the archive under the archive's name (demo-1.0.0.jpg), and the entry
points to it next to the archive's address: publish both.
"""

import argparse
import contextlib
import hashlib
import io
import json
import os
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
    report = io.StringIO()
    with contextlib.redirect_stdout(report):
        status = validate.main(["manifest", manifest])
    if status != 0:
        raise PackError(report.getvalue().strip() or f"{manifest}: invalid")
    with open(manifest, encoding="utf-8") as handle:
        return json.load(handle)


def files_of(folder):
    """Every regular file under `folder`, as posix paths relative to it, sorted. Links are refused."""
    found = []
    for root, dirs, files in os.walk(folder):
        dirs.sort()
        for name in sorted(files):
            full = os.path.join(root, name)
            if os.path.islink(full):
                raise PackError(f"symbolic link not allowed: {full}")
            found.append(os.path.relpath(full, folder).replace(os.sep, "/"))
        for name in dirs:
            if os.path.islink(os.path.join(root, name)):
                raise PackError(f"symbolic link not allowed: {os.path.join(root, name)}")
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


def publish_preview(folder, manifest, archive, url):
    """Copies the manifest's preview next to the archive, under the archive's name, and returns its address next to
    the archive's: the Marketplace shows a catalog preview only from the catalog's host or the archive's."""
    if "preview" not in manifest:
        return None
    target = os.path.splitext(archive)[0] + os.path.splitext(manifest["preview"])[1]
    shutil.copyfile(os.path.join(folder, manifest["preview"]), target)
    return url.rsplit("/", 1)[0] + "/" + os.path.basename(target)


def catalog_entry(manifest, url, sha256, preview=None):
    entry = {
        key: manifest[key]
        for key in ("id", "name", "version", "kind", "author", "description", "kitApi")
    }
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
    with open(catalog_path, encoding="utf-8") as handle:
        catalog = json.load(handle)
    entries = catalog["extensions"]
    entries[:] = [item for item in entries if item["id"] != entry["id"]] + [entry]
    entries.sort(key=lambda item: item["id"])
    with open(catalog_path, "w", encoding="utf-8", newline="\n") as handle:
        json.dump(catalog, handle, ensure_ascii=False, indent=2)
        handle.write("\n")
    report = io.StringIO()
    with contextlib.redirect_stdout(report):
        status = validate.main(["catalog", catalog_path])
    if status != 0:
        raise PackError(f"invalid catalog after adding {entry['id']}: {report.getvalue().strip()}")


def pack(folder, archive, catalog=None, url=None):
    manifest = check_folder(folder)
    if catalog and not url:
        raise PackError("--catalog also needs --url, the https address where the archive will be published")
    sha256 = build_archive(folder, archive)
    if catalog:
        preview = publish_preview(folder, manifest, archive, url)
        update_catalog(catalog, catalog_entry(manifest, url, sha256, preview))
    return {"id": manifest["id"], "version": manifest["version"], "archive": archive, "sha256": sha256}


def main(argv=None):
    parser = argparse.ArgumentParser(description="Packs an Ampify element and pins its SHA-256.")
    parser.add_argument("folder", help="element folder, with its manifest.json")
    parser.add_argument("--out", required=True, help="the .zip archive to create")
    parser.add_argument("--catalog", help="catalog to update (catalog/catalog.json)")
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
