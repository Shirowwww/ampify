"""Tests for tools/validate.py. Run from the repository root:

    python3 -m unittest discover -s tools -v
"""

import contextlib
import io
import json
import os
import tempfile
import unittest

import validate

REPO = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")


def run(kind, path):
    """Run the validator on one file. Returns (exit code, printed text)."""
    out = io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(out):
        code = validate.main([kind, path])
    return code, out.getvalue()


def write_json(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as handle:
        json.dump(data, handle)


def entry(**changes):
    data = {
        "id": "demo-ext",
        "name": "Démo",
        "version": "1.0.0",
        "kind": "extension",
        "author": "Shirowwww",
        "description": "Entrée de test.",
        "kitApi": "1.0",
        "url": "https://example.com/demo-ext.zip",
        "sha256": "a" * 64,
    }
    return apply_changes(data, changes)


def manifest(**changes):
    data = {
        "id": "demo-ext",
        "name": "Démo",
        "version": "1.0.0",
        "kind": "extension",
        "author": "Shirowwww",
        "description": "Manifeste de test.",
        "kitApi": "1.0",
        "main": "main.js",
    }
    return apply_changes(data, changes)


def apply_changes(data, changes):
    """A None value removes the key, so a test can drop a required field."""
    for key, value in changes.items():
        if value is None:
            data.pop(key, None)
        else:
            data[key] = value
    return data


class CatalogTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.path = os.path.join(self.tmp.name, "catalog.json")

    def tearDown(self):
        self.tmp.cleanup()

    def write(self, extensions, **changes):
        data = {"catalogVersion": 1, "extensions": extensions}
        write_json(self.path, apply_changes(data, changes))
        return run("catalog", self.path)

    def test_repository_catalog_is_valid(self):
        code, text = run("catalog", os.path.join(REPO, "catalog", "catalog.json"))
        self.assertEqual(code, 0, text)

    def test_valid_entry_passes(self):
        code, text = self.write([entry()])
        self.assertEqual(code, 0, text)

    def test_sha256_must_be_lowercase_hex(self):
        code, text = self.write([entry(sha256="XYZ")])
        self.assertEqual(code, 1)
        self.assertIn("extensions[0].sha256", text)

    def test_url_must_be_https(self):
        code, text = self.write([entry(url="http://example.com/demo.zip")])
        self.assertEqual(code, 1)
        self.assertIn("extensions[0].url", text)

    def test_duplicate_ids_are_refused(self):
        code, text = self.write([entry(), entry(name="Autre")])
        self.assertEqual(code, 1)
        self.assertIn("déjà présent", text)

    def test_unknown_field_is_refused(self):
        code, text = self.write([entry(download_count=3)])
        self.assertEqual(code, 1)
        self.assertIn("propriété non autorisée", text)

    def test_missing_required_field_is_refused(self):
        code, text = self.write([entry(sha256=None)])
        self.assertEqual(code, 1)
        self.assertIn("champ requis absent : sha256", text)

    def test_catalog_version_must_be_one(self):
        code, text = self.write([], catalogVersion=2)
        self.assertEqual(code, 1)
        self.assertIn("catalogVersion", text)

    def test_other_kit_api_major_is_refused(self):
        code, text = self.write([entry(kitApi="2.0")])
        self.assertEqual(code, 1)
        self.assertIn("version d'API non prise en charge", text)

    def test_unknown_permission_is_refused(self):
        code, text = self.write([entry(permissions=["fs.write"])])
        self.assertEqual(code, 1)
        self.assertIn("permissions[0]", text)

    def test_missing_file_is_a_usage_error(self):
        code, _ = run("catalog", os.path.join(self.tmp.name, "absent.json"))
        self.assertEqual(code, 2)


class ManifestTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()

    def tearDown(self):
        self.tmp.cleanup()

    def install(self, data, folder=None, files=("main.js",)):
        """Write manifest.json in a folder named after the id, with the given files beside it."""
        folder = os.path.join(self.tmp.name, folder or data["id"])
        write_json(os.path.join(folder, "manifest.json"), data)
        for name in files:
            with open(os.path.join(folder, name), "w", encoding="utf-8") as handle:
                handle.write("// test\n")
        return os.path.join(folder, "manifest.json")

    def test_repository_example_is_valid(self):
        path = os.path.join(REPO, "examples", "hello-extension", "manifest.json")
        code, text = run("manifest", path)
        self.assertEqual(code, 0, text)

    def test_valid_extension_passes(self):
        code, text = run("manifest", self.install(manifest(permissions=["storage", "network:api.example.com"])))
        self.assertEqual(code, 0, text)

    def test_folder_must_match_id(self):
        code, text = run("manifest", self.install(manifest(), folder="autre"))
        self.assertEqual(code, 1)
        self.assertIn("doit être le nom du dossier", text)

    def test_file_must_be_named_manifest(self):
        path = self.install(manifest())
        renamed = os.path.join(os.path.dirname(path), "extension.json")
        os.rename(path, renamed)
        code, text = run("manifest", renamed)
        self.assertEqual(code, 1)
        self.assertIn("manifest.json", text)

    def test_extension_needs_main(self):
        code, text = run("manifest", self.install(manifest(main=None)))
        self.assertEqual(code, 1)
        self.assertIn("main : requis", text)

    def test_main_must_exist_beside_manifest(self):
        code, text = run("manifest", self.install(manifest(), files=()))
        self.assertEqual(code, 1)
        self.assertIn("fichier absent du dossier", text)

    def test_path_traversal_is_refused(self):
        code, text = run("manifest", self.install(manifest(main="../evil.js")))
        self.assertEqual(code, 1)
        self.assertIn("main", text)

    def test_unknown_permission_is_refused(self):
        code, text = run("manifest", self.install(manifest(permissions=["fs.write"])))
        self.assertEqual(code, 1)
        self.assertIn("permissions[0]", text)

    def test_kit_api_must_match_major_version(self):
        code, text = run("manifest", self.install(manifest(kitApi="2.0")))
        self.assertEqual(code, 1)
        self.assertIn("kitApi", text)

    def test_theme_needs_colors_or_resources(self):
        code, text = run("manifest", self.install(manifest(kind="theme", main=None), files=()))
        self.assertEqual(code, 1)
        self.assertIn("au moins un des deux", text)

    def test_theme_with_colors_passes(self):
        data = manifest(kind="theme", main=None, colors="colors.ini", defaultScheme="Base")
        code, text = run("manifest", self.install(data, files=("colors.ini",)))
        self.assertEqual(code, 0, text)

    def test_snippet_needs_resources_and_forbids_main(self):
        code, text = run("manifest", self.install(manifest(kind="snippet", resources="snippet.xaml"), files=("snippet.xaml", "main.js")))
        self.assertEqual(code, 1)
        self.assertIn("main : interdit pour un snippet", text)


class SchemaSubsetTests(unittest.TestCase):
    def test_both_repository_schemas_use_only_supported_keywords(self):
        for kind, path in validate.SCHEMAS.items():
            with open(path, encoding="utf-8") as handle:
                validate.check_supported(json.load(handle), kind)

    def test_unsupported_keyword_is_refused(self):
        with self.assertRaises(validate.SchemaError):
            validate.check_supported({"oneOf": []}, "test")

    def test_nested_unsupported_keyword_is_refused(self):
        schema = {"properties": {"a": {"items": {"if": {}}}}}
        with self.assertRaises(validate.SchemaError):
            validate.check_supported(schema, "test")

    def test_broken_reference_is_refused(self):
        with self.assertRaises(validate.SchemaError):
            validate.resolve({"$defs": {}}, "#/$defs/absent")


if __name__ == "__main__":
    unittest.main()
