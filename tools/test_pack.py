"""Tests of tools/pack.py: deterministic archives, pinned checksums, catalog updates.

    python3 -m unittest discover -s tools -v
"""

import hashlib
import json
import os
import shutil
import sys
import tempfile
import unittest
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pack  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXAMPLE = os.path.join(ROOT, "examples", "hello-extension")


class PackTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="ampify-pack-")
        self.addCleanup(shutil.rmtree, self.tmp, True)

    def make_element(self, name="demo", kind="extension", version="1.0.0", extra=None):
        folder = os.path.join(self.tmp, name)
        os.makedirs(folder)
        manifest = {
            "id": name,
            "name": name.title(),
            "version": version,
            "kind": kind,
            "author": "Shirowwww",
            "description": "A test element.",
            "kitApi": "1.0",
        }
        if kind == "extension":
            manifest["main"] = "main.js"
            with open(os.path.join(folder, "main.js"), "w", encoding="utf-8") as handle:
                handle.write('Ampify.log("ok");\n')
        with open(os.path.join(folder, "manifest.json"), "w", encoding="utf-8") as handle:
            json.dump(manifest, handle)
        for relative, text in (extra or {}).items():
            with open(os.path.join(folder, relative), "w", encoding="utf-8") as handle:
                handle.write(text)
        return folder

    def test_example_packs_with_root_entries_and_pinned_sha(self):
        archive = os.path.join(self.tmp, "hello.zip")
        result = pack.pack(EXAMPLE, archive)
        with zipfile.ZipFile(archive) as bundle:
            self.assertEqual(sorted(bundle.namelist()), ["main.js", "manifest.json", "preview.jpg"])
        with open(archive, "rb") as handle:
            self.assertEqual(result["sha256"], hashlib.sha256(handle.read()).hexdigest())
        self.assertEqual(result["id"], "hello-extension")

    def test_same_folder_gives_the_same_bytes(self):
        folder = self.make_element()
        first = pack.pack(folder, os.path.join(self.tmp, "a.zip"))["sha256"]
        second = pack.pack(folder, os.path.join(self.tmp, "b.zip"))["sha256"]
        self.assertEqual(first, second)

    def test_archive_is_stored_and_matches_the_bytes_of_ampify_build(self):
        # The same hash is checked on src/cli/zip_writer.cpp in tests/cli_tests.cpp: one archive, one SHA-256.
        entries = [("lib/é.js", b"x=1;\n"), ("main.js", b"Ampify.log(1);\n"), ("manifest.json", b"{}")]
        data = pack.zip_bytes(entries)
        self.assertEqual(hashlib.sha256(data).hexdigest(),
                         "37e93ce42dec78b704561e8191016bad688a7414c1e2a80a5842b139211ad6fd")
        path = os.path.join(self.tmp, "stored.zip")
        with open(path, "wb") as handle:
            handle.write(data)
        with zipfile.ZipFile(path) as bundle:
            self.assertIsNone(bundle.testzip())
            self.assertEqual({info.compress_type for info in bundle.infolist()}, {zipfile.ZIP_STORED})
            self.assertEqual(bundle.read("lib/é.js"), b"x=1;\n")

    def test_hidden_files_and_node_modules_stay_out_like_in_ampify_build(self):
        folder = self.make_element(extra={".hidden": "x"})
        os.makedirs(os.path.join(folder, "node_modules"))
        os.makedirs(os.path.join(folder, ".git"))
        for relative in ("node_modules/a.js", ".git/config"):
            with open(os.path.join(folder, relative), "w", encoding="utf-8") as handle:
                handle.write("x")
        self.assertEqual(pack.files_of(folder), ["main.js", "manifest.json"])

    def test_invalid_manifest_is_refused(self):
        folder = self.make_element()
        os.remove(os.path.join(folder, "main.js"))
        with self.assertRaises(pack.PackError) as caught:
            pack.pack(folder, os.path.join(self.tmp, "x.zip"))
        self.assertIn("main", str(caught.exception))

    def test_links_are_refused(self):
        folder = self.make_element()
        target = os.path.join(folder, "main.js")
        try:
            os.symlink(target, os.path.join(folder, "alias.js"))
        except OSError:
            self.skipTest("symbolic links are not available here")
        with self.assertRaises(pack.PackError):
            pack.pack(folder, os.path.join(self.tmp, "x.zip"))

    def test_catalog_entry_is_added_then_replaced(self):
        folder = self.make_element()
        catalog = os.path.join(self.tmp, "catalog.json")
        with open(catalog, "w", encoding="utf-8") as handle:
            json.dump({"catalogVersion": 1, "extensions": []}, handle)

        pack.pack(folder, os.path.join(self.tmp, "demo-1.zip"), catalog, "https://example.com/demo-1.zip")
        pack.pack(folder, os.path.join(self.tmp, "demo-2.zip"), catalog, "https://example.com/demo-2.zip")

        with open(catalog, encoding="utf-8") as handle:
            entries = json.load(handle)["extensions"]
        self.assertEqual([entry["id"] for entry in entries], ["demo"])
        self.assertEqual(entries[0]["url"], "https://example.com/demo-2.zip")
        self.assertRegex(entries[0]["sha256"], r"^[0-9a-f]{64}$")

    def test_preview_is_published_next_to_the_archive(self):
        folder = self.make_element()
        with open(os.path.join(folder, "preview.jpg"), "wb") as handle:
            handle.write(b"\xff\xd8\xff\xe0 not a real picture")
        with open(os.path.join(folder, "manifest.json"), encoding="utf-8") as handle:
            manifest = json.load(handle)
        manifest.update(preview="preview.jpg", tags=["dark", "minimal"])
        with open(os.path.join(folder, "manifest.json"), "w", encoding="utf-8") as handle:
            json.dump(manifest, handle)
        catalog = os.path.join(self.tmp, "catalog.json")
        with open(catalog, "w", encoding="utf-8") as handle:
            json.dump({"catalogVersion": 1, "extensions": []}, handle)

        archive = os.path.join(self.tmp, "packages", "demo-1.0.0.zip")
        os.makedirs(os.path.dirname(archive))
        pack.pack(folder, archive, catalog, "https://example.com/packages/demo-1.0.0.zip")

        with open(catalog, encoding="utf-8") as handle:
            entry = json.load(handle)["extensions"][0]
        self.assertEqual(entry["preview"], "https://example.com/packages/demo-1.0.0.jpg")
        self.assertEqual(entry["tags"], ["dark", "minimal"])
        self.assertTrue(os.path.isfile(os.path.join(self.tmp, "packages", "demo-1.0.0.jpg")))

    def test_readme_is_published_next_to_the_archive_without_a_catalog_field(self):
        folder = self.make_element()
        with open(os.path.join(folder, "README.md"), "w", encoding="utf-8") as handle:
            handle.write("# Demo\n\nWhat it does.\n")
        catalog = os.path.join(self.tmp, "catalog.json")
        with open(catalog, "w", encoding="utf-8") as handle:
            json.dump({"catalogVersion": 1, "extensions": []}, handle)

        archive = os.path.join(self.tmp, "packages", "demo-1.0.0.zip")
        os.makedirs(os.path.dirname(archive))
        pack.pack(folder, archive, catalog, "https://example.com/packages/demo-1.0.0.zip")

        with open(os.path.join(self.tmp, "packages", "demo-1.0.0.md"), encoding="utf-8") as handle:
            self.assertEqual(handle.read(), "# Demo\n\nWhat it does.\n")
        with zipfile.ZipFile(archive) as bundle:
            self.assertIn("README.md", bundle.namelist())
        with open(catalog, encoding="utf-8") as handle:
            entry = json.load(handle)["extensions"][0]
        self.assertNotIn("readme", entry, "the README is found from the archive's address, not from a field")

    def test_catalog_entry_leaves_the_translations_in_the_archive(self):
        folder = self.make_element()
        with open(os.path.join(folder, "manifest.json"), encoding="utf-8") as handle:
            manifest = json.load(handle)
        manifest["translations"] = {"fr": {"name": "Démo", "description": "Un élément de test."}}
        with open(os.path.join(folder, "manifest.json"), "w", encoding="utf-8") as handle:
            json.dump(manifest, handle)
        catalog = os.path.join(self.tmp, "catalog.json")
        with open(catalog, "w", encoding="utf-8") as handle:
            json.dump({"catalogVersion": 1, "extensions": []}, handle)
        archive = os.path.join(self.tmp, "demo-1.0.0.zip")
        pack.pack(folder, archive, catalog, "https://example.com/demo-1.0.0.zip")
        with open(catalog, encoding="utf-8") as handle:
            entry = json.load(handle)["extensions"][0]
        self.assertNotIn("translations", entry)  # docs/extensions/catalog.md, "Compatibility"

    def test_catalog_needs_an_https_address(self):
        folder = self.make_element()
        catalog = os.path.join(self.tmp, "catalog.json")
        with open(catalog, "w", encoding="utf-8") as handle:
            json.dump({"catalogVersion": 1, "extensions": []}, handle)
        with self.assertRaises(pack.PackError):
            pack.pack(folder, os.path.join(self.tmp, "x.zip"), catalog, "http://example.com/x.zip")

    def test_archive_and_address_must_share_a_name(self):
        folder = self.make_element()
        catalog = os.path.join(self.tmp, "catalog.json")
        with open(catalog, "w", encoding="utf-8") as handle:
            json.dump({"catalogVersion": 1, "extensions": []}, handle)
        with self.assertRaises(pack.PackError) as caught:
            pack.pack(folder, os.path.join(self.tmp, "demo.zip"), catalog, "https://example.com/demo-1.0.0.zip")
        self.assertIn("demo-1.0.0", str(caught.exception))
        self.assertEqual(sorted(os.listdir(self.tmp)), ["catalog.json", "demo"])

    def test_invalid_catalog_is_left_untouched(self):
        folder = self.make_element()
        catalog = os.path.join(self.tmp, "catalog.json")
        with open(catalog, "w", encoding="utf-8") as handle:
            json.dump({"catalogVersion": 1, "extensions": [{"id": "old", "kitApi": "9.0"}]}, handle)
        with open(catalog, "rb") as handle:
            before = handle.read()
        with self.assertRaises(pack.PackError):
            pack.pack(folder, os.path.join(self.tmp, "demo-1.zip"), catalog, "https://example.com/demo-1.zip")
        with open(catalog, "rb") as handle:
            self.assertEqual(handle.read(), before)
        self.assertEqual(sorted(os.listdir(self.tmp)), ["catalog.json", "demo", "demo-1.zip"])


if __name__ == "__main__":
    unittest.main()
