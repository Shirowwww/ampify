#!/usr/bin/env python3
"""Packs an element folder into the zip that the catalog points to, and pins its SHA-256.

    python3 tools/pack.py <folder> --out <archive.zip>
    python3 tools/pack.py <folder> --out <archive.zip> --catalog ../ampify-catalog/catalog.json --url https://...

The archive holds the files of the folder at its root, with manifest.json among them, as docs/extensions/catalog.md
requires. The zip is deterministic: entries are sorted, stored (not compressed) and carry a fixed timestamp, so the same
folder always gives the same SHA-256, with any zlib and byte for byte what `ampify build` writes (src/cli/zip_writer.cpp). With --catalog, the entry for the element is added or replaced, and the catalog is validated. The
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
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import validate  # noqa: E402  (same folder: the schemas and rules of Ampify)

# The fields of src/cli/zip_writer.cpp: 1980-01-01 00:00:00, UTF-8 names, a Unix host, mode 0100644.
DOS_DATE = (1 << 5) | 1
UTF8_NAMES = 0x0800
VERSION_NEEDED = 10
VERSION_MADE_BY = 0x031E
REGULAR_FILE_MODE = 0o100644 << 16


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
    """Every regular file under `folder`, as posix paths relative to it, sorted by their UTF-8 bytes. Like `ampify
    build`, it leaves out hidden files and folders, and node_modules."""
    found = []
    for root, dirs, files in os.walk(folder):
        dirs[:] = sorted(name for name in dirs if not name.startswith(".") and name != "node_modules")
        for name in files:
            if not name.startswith("."):
                found.append(os.path.relpath(os.path.join(root, name), folder).replace(os.sep, "/"))
    return sorted(found, key=lambda relative: relative.encode("utf-8"))


def zip_bytes(entries):
    """A zip of (name, data) pairs, in the given order, every file stored: the layout of src/cli/zip_writer.cpp."""
    body, records = bytearray(), []
    for name, data in entries:
        raw, crc = name.encode("utf-8"), zlib.crc32(data)
        records.append((raw, crc, len(data), len(body)))
        body += struct.pack("<IHHHHHIIIHH", 0x04034B50, VERSION_NEEDED, UTF8_NAMES, 0, 0, DOS_DATE, crc, len(data),
                            len(data), len(raw), 0) + raw + data
    start = len(body)
    for raw, crc, size, offset in records:
        body += struct.pack("<IHHHHHHIIIHHHHHII", 0x02014B50, VERSION_MADE_BY, VERSION_NEEDED, UTF8_NAMES, 0, 0,
                            DOS_DATE, crc, size, size, len(raw), 0, 0, 0, 0, REGULAR_FILE_MODE, offset) + raw
    body += struct.pack("<IHHHHIIH", 0x06054B50, 0, 0, len(records), len(records), len(body) - start, start, 0)
    return bytes(body)


def build_archive(folder, archive):
    """Writes the zip and returns the SHA-256 of its bytes."""
    entries = []
    for relative in files_of(folder):
        with open(os.path.join(folder, relative), "rb") as source:
            entries.append((relative, source.read()))
    with open(archive, "wb") as handle:
        handle.write(zip_bytes(entries))
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
    """Copies the element's README.md next to the archive, under the archive's name (demo-1.0.0.md). The catalog has
    no field for it: `ampify market readme` finds it from the archive's address."""
    readme = os.path.join(folder, "README.md")
    if os.path.isfile(readme):
        shutil.copyfile(readme, os.path.splitext(archive)[0] + ".md")


def catalog_entry(manifest, url, sha256, preview=None):
    entry = {
        key: manifest[key]
        for key in ("id", "name", "version", "kind", "author", "description", "kitApi")
    }
    # Not the translations: see "Compatibility" in docs/extensions/catalog.md. They stay in the archive's manifest.
    for key in ("homepage", "license", "tags"):
        if key in manifest:
            entry[key] = manifest[key]
    if preview:
        entry["preview"] = preview
    entry["url"] = url
    entry["sha256"] = sha256
    entry["permissions"] = list(manifest.get("permissions", []))
    return entry


def place_entry(entries, entry, min_ampify):
    """Without min_ampify, the version replaces the entry (its newer list stays). With it, a first version becomes the
    entry and a later one goes to the entry's newer list: Ampify 1.2.1 and older read only the entry, so it keeps the
    version they take."""
    if min_ampify:
        entry = dict(entry, minAmpify=min_ampify)
    current = next((item for item in entries if item["id"] == entry["id"]), None)
    if current is None:
        entries.append(entry)
    elif min_ampify:
        newer = [item for item in current.get("newer", []) if item["version"] != entry["version"]] + [entry]
        current["newer"] = sorted(newer, key=lambda item: validate.version_key(item["version"]))
    else:
        if "newer" in current:
            entry = dict(entry, newer=current["newer"])
        entries[entries.index(current)] = entry
    entries.sort(key=lambda item: item["id"])


def update_catalog(catalog_path, entry, min_ampify=None):
    """Adds the version to the catalog (see place_entry). The new catalog is checked beside the old one and replaces it
    only when valid."""
    with open(catalog_path, encoding="utf-8") as handle:
        catalog = json.load(handle)
    place_entry(catalog["extensions"], entry, min_ampify)
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


def pack(folder, archive, catalog=None, url=None, min_ampify=None):
    manifest = check_folder(folder)
    if catalog and not url:
        raise PackError("--catalog also needs --url, the https address where the archive will be published")
    if min_ampify and not catalog:
        raise PackError("--min-ampify needs --catalog, the catalog where the version is listed")
    if catalog:
        check_same_name(archive, url)
    sha256 = build_archive(folder, archive)
    if catalog:
        preview = preview_target(manifest, archive)
        entry = catalog_entry(manifest, url, sha256, preview and preview_address(preview, url))
        update_catalog(catalog, entry, min_ampify)
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
    parser.add_argument("--min-ampify", help="oldest Ampify release that takes this version (like 1.3.0): a later "
                        "version of a listed element then goes to its newer list")
    args = parser.parse_args(argv)
    if args.min_ampify and not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", args.min_ampify):
        parser.error("--min-ampify expects three numbers, like 1.3.0")

    try:
        result = pack(args.folder, args.out, args.catalog, args.url, args.min_ampify)
    except (PackError, OSError, KeyError, ValueError) as error:
        print(f"pack: {error}", file=sys.stderr)
        return 1
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0

if __name__ == "__main__":
    sys.exit(main())
