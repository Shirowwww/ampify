"""End-to-end tests of the `amk` command line: the real binary, a temporary kit, and local archives.

    AMK_BIN=build/amk python3 -m unittest discover -s tools -p "test_cli.py" -v

Set AMK_WINE=1 to run amk.exe under Wine (AMK_BIN is then the path to amk.exe). Under Wine the tests that need
tar.exe or curl.exe are skipped: Wine does not ship them, and Windows 10 and later do.

Downloads are local: catalogues point at file:// archives. A remote catalogue is refused unless it is https.
"""

import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
import unittest
import zipfile

TOOLS = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(TOOLS)
sys.path.insert(0, TOOLS)
import pack  # noqa: E402

AMK = os.environ.get("AMK_BIN") or os.path.join(ROOT, "build", "amk")
WINE = os.environ.get("AMK_WINE") == "1"
APP_ID = "AppleInc.AppleMusicWin_nzyj5cx40ttqa!App"


def native(path):
    """The path as amk.exe sees it: a Windows path under Wine, the path itself elsewhere."""
    if not WINE:
        return path
    return subprocess.run(["winepath", "-w", path], capture_output=True, text=True, check=True).stdout.strip()


def file_url(path):
    text = native(path).replace("\\", "/")
    if len(text) > 1 and text[1] == ":":
        return "file:///" + text
    return "file://" + text


def read_json(path):
    with open(path, encoding="utf-8") as handle:
        return json.load(handle)


def sha256_of(path):
    with open(path, "rb") as handle:
        return hashlib.sha256(handle.read()).hexdigest()


@unittest.skipUnless(os.path.exists(AMK), "build amk first (cmake --build build), or set AMK_BIN")
class AmkTestCase(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="amk-e2e-")
        self.addCleanup(shutil.rmtree, self.tmp, True)
        self.kit = os.path.join(self.tmp, "kit")
        self.packages = os.path.join(self.tmp, "packages")
        os.makedirs(self.packages)

    def amk(self, *args, input_text="", cwd=None, timeout=60):
        command = (["wine"] if WINE else []) + [AMK, "--kit-dir", native(self.kit)] + list(args)
        return subprocess.run(
            command, input=input_text, capture_output=True, text=True, encoding="utf-8",
            cwd=cwd or self.tmp, timeout=timeout,
        )

    def write_element(self, name, kind, version="1.0.0", files=None, permissions=None, folder_name=None):
        """Writes an element folder in the test area and returns its path."""
        folder = os.path.join(self.tmp, "src", folder_name or name)
        os.makedirs(folder, exist_ok=True)
        manifest = {
            "id": name,
            "name": name.title(),
            "version": version,
            "kind": kind,
            "author": "Shirowwww",
            "description": f"Élément de test {name}.",
            "kitApi": "1.0",
        }
        if kind == "extension":
            manifest["main"] = "main.js"
            files = {"main.js": 'AMK.log("ok");\n', **(files or {})}
        if kind == "theme":
            manifest["colors"] = "colors.ini"
            manifest["defaultScheme"] = "Dark"
            files = {"colors.ini": "[Dark]\naccent = FA2D48\n", **(files or {})}
        if kind == "snippet":
            manifest["resources"] = "resources.xaml"
            files = {"resources.xaml": "<ResourceDictionary/>\n", **(files or {})}
        if permissions:
            manifest["permissions"] = permissions
        with open(os.path.join(folder, "manifest.json"), "w", encoding="utf-8") as handle:
            json.dump(manifest, handle)
        for relative, text in (files or {}).items():
            with open(os.path.join(folder, relative), "w", encoding="utf-8") as handle:
                handle.write(text)
        return folder

    def package(self, folder):
        """Packs an element folder into the test area. Returns (archive path, sha256)."""
        archive = os.path.join(self.packages, os.path.basename(folder) + "-" + str(time.monotonic_ns()) + ".zip")
        result = pack.pack(folder, archive)
        return archive, result["sha256"]

    def write_catalog(self, entries, name="catalog.json"):
        path = os.path.join(self.tmp, name)
        with open(path, "w", encoding="utf-8") as handle:
            json.dump({"catalogVersion": 1, "extensions": entries}, handle, ensure_ascii=False)
        return path

    def entry_for(self, folder, url=None, sha=None):
        manifest = read_json(os.path.join(folder, "manifest.json"))
        archive, digest = self.package(folder)
        entry = pack.catalog_entry(manifest, url or file_url(archive), sha or digest)
        return entry, archive

    def catalog_option(self, path):
        return ["--catalog", native(path)]

    def settings_text(self):
        path = os.path.join(self.kit, "settings.txt")
        if not os.path.exists(path):
            return ""
        with open(path, encoding="utf-8") as handle:
            return handle.read()

    def read(self, *parts):
        with open(os.path.join(self.kit, *parts), encoding="utf-8") as handle:
            return handle.read()


class Basics(AmkTestCase):
    def test_version_and_help(self):
        result = self.amk("version")
        self.assertEqual(result.returncode, 0)
        self.assertIn("amk ", result.stdout)
        help_text = self.amk("help")
        self.assertEqual(help_text.returncode, 0)
        self.assertIn("install <id>", help_text.stdout)

    def test_unknown_command_is_a_usage_error(self):
        result = self.amk("inventer")
        self.assertEqual(result.returncode, 2)
        self.assertIn("commande inconnue", result.stderr)

    def test_unknown_option_is_refused(self):
        command = (["wine"] if WINE else []) + [AMK, "--frobnicate", "list"]
        result = subprocess.run(command, capture_output=True, text=True)
        self.assertEqual(result.returncode, 2)

    def test_path_names_the_kit(self):
        result = self.amk("path")
        self.assertEqual(result.returncode, 0)
        self.assertIn("Dossier du kit", result.stdout)
        self.assertIn(APP_ID, result.stdout)

    def test_config_reads_writes_and_checks_values(self):
        self.assertEqual(self.amk("config", "theme.parts.colors", "off").returncode, 0)
        self.assertIn("theme.parts.colors = 0", self.settings_text())
        self.assertEqual(self.amk("config", "theme.parts.colors").stdout.strip(), "0")
        self.assertEqual(self.amk("config", "theme.parts.colors", "yes").returncode, 0)
        self.assertIn("theme.parts.colors = 1", self.settings_text())

        self.assertEqual(self.amk("config", "theme.parts.colors", "peut-être").returncode, 1)
        self.assertEqual(self.amk("config", "theme.active", "Pas Bon").returncode, 1)
        self.assertEqual(self.amk("config", "catalog.url", "http://example.com/c.json").returncode, 1)
        self.assertEqual(self.amk("config", "clé.inconnue").returncode, 1)

    def test_config_lists_every_key(self):
        self.amk("config", "theme.active", "night")
        listing = self.amk("config").stdout
        self.assertIn("theme.active = night", listing)


class Authoring(AmkTestCase):
    def test_validate_accepts_the_example(self):
        example = os.path.join(ROOT, "examples", "hello-extension")
        result = self.amk("validate", example)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("valide", result.stdout)

    def test_validate_reports_a_missing_main_file(self):
        folder = self.write_element("broken", "extension")
        os.remove(os.path.join(folder, "main.js"))
        result = self.amk("validate", folder)
        self.assertEqual(result.returncode, 1)
        self.assertIn("main : fichier absent du dossier : main.js", result.stderr)

    def test_validate_catalog_files(self):
        result = self.amk("validate", os.path.join(ROOT, "catalog", "catalog.json"))
        self.assertEqual(result.returncode, 0, result.stderr)
        local = self.write_catalog([{"id": "demo"}])
        self.assertEqual(self.amk("validate", local).returncode, 1)

    def test_new_creates_a_valid_element_of_every_kind(self):
        for kind in ("extension", "theme", "snippet", "app"):
            name = "sample-" + kind
            result = self.amk("new", kind, name, cwd=self.tmp)
            self.assertEqual(result.returncode, 0, result.stderr)
            check = self.amk("validate", os.path.join(self.tmp, name))
            self.assertEqual(check.returncode, 0, check.stderr)

    def test_new_refuses_to_overwrite(self):
        self.assertEqual(self.amk("new", "theme", "twice", cwd=self.tmp).returncode, 0)
        self.assertEqual(self.amk("new", "theme", "twice", cwd=self.tmp).returncode, 1)


@unittest.skipIf(WINE, "install needs tar.exe and curl.exe, which Wine does not ship")
class Marketplace(AmkTestCase):
    def setUp(self):
        super().setUp()
        self.demo_folder = self.write_element("demo", "extension", permissions=["storage"])
        self.night_folder = self.write_element("night", "theme")
        self.calm_folder = self.write_element("calm", "snippet")
        self.demo_entry, self.demo_archive = self.entry_for(self.demo_folder)
        self.night_entry, _ = self.entry_for(self.night_folder)
        self.calm_entry, _ = self.entry_for(self.calm_folder)
        self.catalog = self.write_catalog([self.demo_entry, self.night_entry, self.calm_entry])

    def install(self, name, catalog=None):
        return self.amk("install", name, "--yes", *self.catalog_option(catalog or self.catalog))

    def test_market_lists_the_catalog(self):
        result = self.amk("market", *self.catalog_option(self.catalog))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("demo", result.stdout)
        self.assertIn("non installé", result.stdout)

    def test_install_asks_before_writing_without_yes(self):
        result = self.amk("install", "demo", *self.catalog_option(self.catalog), input_text="non\n")
        self.assertEqual(result.returncode, 1)
        self.assertIn("Permissions demandées : storage", result.stdout)
        self.assertIn("Annulé", result.stdout)
        self.assertFalse(os.path.exists(os.path.join(self.kit, "extensions", "demo")))

    def test_extension_lifecycle_through_apply(self):
        install = self.install("demo")
        self.assertEqual(install.returncode, 0, install.stderr)
        self.assertIn("désactivé", install.stdout)
        self.assertIn("amk extension enable demo", install.stdout)

        self.assertIn("demo", self.amk("list").stdout)
        enable = self.amk("extension", "enable", "demo")
        self.assertEqual(enable.returncode, 0, enable.stderr)
        self.assertIn("Permissions demandées : storage", enable.stdout)
        self.assertIn("extensions.demo.enabled = 1", self.settings_text())

        apply = self.amk("apply")
        self.assertIn("extension demo 1.0.0", self.read("active.txt"))
        self.assertTrue(os.path.exists(os.path.join(self.kit, "reload.txt")))
        self.assertIn("charge    extension demo", apply.stdout)

        self.assertEqual(self.amk("extension", "disable", "demo").returncode, 0)
        self.amk("apply")
        self.assertNotIn("extension demo", self.read("active.txt"))

    def test_checksum_mismatch_is_refused(self):
        wrong = dict(self.demo_entry, id="broken", sha256="0" * 64)
        catalog = self.write_catalog([wrong])
        result = self.install("broken", catalog)
        self.assertEqual(result.returncode, 1)
        self.assertIn("empreinte SHA-256", result.stderr)
        self.assertFalse(os.path.exists(os.path.join(self.kit, "extensions", "broken")))

    def test_path_traversal_in_an_archive_is_refused(self):
        evil_archive = os.path.join(self.packages, "evil.zip")
        with zipfile.ZipFile(evil_archive, "w") as bundle:
            with open(os.path.join(self.demo_folder, "manifest.json"), encoding="utf-8") as handle:
                bundle.writestr("manifest.json", handle.read().replace('"demo"', '"evil"').replace('"Demo"', '"Evil"'))
            bundle.writestr("../escape.js", "AMK.log('pwned');")
        manifest = read_json(os.path.join(self.demo_folder, "manifest.json"))
        entry = pack.catalog_entry(dict(manifest, id="evil"), file_url(evil_archive), sha256_of(evil_archive))
        catalog = self.write_catalog([entry])

        result = self.install("evil", catalog)
        self.assertEqual(result.returncode, 1)
        self.assertIn("archive refusée", result.stderr)
        self.assertFalse(os.path.exists(os.path.join(self.kit, "escape.js")))
        self.assertFalse(os.path.exists(os.path.join(self.kit, "extensions", "evil")))

    def test_theme_selection_scheme_and_clear(self):
        self.assertEqual(self.install("night").returncode, 0)
        choose = self.amk("theme", "set", "night", "--scheme", "Dark")
        self.assertEqual(choose.returncode, 0, choose.stderr)
        self.assertIn("theme.active = night", self.settings_text())
        self.amk("apply")
        self.assertIn("theme night 1.0.0 scheme=Dark", self.read("active.txt"))

        self.assertEqual(self.amk("theme", "clear").returncode, 0)
        self.amk("apply")
        self.assertNotIn("theme night", self.read("active.txt"))

    def test_snippet_enable_and_plus_minus_syntax(self):
        self.assertEqual(self.install("calm").returncode, 0)
        self.assertEqual(self.amk("snippet", "enable", "calm").returncode, 0)
        self.assertIn("snippets.calm.enabled = 1", self.settings_text())

        plus = self.amk("config", "extensions", "+demo")
        self.assertEqual(plus.returncode, 1, "demo is not installed yet, so it must not be enabled")
        self.assertIn("absent ou invalide", plus.stderr)

        self.assertEqual(self.install("demo").returncode, 0)
        self.assertEqual(self.amk("config", "extensions", "+demo").returncode, 0)
        self.assertIn("extensions.demo.enabled = 1", self.settings_text())
        self.assertEqual(self.amk("config", "extensions", "-demo").returncode, 0)
        self.assertIn("extensions.demo.enabled = 0", self.settings_text())

    def test_unknown_id_is_not_in_the_catalog(self):
        result = self.install("fantome")
        self.assertEqual(result.returncode, 1)
        self.assertIn("absent du catalogue", result.stderr)

    def test_remote_catalog_must_be_https(self):
        result = self.amk("market", "--catalog", "http://127.0.0.1:9/catalog.json")
        self.assertEqual(result.returncode, 1)

    def test_remove_keeps_a_backup_that_restores_the_element(self):
        self.assertEqual(self.install("demo").returncode, 0)
        self.amk("extension", "enable", "demo")
        removed = self.amk("remove", "demo")
        self.assertEqual(removed.returncode, 0, removed.stderr)
        snapshot = re.search(r"Sauvegarde faite avant : (\S+)", removed.stdout).group(1)
        self.assertFalse(os.path.exists(os.path.join(self.kit, "extensions", "demo")))
        self.assertNotIn("extensions.demo.enabled", self.settings_text())

        self.assertIn(snapshot, self.amk("backup", "list").stdout)
        restored = self.amk("restore", "--backup", snapshot)
        self.assertEqual(restored.returncode, 0, restored.stderr)
        self.assertTrue(os.path.exists(os.path.join(self.kit, "extensions", "demo", "manifest.json")))
        self.assertIn("extensions.demo.enabled = 1", self.settings_text())

    def test_update_element_takes_the_newer_catalog_version(self):
        self.assertEqual(self.install("demo").returncode, 0)
        same = self.amk("update", "demo", *self.catalog_option(self.catalog))
        self.assertIn("déjà à jour", same.stdout)

        newer_folder = self.write_element("demo", "extension", version="1.1.0", permissions=["storage"])
        newer_entry, _ = self.entry_for(newer_folder)
        catalog = self.write_catalog([newer_entry, self.night_entry, self.calm_entry])
        result = self.amk("update", "demo", "--yes", *self.catalog_option(catalog))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("mis à jour", result.stdout)
        self.assertIn('"version": "1.1.0"', self.read("extensions", "demo", "manifest.json"))

    def test_broken_element_is_reported_and_not_loaded(self):
        self.assertEqual(self.install("demo").returncode, 0)
        self.amk("extension", "enable", "demo")
        os.remove(os.path.join(self.kit, "extensions", "demo", "main.js"))
        self.assertIn("invalide", self.amk("list").stdout)
        apply = self.amk("apply")
        self.assertEqual(apply.returncode, 1)
        self.assertIn("extension demo", apply.stderr)
        self.assertNotIn("extension demo", self.read("active.txt"))


class Activation(AmkTestCase):
    def test_launch_dry_run_names_the_launcher(self):
        result = self.amk("launch", "--dry-run")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("amk_launcher.exe", result.stdout)

    def test_restore_dry_run_starts_the_store_app_without_kernel(self):
        result = self.amk("restore", "--dry-run")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(APP_ID, result.stdout)

    def test_backup_create_and_list(self):
        self.amk("config", "theme.active", "night")
        created = self.amk("backup")
        self.assertEqual(created.returncode, 0, created.stderr)
        name = re.search(r"Sauvegarde créée : (\S+)", created.stdout).group(1)
        self.assertIn(name, self.amk("backup", "list").stdout)
        self.assertEqual(self.amk("restore", "--backup", "absent-19700101-000000").returncode, 1)

    def test_watch_reacts_to_an_edit(self):
        self.amk("config", "theme.active", "")
        process = subprocess.Popen(
            ([AMK] if not WINE else ["wine", AMK]) + ["--kit-dir", native(self.kit), "watch",
                                                      "--interval", "100", "--max-cycles", "60"],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, encoding="utf-8",
        )
        time.sleep(1.5)
        self.amk("config", "theme.parts.colors", "0")
        output, errors = process.communicate(timeout=60)
        self.assertEqual(process.returncode, 0, errors)
        self.assertIn("application initiale", output)
        self.assertIn("modifications détectées", output)
        self.assertIn("theme.parts.colors = 0", self.settings_text())
        self.assertTrue(os.path.exists(os.path.join(self.kit, "active.txt")))

    def test_apply_refuses_an_enabled_element_that_is_not_installed(self):
        self.amk("config", "extensions.fantome.enabled", "1")
        result = self.amk("apply")
        self.assertEqual(result.returncode, 1)
        self.assertIn("activé, mais absent du kit", result.stderr)


class KitUpdate(AmkTestCase):
    def make_release(self, version, sums_hash=None, release_tag=None):
        bundle_dir = os.path.join(self.tmp, "release", version)
        os.makedirs(bundle_dir, exist_ok=True)
        archive = os.path.join(bundle_dir, f"amk-kit-{version}-windows-x64.zip")
        with zipfile.ZipFile(archive, "w") as bundle:
            for name in ("amk.exe", "amk_launcher.exe", "amk_kernel.dll"):
                bundle.writestr(name, f"{name} {version}")
        digest = sha256_of(archive)
        sums = os.path.join(bundle_dir, "SHA256SUMS.txt")
        with open(sums, "w", encoding="utf-8") as handle:
            handle.write(f"{sums_hash or digest}  amk-kit-{version}-windows-x64.zip\n")
        release = {
            "tag_name": release_tag or f"v{version}",
            "assets": [
                {"name": os.path.basename(archive), "browser_download_url": file_url(archive)},
                {"name": "SHA256SUMS.txt", "browser_download_url": file_url(sums)},
            ],
        }
        release_path = os.path.join(bundle_dir, "latest.json")
        with open(release_path, "w", encoding="utf-8") as handle:
            json.dump(release, handle)
        return release_path

    @unittest.skipIf(WINE, "update needs tar.exe, which Wine does not ship")
    def test_check_reports_a_newer_release(self):
        release = self.make_release("9.9.9")
        result = self.amk("update", "--check", "--release-url", file_url(release))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("Version 9.9.9 disponible", result.stdout)

    @unittest.skipIf(WINE, "update needs tar.exe, which Wine does not ship")
    def test_same_or_older_release_is_up_to_date(self):
        release = self.make_release("0.1.0")
        result = self.amk("update", "--release-url", file_url(release))
        self.assertIn("à jour", result.stdout)

    @unittest.skipIf(WINE, "update needs tar.exe, which Wine does not ship")
    def test_apply_replaces_the_files_and_keeps_the_old_ones(self):
        release = self.make_release("9.9.9")
        install_dir = os.path.join(self.tmp, "install")
        os.makedirs(install_dir)
        with open(os.path.join(install_dir, "amk.exe"), "w", encoding="utf-8") as handle:
            handle.write("old")

        result = self.amk("update", "--apply", "--release-url", file_url(release),
                          "--install-dir", native(install_dir))
        self.assertEqual(result.returncode, 0, result.stderr)
        with open(os.path.join(install_dir, "amk.exe"), encoding="utf-8") as handle:
            self.assertEqual(handle.read(), "amk.exe 9.9.9")
        with open(os.path.join(install_dir, "amk.exe.old"), encoding="utf-8") as handle:
            self.assertEqual(handle.read(), "old")
        self.assertTrue(os.path.exists(os.path.join(install_dir, "amk_kernel.dll")))

    @unittest.skipIf(WINE, "update needs tar.exe, which Wine does not ship")
    def test_wrong_checksum_refuses_the_update(self):
        release = self.make_release("9.9.9", sums_hash="f" * 64)
        result = self.amk("update", "--check", "--release-url", file_url(release))
        self.assertEqual(result.returncode, 0)  # --check only reads the version
        result = self.amk("update", "--apply", "--release-url", file_url(release),
                          "--install-dir", native(os.path.join(self.tmp, "install")))
        self.assertEqual(result.returncode, 1)
        self.assertIn("empreinte SHA-256", result.stderr)


def api_members():
    """Member names of section 5 of API.md (the last segment of each `AMK.a.b` path), checked against amk.d.ts.
    `React` and `components` are planned for M8 and are not declared yet."""
    with open(os.path.join(ROOT, "docs", "extensions", "API.md"), encoding="utf-8") as handle:
        text = handle.read()
    section = text[text.index("## 5. Objet global"):text.index("## 6. ")]
    paths = re.findall(r"AMK\.([A-Za-z_]\w*(?:\.[A-Za-z_]\w*)*)", section)
    names = {path.split(".")[-1] for path in paths}
    return sorted(names - {"React", "components"})


class Build(AmkTestCase):
    @unittest.skipIf(WINE, "build reads the zip back with tar.exe, which Wine does not ship")
    def test_build_writes_a_zip_that_zipfile_reads(self):
        folder = self.write_element("demo-build", "extension")
        out = os.path.join(self.tmp, "demo-build.zip")
        result = self.amk("build", native(folder), native(out))
        self.assertEqual(result.returncode, 0, result.stderr)
        with zipfile.ZipFile(out) as bundle:
            self.assertIsNone(bundle.testzip())
            self.assertEqual(sorted(bundle.namelist()), ["main.js", "manifest.json"])
        self.assertIn(sha256_of(out), result.stdout)

    @unittest.skipIf(WINE, "build reads the zip back with tar.exe, which Wine does not ship")
    def test_build_is_deterministic(self):
        folder = self.write_element("demo-same", "extension")
        first = os.path.join(self.tmp, "one.zip")
        second = os.path.join(self.tmp, "two.zip")
        self.assertEqual(self.amk("build", native(folder), native(first)).returncode, 0)
        self.assertEqual(self.amk("build", native(folder), native(second)).returncode, 0)
        self.assertEqual(sha256_of(first), sha256_of(second))

    def test_build_refuses_an_invalid_element_and_writes_nothing(self):
        folder = self.write_element("demo-bad", "extension")
        os.remove(os.path.join(folder, "main.js"))
        out = os.path.join(self.tmp, "bad.zip")
        result = self.amk("build", native(folder), native(out))
        self.assertEqual(result.returncode, 1)
        self.assertIn("invalide", result.stderr)
        self.assertFalse(os.path.exists(out))

    def test_build_refuses_to_overwrite(self):
        folder = self.write_element("demo-over", "extension")
        out = os.path.join(self.tmp, "over.zip")
        with open(out, "w", encoding="utf-8") as handle:
            handle.write("occupé")
        result = self.amk("build", native(folder), native(out))
        self.assertEqual(result.returncode, 1)
        self.assertIn("existe déjà", result.stderr)

    @unittest.skipIf(WINE, "install needs tar.exe, which Wine does not ship")
    def test_built_archive_installs_from_a_local_catalog(self):
        folder = self.write_element("demo-built", "extension")
        out = os.path.join(self.tmp, "demo-built.zip")
        built = self.amk("build", native(folder), native(out))
        self.assertEqual(built.returncode, 0, built.stderr)
        entry = json.loads(built.stdout[built.stdout.index("{"):])
        entry["url"] = file_url(out)
        catalog = self.write_catalog([entry])
        installed = self.amk("install", "demo-built", *self.catalog_option(catalog), "--yes")
        self.assertEqual(installed.returncode, 0, installed.stderr)
        self.assertTrue(os.path.exists(os.path.join(self.kit, "extensions", "demo-built", "manifest.json")))


class Inspection(AmkTestCase):
    def test_inspect_lists_files_with_checksums(self):
        folder = self.write_element("demo-look", "extension")
        result = self.amk("inspect", native(folder))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertRegex(result.stdout, r"[0-9a-f]{64}  main\.js")

    def test_inspect_an_installed_element_shows_its_state(self):
        folder = self.write_element("demo-state", "extension")
        self.assertEqual(self.amk("dev", native(folder), "--max-cycles", "1").returncode, 0)
        result = self.amk("inspect", "extension", "demo-state")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertRegex(result.stdout, r"activé\s+oui")

    def test_inspect_rejects_an_unknown_kind(self):
        result = self.amk("inspect", "widget", "demo")
        self.assertEqual(result.returncode, 1)
        self.assertIn("type inconnu", result.stderr)

    def test_types_declares_every_member_of_section_5(self):
        path = os.path.join(self.tmp, "amk.d.ts")
        result = self.amk("types", native(path))
        self.assertEqual(result.returncode, 0, result.stderr)
        with open(path, encoding="utf-8") as handle:
            declared = handle.read()
        missing = [name for name in api_members() if not re.search(r"\b%s\b" % re.escape(name), declared)]
        self.assertEqual(missing, [])

    def test_types_refuses_to_overwrite_without_yes(self):
        path = os.path.join(self.tmp, "amk.d.ts")
        with open(path, "w", encoding="utf-8") as handle:
            handle.write("mon fichier")
        result = self.amk("types", native(path))
        self.assertEqual(result.returncode, 1)
        self.assertIn("existe déjà", result.stderr)
        with open(path, encoding="utf-8") as handle:
            self.assertEqual(handle.read(), "mon fichier")
        self.assertEqual(self.amk("types", native(path), "--yes").returncode, 0)


class Diagnostics(AmkTestCase):
    def test_empty_kit_is_a_warning_not_an_error(self):
        result = self.amk("doctor")
        self.assertIn("dossier du kit absent", result.stdout)
        self.assertIn("Résumé", result.stdout)
        if WINE:
            # Wine ships neither tar.exe nor curl.exe. doctor reports them as errors, which is what it must do.
            self.assertEqual(result.returncode, 1, result.stdout)
            self.assertIn("tar.exe", result.stdout)
        else:
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("Résumé : 0 erreur(s)", result.stdout)

    def test_enabled_element_that_is_missing_is_reported(self):
        self.assertEqual(self.amk("config", "extensions.ghost.enabled", "1").returncode, 0)
        result = self.amk("doctor")
        self.assertIn("activé mais non chargé", result.stdout)
        self.assertIn("ghost", result.stdout)


class Development(AmkTestCase):
    def write_journal(self, text):
        os.makedirs(os.path.join(self.kit, "journal"), exist_ok=True)
        path = os.path.join(self.kit, "journal", "AppleMusic.log")
        with open(path, "w", encoding="utf-8") as handle:
            handle.write(text)
        return path

    def test_logs_prints_the_last_lines(self):
        self.write_journal("un\ndeux\ntrois\nquatre\ncinq\n")
        result = self.amk("logs", "2")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.splitlines(), ["quatre", "cinq"])

    def test_logs_without_journal_says_so(self):
        result = self.amk("logs")
        self.assertEqual(result.returncode, 0)
        self.assertIn("Aucun journal", result.stdout)

    def test_logs_follow_prints_lines_added_later(self):
        path = self.write_journal("avant\n")
        command = (["wine"] if WINE else []) + [
            AMK, "--kit-dir", native(self.kit), "logs", "--follow", "--interval", "100", "--max-cycles", "40",
        ]
        process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, encoding="utf-8")
        time.sleep(0.5)
        with open(path, "a", encoding="utf-8") as handle:
            handle.write("après\n")
        out, _ = process.communicate(timeout=60)
        self.assertEqual(process.returncode, 0)
        self.assertEqual(out.splitlines(), ["avant", "après"])

    def test_dev_copies_the_element_into_the_kit_and_enables_it(self):
        folder = self.write_element("demo-dev", "extension")
        result = self.amk("dev", native(folder), "--max-cycles", "1")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(os.path.exists(os.path.join(self.kit, "extensions", "demo-dev", "main.js")))
        self.assertRegex(self.settings_text(), r"extensions\.demo-dev\.enabled\s*=\s*1")

    def test_dev_keeps_the_working_copy_when_an_edit_breaks_it(self):
        folder = self.write_element("demo-broken", "extension")
        command = (["wine"] if WINE else []) + [
            AMK, "--kit-dir", native(self.kit), "dev", native(folder), "--interval", "100", "--max-cycles", "40",
        ]
        process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, encoding="utf-8")
        time.sleep(0.8)
        with open(os.path.join(folder, "manifest.json"), "w", encoding="utf-8") as handle:
            handle.write("{ casse")
        out, err = process.communicate(timeout=60)
        self.assertEqual(process.returncode, 0)
        self.assertIn("rechargé", out)
        self.assertIn("invalide", err)
        self.assertEqual(read_json(os.path.join(self.kit, "extensions", "demo-broken", "manifest.json"))["id"], "demo-broken")

    def test_dev_refuses_an_invalid_element(self):
        folder = self.write_element("demo-no-main", "extension")
        os.remove(os.path.join(folder, "main.js"))
        result = self.amk("dev", native(folder), "--max-cycles", "1")
        self.assertEqual(result.returncode, 1)
        self.assertFalse(os.path.exists(os.path.join(self.kit, "extensions", "demo-no-main")))

    def test_dev_asks_the_kernel_to_restart_the_element(self):
        folder = self.write_element("demo-restart", "extension")
        result = self.amk("dev", native(folder), "--max-cycles", "1")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("restart demo-restart", self.read("reload.txt").splitlines())

    def test_apply_asks_for_no_restart(self):
        result = self.amk("apply")
        self.assertIn(result.returncode, (0, 1), result.stderr)
        self.assertFalse(any(line.startswith("restart ") for line in self.read("reload.txt").splitlines()))


class Verification(AmkTestCase):
    def test_test_runs_the_example_in_the_engine(self):
        folder = os.path.join(ROOT, "examples", "hello-extension")
        result = self.amk("test", native(folder))
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("chargé et exécuté sans erreur", result.stdout)
        self.assertIn("Bonjour, lancement n° 1", result.stdout)

    def test_test_accepts_a_folder_with_a_trailing_separator(self):
        folder = os.path.join(ROOT, "examples", "gain-boost")
        result = self.amk("test", native(folder) + ("\\" if WINE else "/"))
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("gain appliqué", result.stdout)

    def test_test_refuses_a_call_without_its_permission(self):
        folder = self.write_element("demo-denied", "extension", files={"main.js": 'AMK.storage.get("x", 0);\n'})
        result = self.amk("test", native(folder))
        self.assertEqual(result.returncode, 1)
        self.assertIn("permission manquante : storage", result.stdout)

    def test_test_writes_nothing_to_the_kit(self):
        folder = os.path.join(ROOT, "examples", "sleep-timer")
        result = self.amk("test", native(folder))
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertFalse(os.path.exists(self.kit))


class Transfer(AmkTestCase):
    def export_one(self, name):
        folder = self.write_element(name, "extension")
        self.assertEqual(self.amk("dev", native(folder), "--max-cycles", "1").returncode, 0)
        out = os.path.join(self.tmp, name + ".zip")
        result = self.amk("export", native(out))
        self.assertEqual(result.returncode, 0, result.stderr)
        return out

    def test_profile_save_use_list_and_delete(self):
        self.assertEqual(self.amk("config", "catalog.url", "https://example.org/a.json").returncode, 0)
        self.assertEqual(self.amk("profile", "save", "travail").returncode, 0)
        self.assertEqual(self.amk("config", "catalog.url", "https://example.org/b.json").returncode, 0)
        result = self.amk("profile", "use", "travail")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("État précédent sauvegardé", result.stdout)
        self.assertEqual(self.amk("config", "catalog.url").stdout.strip(), "https://example.org/a.json")
        self.assertIn("travail", self.amk("profile", "list").stdout.split())
        self.assertEqual(self.amk("profile", "delete", "travail").returncode, 0)
        self.assertIn("Aucun profil", self.amk("profile", "list").stdout)

    def test_profile_name_and_missing_profile_are_refused(self):
        self.assertEqual(self.amk("profile", "save", "Pas Bon").returncode, 1)
        result = self.amk("profile", "use", "absent")
        self.assertEqual(result.returncode, 1)
        self.assertIn("introuvable", result.stderr)

    def test_export_writes_no_app_or_state_files(self):
        out = self.export_one("demo-export")
        with zipfile.ZipFile(out) as bundle:
            names = bundle.namelist()
        self.assertIn("AMK-EXPORT", names)
        self.assertIn("extensions/demo-export/manifest.json", names)
        self.assertFalse(any(name.startswith(("backups", "journal", "profiles", "cache")) for name in names))

    def test_export_refuses_to_overwrite(self):
        out = os.path.join(self.tmp, "taken.zip")
        with open(out, "w", encoding="utf-8") as handle:
            handle.write("occupé")
        result = self.amk("export", native(out))
        self.assertEqual(result.returncode, 1)
        self.assertIn("existe déjà", result.stderr)

    @unittest.skipIf(WINE, "import unpacks with tar.exe, which Wine does not ship")
    def test_export_then_import_restores_elements_and_settings(self):
        out = self.export_one("demo-trans")
        shutil.rmtree(self.kit)
        result = self.amk("import", native(out), "--yes")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(os.path.exists(os.path.join(self.kit, "extensions", "demo-trans", "manifest.json")))
        self.assertRegex(self.settings_text(), r"extensions\.demo-trans\.enabled\s*=\s*1")

    @unittest.skipIf(WINE, "import unpacks with tar.exe, which Wine does not ship")
    def test_import_dry_run_writes_nothing(self):
        out = self.export_one("demo-dry")
        shutil.rmtree(self.kit)
        result = self.amk("import", native(out), "--dry-run")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("rien n'a été écrit", result.stdout)
        self.assertFalse(os.path.exists(os.path.join(self.kit, "extensions")))

    @unittest.skipIf(WINE, "import unpacks with tar.exe, which Wine does not ship")
    def test_import_refuses_an_archive_that_is_not_an_export(self):
        folder = self.write_element("demo-plain", "extension")
        archive, _ = self.package(folder)
        result = self.amk("import", native(archive), "--yes")
        self.assertEqual(result.returncode, 1)
        self.assertIn("n'est pas un export", result.stderr)


if __name__ == "__main__":
    unittest.main()
