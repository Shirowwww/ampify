"""Tests for tools/validate.py. Run from the repository root:

    python3 -m unittest discover -s tools -v
"""

import contextlib
import hashlib
import io
import json
import os
import random
import re
import tempfile
import unittest

import validate

REPO = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
# The public catalog lives in its own repository (ampify-catalog). Its files are checked when a clone sits next to
# this one, or where AMPIFY_CATALOG points.
CATALOG = os.environ.get("AMPIFY_CATALOG") or os.path.join(REPO, "..", "ampify-catalog")
needs_catalog = unittest.skipUnless(os.path.exists(os.path.join(CATALOG, "catalog.json")),
                                    "no ampify-catalog clone next to this repository (or set AMPIFY_CATALOG)")


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
        "name": "Demo",
        "version": "1.0.0",
        "kind": "extension",
        "author": "Shirowwww",
        "description": "A test entry.",
        "kitApi": "1.0",
        "url": "https://example.com/demo-ext.zip",
        "sha256": "a" * 64,
    }
    return apply_changes(data, changes)


def manifest(**changes):
    data = {
        "id": "demo-ext",
        "name": "Demo",
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

    @needs_catalog
    def test_public_catalog_is_valid(self):
        code, text = run("catalog", os.path.join(CATALOG, "catalog.json"))
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
        code, text = self.write([entry(), entry(name="Other")])
        self.assertEqual(code, 1)
        self.assertIn("already in the catalog", text)

    def test_unknown_field_is_refused(self):
        code, text = self.write([entry(download_count=3)])
        self.assertEqual(code, 1)
        self.assertIn("property not allowed", text)

    def test_missing_required_field_is_refused(self):
        code, text = self.write([entry(sha256=None)])
        self.assertEqual(code, 1)
        self.assertIn("required field missing: sha256", text)

    def test_catalog_version_must_be_one(self):
        code, text = self.write([], catalogVersion=2)
        self.assertEqual(code, 1)
        self.assertIn("catalogVersion", text)

    def test_other_kit_api_major_is_refused(self):
        code, text = self.write([entry(kitApi="2.0")])
        self.assertEqual(code, 1)
        self.assertIn("unsupported API version", text)

    def test_unknown_permission_is_refused(self):
        code, text = self.write([entry(permissions=["fs.write"])])
        self.assertEqual(code, 1)
        self.assertIn("permissions[0]", text)

    def test_kit_manage_is_refused_as_every_client_refuses_it(self):
        code, text = self.write([entry(permissions=["storage", "kit.manage"])])
        self.assertEqual(code, 1)
        self.assertIn("permissions[1]", text)

    def test_tags_and_preview_pass(self):
        code, text = self.write([entry(tags=["dark", "glass-2"], preview="https://example.com/demo.png")])
        self.assertEqual(code, 0, text)
        for ending in ("jpg", "jpeg"):
            code, text = self.write([entry(preview=f"https://example.com/demo.{ending}")])
            self.assertEqual(code, 0, text)

    def test_translations_pass_for_french_only(self):
        code, text = self.write([entry(translations={"fr": {"name": "Démo", "description": "Une entrée de test."}})])
        self.assertEqual(code, 0, text)

    def test_bad_translations_are_refused(self):
        cases = {
            "unknown language": {"it": {"name": "Demo", "description": "Test."}},
            "missing description": {"fr": {"name": "Démo"}},
            "empty name": {"fr": {"name": "", "description": "Test."}},
            "name too long": {"fr": {"name": "a" * 61, "description": "Test."}},
            "description too long": {"fr": {"name": "Démo", "description": "a" * 301}},
            "extra field": {"fr": {"name": "Démo", "description": "Test.", "tags": []}},
            "not an object": {"fr": "Démo"},
        }
        for label, translations in cases.items():
            with self.subTest(label):
                code, text = self.write([entry(translations=translations)])
                self.assertEqual(code, 1)
                self.assertIn("extensions[0].translations", text)

    def test_bad_tags_are_refused(self):
        cases = {
            "capital": ["Dark"],
            "space": ["two words"],
            "leading hyphen": ["-dark"],
            "too long": ["a" * 21],
            "six tags": ["a", "b", "c", "d", "e", "f"],
            "duplicate": ["dark", "dark"],
        }
        for label, tags in cases.items():
            with self.subTest(label):
                code, text = self.write([entry(tags=tags)])
                self.assertEqual(code, 1)
                self.assertIn("extensions[0].tags", text)

    def test_preview_must_be_an_https_image(self):
        for value in ("http://example.com/demo.png", "https://example.com/demo.gif", "https://example.com/demo",
                      "demo.png", "https://example.com/a b.png", "https://example.com/" + "a" * 500 + ".png"):
            with self.subTest(value[:40]):
                code, text = self.write([entry(preview=value)])
                self.assertEqual(code, 1)
                self.assertIn("extensions[0].preview", text)

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

    def test_every_element_of_the_repository_is_valid(self):
        folders = ["themes", os.path.join("themes", "snippets"), "extensions", "apps", "examples"]
        paths = [os.path.join(REPO, parent, name, "manifest.json")
                 for parent in folders for name in sorted(os.listdir(os.path.join(REPO, parent)))
                 if os.path.isfile(os.path.join(REPO, parent, name, "manifest.json"))]
        self.assertGreaterEqual(len(paths), 15)
        for path in paths:
            with self.subTest(path=os.path.relpath(path, REPO)):
                code, text = run("manifest", path)
                self.assertEqual(code, 0, text)

    def test_valid_extension_passes(self):
        code, text = run("manifest", self.install(manifest(permissions=["storage", "network:api.example.com"])))
        self.assertEqual(code, 0, text)

    def test_folder_must_match_id(self):
        code, text = run("manifest", self.install(manifest(), folder="autre"))
        self.assertEqual(code, 1)
        self.assertIn("must be the folder name", text)

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
        self.assertIn("main: required", text)

    def test_main_must_exist_beside_manifest(self):
        code, text = run("manifest", self.install(manifest(), files=()))
        self.assertEqual(code, 1)
        self.assertIn("file missing from the folder", text)

    def test_path_traversal_is_refused(self):
        code, text = run("manifest", self.install(manifest(main="../evil.js")))
        self.assertEqual(code, 1)
        self.assertIn("main", text)

    def test_unknown_permission_is_refused(self):
        code, text = run("manifest", self.install(manifest(permissions=["fs.write"])))
        self.assertEqual(code, 1)
        self.assertIn("permissions[0]", text)

    def test_permissions_used_by_the_parity_paths_are_accepted(self):
        granted = ["ipc.discord", "services.read", "library.write", "ui.window", "ui.tray", "midi.input"]
        code, text = run("manifest", self.install(manifest(permissions=granted)))
        self.assertEqual(code, 0, text)

    def test_kit_api_must_match_major_version(self):
        code, text = run("manifest", self.install(manifest(kitApi="2.0")))
        self.assertEqual(code, 1)
        self.assertIn("kitApi", text)

    def test_theme_needs_colors_or_resources(self):
        code, text = run("manifest", self.install(manifest(kind="theme", main=None), files=()))
        self.assertEqual(code, 1)
        self.assertIn("at least one of the two", text)

    def test_theme_with_colors_passes(self):
        data = manifest(kind="theme", main=None, colors="colors.ini", defaultScheme="Base")
        code, text = run("manifest", self.install(data, files=("colors.ini",)))
        self.assertEqual(code, 0, text)

    def test_type_is_script_or_module(self):
        for value in ("script", "module"):
            code, text = run("manifest", self.install(manifest(type=value)))
            self.assertEqual(code, 0, text)
        code, text = run("manifest", self.install(manifest(type="commonjs")))
        self.assertEqual(code, 1)
        self.assertIn("type", text)

    def test_tags_and_preview_pass(self):
        data = manifest(tags=["dark", "layout"], preview="preview.png")
        code, text = run("manifest", self.install(data, files=("main.js", "preview.png")))
        self.assertEqual(code, 0, text)

    def test_translations_pass_for_french_only(self):
        data = manifest(translations={"fr": {"name": "Démo", "description": "Un manifeste de test."}})
        code, text = run("manifest", self.install(data))
        self.assertEqual(code, 0, text)

    def test_bad_translations_are_refused(self):
        cases = {
            "unknown language": {"it": {"name": "Demo", "description": "Test."}},

            "missing name": {"fr": {"description": "Test."}},
            "description too long": {"fr": {"name": "Démo", "description": "a" * 301}},
            "extra field": {"fr": {"name": "Démo", "description": "Test.", "x": 1}},
        }
        for label, translations in cases.items():
            with self.subTest(label):
                code, text = run("manifest", self.install(manifest(translations=translations)))
                self.assertEqual(code, 1)
                self.assertIn("translations", text)

    def test_bad_tags_are_refused(self):
        cases = {
            "capital": ["Dark"],
            "space": ["two words"],
            "six tags": ["a", "b", "c", "d", "e", "f"],
            "duplicate": ["dark", "dark"],
        }
        for label, tags in cases.items():
            with self.subTest(label):
                code, text = run("manifest", self.install(manifest(tags=tags)))
                self.assertEqual(code, 1)
                self.assertIn("tags", text)

    def test_preview_must_be_a_png_or_jpeg_name_at_the_root(self):
        for value in ("preview.gif", "img/preview.png", "../preview.png", "preview"):
            with self.subTest(value):
                code, text = run("manifest", self.install(manifest(preview=value)))
                self.assertEqual(code, 1)
                self.assertIn("preview", text)

    def test_preview_file_must_exist(self):
        code, text = run("manifest", self.install(manifest(preview="preview.png")))
        self.assertEqual(code, 1)
        self.assertIn("preview: file missing from the folder: preview.png", text)

    def test_preview_file_is_at_most_two_megabytes(self):
        path = self.install(manifest(preview="preview.png"), files=("main.js", "preview.png"))
        image = os.path.join(os.path.dirname(path), "preview.png")
        with open(image, "wb") as handle:
            handle.write(b"x" * (2 * 1024 * 1024))
        code, text = run("manifest", path)
        self.assertEqual(code, 0, text)
        with open(image, "ab") as handle:
            handle.write(b"x")
        code, text = run("manifest", path)
        self.assertEqual(code, 1)
        self.assertIn("preview: file larger than 2 MB", text)

    def test_subfolders_are_allowed_and_links_are_refused(self):
        path = self.install(manifest(), files=("main.js",))
        folder = os.path.dirname(path)
        os.makedirs(os.path.join(folder, "lib"))
        with open(os.path.join(folder, "lib", "util.js"), "w", encoding="utf-8") as handle:
            handle.write("export const x = 1;\n")
        code, text = run("manifest", path)
        self.assertEqual(code, 0, text)
        try:
            os.symlink(os.path.join(self.tmp.name, "elsewhere.js"), os.path.join(folder, "lib", "link.js"))
        except (OSError, NotImplementedError):
            self.skipTest("no symbolic links on this system")
        code, text = run("manifest", path)
        self.assertEqual(code, 1)
        self.assertIn("symbolic link not allowed in the element: lib/link.js", text)

    def test_snippet_needs_resources_and_forbids_main(self):
        code, text = run("manifest", self.install(manifest(kind="snippet", resources="snippet.xaml"), files=("snippet.xaml", "main.js")))
        self.assertEqual(code, 1)
        self.assertIn("main: not allowed for a snippet", text)

    def test_snippet_of_a_user_css_alone_is_valid(self):
        data = manifest(id="css-only", kind="snippet", main=None)
        code, text = run("manifest", self.install(data, files=()))
        self.assertEqual(code, 1)
        self.assertIn("resources or user.css: at least one of the two is required for a snippet", text)
        code, text = run("manifest", self.install(data, files=("user.css",)))
        self.assertEqual(code, 0, text)

    def test_declared_settings_are_valid(self):
        settings = [
            {"key": "seconds", "label": "Show the seconds", "type": "toggle", "default": True},
            {"key": "side", "label": "Side", "type": "choice", "options": ["Left", "Right"], "default": "Left"},
            {"key": "greeting", "label": "Greeting", "type": "text", "default": ""},
            {"key": "size", "label": "Size", "type": "number", "min": 8, "max": 32, "default": 12},
        ]
        code, text = run("manifest", self.install(manifest(settings=settings)))
        self.assertEqual(code, 0, text)

    def test_settings_that_do_not_hold_are_refused(self):
        toggle = {"key": "a", "label": "A", "type": "toggle", "default": True}
        cases = {
            "default must be true or false": [dict(toggle, default="yes")],
            "default must be one of the options": [{"key": "a", "label": "A", "type": "choice",
                                                    "options": ["x", "y"], "default": "z"}],
            "options must be on one line, without spaces at either end": [
                {"key": "a", "label": "A", "type": "choice", "options": ["x", "y "], "default": "x"}],
            "default must be a text of 200 characters at most, on one line": [
                {"key": "a", "label": "A", "type": "text", "default": " padded"}],
            "default must be a number between min and max": [{"key": "a", "label": "A", "type": "number",
                                                              "min": 1, "max": 5, "default": 9}],
            "min and max belong to a number": [dict(toggle, min=1)],
            "key enabled is reserved": [dict(toggle, key="enabled")],
            "key a is already used": [toggle, dict(toggle, label="B")],
            "property not allowed": [dict(toggle, hint="x")],
        }
        for message, settings in cases.items():
            with self.subTest(message):
                code, text = run("manifest", self.install(manifest(settings=settings)))
                self.assertEqual(code, 1, text)
                self.assertIn(message, text)
        code, text = run("manifest", self.install(manifest(id="demo-app", kind="app", settings=[toggle])))
        self.assertEqual(code, 1)
        self.assertIn("settings: only an extension, a theme or a snippet declares settings", text)

    def test_settings_may_have_a_group_and_a_description(self):
        grouped = [
            {"key": "seconds", "label": "Show the seconds", "type": "toggle", "default": True,
             "group": "Clock", "description": "Adds the seconds to the clock."},
            {"key": "halo", "label": "Halo", "type": "toggle", "default": False},
        ]
        code, text = run("manifest", self.install(manifest(settings=grouped)))
        self.assertEqual(code, 0, text)

    def test_a_group_or_a_description_that_does_not_fit_is_refused(self):
        toggle = {"key": "a", "label": "A", "type": "toggle", "default": True}
        too_long = [dict(toggle, group="x" * 41), dict(toggle, description="x" * 201), dict(toggle, group="")]
        for settings in too_long:
            with self.subTest(settings=settings):
                code, text = run("manifest", self.install(manifest(settings=[settings])))
                self.assertEqual(code, 1, text)
        code, text = run("manifest", self.install(manifest(settings=[dict(toggle, description="two\nlines")])))
        self.assertEqual(code, 1, text)
        self.assertIn("description must be 1 to 200 characters, on one line", text)

    def test_a_theme_and_a_snippet_declare_settings(self):
        toggle = {"key": "hide-radio", "label": "Hide Radio", "type": "toggle", "default": False}
        theme = manifest(id="demo-theme", kind="theme", main=None, colors="colors.ini", settings=[toggle])
        code, text = run("manifest", self.install(theme, files=("colors.ini",)))
        self.assertEqual(code, 0, text)
        snippet = manifest(id="demo-snippet", kind="snippet", main=None, settings=[toggle])
        code, text = run("manifest", self.install(snippet, files=("user.css",)))
        self.assertEqual(code, 0, text)


try:
    from jsonschema import Draft202012Validator
except ImportError:  # optional: the cross-check only runs where jsonschema is installed
    Draft202012Validator = None


with open(os.path.join(REPO, "facts", "apple.json"), encoding="utf-8") as _facts:
    REQUIRED_EXPORTS = json.load(_facts)["requiredPlayerExports"]
with open(os.path.join(REPO, "facts", "release.json"), encoding="utf-8") as _facts:
    LANGUAGES = {language["code"] for language in json.load(_facts)["languages"]}


def tested_version(**changes):
    data = {"version": "1.1600.1.0", "tested": "2026-10-30", "exports": list(REQUIRED_EXPORTS)}
    return apply_changes(data, changes)


class VersionsTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.path = os.path.join(self.tmp.name, "versions.json")

    def tearDown(self):
        self.tmp.cleanup()

    def write(self, versions, **changes):
        data = {"versionsFormat": 1, "playerDll": "AVFoundationCF.dll", "versions": versions}
        write_json(self.path, apply_changes(data, changes))
        return run("versions", self.path)

    @needs_catalog
    def test_public_versions_are_valid(self):
        code, text = run("versions", os.path.join(CATALOG, "versions.json"))
        self.assertEqual(code, 0, text)

    def test_required_exports_are_those_the_kernel_is_built_with(self):
        self.assertEqual(validate.required_exports(), REQUIRED_EXPORTS)
        self.assertIn("AVCFPlayerSetVolume", REQUIRED_EXPORTS)

    def test_valid_version_passes(self):
        code, text = self.write([tested_version()])
        self.assertEqual(code, 0, text)

    def test_version_needs_four_numbers(self):
        code, text = self.write([tested_version(version="1.1600.1")])
        self.assertEqual(code, 1)
        self.assertIn("versions[0].version", text)

    def test_duplicate_versions_are_refused(self):
        code, text = self.write([tested_version(), tested_version()])
        self.assertEqual(code, 1)
        self.assertIn("already listed", text)

    def test_every_required_export_must_be_listed(self):
        code, text = self.write([tested_version(exports=REQUIRED_EXPORTS[1:])])
        self.assertEqual(code, 1)
        self.assertIn("export required by Ampify is missing: AVCFPlayerCopyCurrentItem", text)

    def test_unknown_export_is_refused(self):
        code, text = self.write([tested_version(exports=REQUIRED_EXPORTS + ["AVCFInventé"])])
        self.assertEqual(code, 1)

        code, text = self.write([tested_version(exports=REQUIRED_EXPORTS + ["AVCFInvented"])])
        self.assertEqual(code, 1)
        self.assertIn("not a known export", text)

    def test_other_dll_is_refused(self):
        code, text = self.write([tested_version()], playerDll="Other.dll")
        self.assertEqual(code, 1)
        self.assertIn("playerDll", text)


class CatalogVersionsPinTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.catalog = os.path.join(self.tmp.name, "catalog.json")
        self.versions = os.path.join(self.tmp.name, "versions.json")
        write_json(self.versions, {"versionsFormat": 1, "playerDll": "AVFoundationCF.dll", "versions": []})

    def tearDown(self):
        self.tmp.cleanup()

    def write(self, sha):
        pin = {"url": "https://example.com/catalog/versions.json", "sha256": sha}
        write_json(self.catalog, {"catalogVersion": 1, "versions": pin, "extensions": []})
        return run("catalog", self.catalog)

    def test_pin_matching_the_file_beside_the_catalog_passes(self):
        with open(self.versions, "rb") as handle:
            digest = hashlib.sha256(handle.read()).hexdigest()
        code, text = self.write(digest)
        self.assertEqual(code, 0, text)

    def test_stale_pin_is_refused(self):
        code, text = self.write("0" * 64)
        self.assertEqual(code, 1)
        self.assertIn("versions.sha256", text)


@unittest.skipIf(Draft202012Validator is None, "jsonschema is not installed: cross-check skipped")
class ReferenceAgreementTests(unittest.TestCase):
    """Random documents must get the same verdict from this validator and from jsonschema."""

    POOL = [
        "", "a", "demo-ext", "Demo", "1.0.0", "1.0", "1", "2.0", "https://a.b/c.zip", "http://a.b",
        "a\nb", "x" * 61, "../x.js", "main.js", "colors.ini", "network:api.example.com", "fs.write",
        "storage", "Shirowwww", "-bad", "é", "0" * 64, "a" * 64, "extension", "theme", "plugin", "module", "script",
        0, 1, 1.0, 1.5, -1, True, False, None, [], ["storage"], ["storage", "storage"], {}, {"a": 1},
    ]

    def mutate(self, rng, doc):
        doc = json.loads(json.dumps(doc))
        for _ in range(rng.randint(1, 3)):
            keys = list(doc)
            roll = rng.random()
            if keys and roll < 0.3:
                del doc[rng.choice(keys)]
            elif keys and roll < 0.7:
                doc[rng.choice(keys)] = rng.choice(self.POOL)
            else:
                doc[rng.choice(["extra", "permissions", "appVersions", "license", "resources", "type"])] = rng.choice(self.POOL)
        return doc

    def check(self, schema, base, mutator, count=1500):
        rng = random.Random(42)
        reference = Draft202012Validator(schema)
        for _ in range(count):
            doc = mutator(rng, base)
            errors = []
            validate.validate(doc, schema, schema, "", errors)
            self.assertEqual(not errors, reference.is_valid(doc), json.dumps(doc, sort_keys=True)[:200])

    def test_manifest_schema_agrees_with_jsonschema(self):
        base = {"id": "demo-ext", "name": "Demo", "version": "1.0.0", "kind": "extension", "author": "Shirowwww",
                "description": "x", "kitApi": "1.0", "main": "main.js"}
        self.check(load_schema("manifest"), base, self.mutate)

    def test_catalog_schema_agrees_with_jsonschema(self):
        entry = {"id": "demo-ext", "name": "Demo", "version": "1.0.0", "kind": "extension", "author": "Shirowwww",
                 "description": "x", "kitApi": "1.0", "url": "https://a.b/c.zip", "sha256": "a" * 64}
        base = {"catalogVersion": 1, "extensions": [entry]}
        self.check(load_schema("catalog"), base, self.mutate_catalog)

    def mutate_catalog(self, rng, doc):
        if rng.random() < 0.5:
            return self.mutate(rng, doc)
        entries = [self.mutate(rng, doc["extensions"][0]) for _ in range(rng.randint(0, 3))]
        return {"catalogVersion": rng.choice([1, 1.0, 2, "1"]), "extensions": entries}


def load_schema(kind):
    with open(validate.SCHEMAS[kind], encoding="utf-8") as handle:
        return json.load(handle)


def permission_patterns(node):
    """Every pattern of the schema that lists the permissions (the one that accepts network:...)."""
    if isinstance(node, dict):
        found = [node["pattern"]] if "network:" in str(node.get("pattern", "")) else []
        return found + [p for value in node.values() for p in permission_patterns(value)]
    if isinstance(node, list):
        return [p for value in node for p in permission_patterns(value)]
    return []


class SchemaSubsetTests(unittest.TestCase):
    def test_the_catalog_takes_the_permissions_a_manifest_takes_but_kit_manage(self):
        manifest = permission_patterns(load_schema("manifest"))
        catalog = permission_patterns(load_schema("catalog"))
        self.assertTrue(manifest and catalog)
        self.assertEqual({p.replace("kit\\.manage|", "") for p in manifest}, set(catalog),
                         "an element valid on its own must also be valid in the catalog, except the first-party kit.manage")

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


class SnippetContentTests(unittest.TestCase):
    """Every snippet of the repository: its texts in the eight languages and the element names it uses."""

    SNIPPETS = os.path.join(REPO, "themes", "snippets")

    @staticmethod
    def known_names():
        """The stable names of src/core/theme/builtins.cpp, the only ones a theme or a snippet may use."""
        with open(os.path.join(REPO, "src", "core", "theme", "builtins.cpp"), encoding="utf-8") as handle:
            return set(re.findall(r'\{"([a-z][a-z0-9-]*)",\s*\{', handle.read()))

    @staticmethod
    def class_names(css):
        """The classes a stylesheet uses: comments and url(...) paths are not selectors."""
        css = re.sub(r"/\*.*?\*/", "", css, flags=re.S)
        css = re.sub(r"url\([^)]*\)", "", css)
        return set(re.findall(r"(?<![\w-])\.([a-z][a-z0-9-]*)", css))

    def snippet_folders(self):
        return [os.path.join(self.SNIPPETS, name) for name in sorted(os.listdir(self.SNIPPETS))
                if os.path.isfile(os.path.join(self.SNIPPETS, name, "manifest.json"))]

    def test_every_snippet_has_all_eight_languages(self):
        for folder in self.snippet_folders():
            with open(os.path.join(folder, "manifest.json"), encoding="utf-8") as handle:
                data = json.load(handle)
            with self.subTest(snippet=data["id"]):
                self.assertEqual(set(data.get("translations", {})), LANGUAGES)
                for setting in data.get("settings", []):
                    for language, texts in data["translations"].items():
                        self.assertIn(setting["key"], texts.get("settings", {}), language)
                        for option in setting.get("options", []):
                            self.assertIn(option, texts.get("options", {}), language)

    def test_every_class_a_snippet_uses_is_a_known_name(self):
        names = self.known_names()
        self.assertGreater(len(names), 100)
        for folder in self.snippet_folders():
            path = os.path.join(folder, "user.css")
            if not os.path.isfile(path):
                continue
            with open(path, encoding="utf-8") as handle:
                unknown = sorted(self.class_names(handle.read()) - names)
            with self.subTest(snippet=os.path.basename(folder)):
                self.assertEqual(unknown, [])


class CheckFileTests(unittest.TestCase):
    def test_returns_the_problems_instead_of_printing_them(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "manifest.json")
            write_json(path, {"id": "x"})
            self.assertTrue(validate.check_file("manifest", path))

    def test_a_file_that_is_not_json_cannot_be_checked(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "manifest.json")
            with open(path, "w", encoding="utf-8") as handle:
                handle.write("{")
            with self.assertRaises(ValueError):
                validate.check_file("manifest", path)


if __name__ == "__main__":
    unittest.main()
