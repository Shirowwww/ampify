"""End-to-end tests of the `ampify` command line: the real binary, a temporary kit, and local archives.

    AMPIFY_BIN=build/ampify python3 -m unittest discover -s tools -p "test_cli.py" -v

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

Ampify = os.environ.get("AMPIFY_BIN") or os.path.join(ROOT, "build", "ampify")
APP_ID = "AppleInc.AppleMusicWin_nzyj5cx40ttqa!App"


def file_url(path):
    text = path.replace("\\", "/")
    if len(text) > 1 and text[1] == ":":
        return "file:///" + text
    return "file://" + text


def read_json(path):
    with open(path, encoding="utf-8") as handle:
        return json.load(handle)


def sha256_of(path):
    with open(path, "rb") as handle:
        return hashlib.sha256(handle.read()).hexdigest()


@unittest.skipUnless(os.path.exists(Ampify), "build ampify first (cmake --build build), or set AMPIFY_BIN")
class AmpifyTestCase(unittest.TestCase):
    def setUp(self):
        # The installed copy turns the automatic start on with apply: it would change the PC, not a test folder.
        installed = os.path.join(os.environ.get("LOCALAPPDATA", ""), "Programs", "Ampify")
        if os.name == "nt" and os.path.normcase(os.path.dirname(os.path.abspath(Ampify))) == os.path.normcase(installed):
            self.skipTest("AMPIFY_BIN is the installed copy: point it at a build folder")
        self.tmp = tempfile.mkdtemp(prefix="ampify-e2e-")
        self.addCleanup(shutil.rmtree, self.tmp, True)
        self.kit = os.path.join(self.tmp, "kit")
        self.packages = os.path.join(self.tmp, "packages")
        os.makedirs(self.packages)

    def ampify(self, *args, input_text="", cwd=None, timeout=60):
        command = [Ampify, "--kit-dir", self.kit] + list(args)
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
            "description": f"Test element {name}.",
            "kitApi": "1.0",
        }
        if kind in ("extension", "app"):
            manifest["main"] = "main.js"
            files = {"main.js": 'Ampify.log("ok");\n', **(files or {})}
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
        return ["--catalog", path]

    def settings_text(self):
        path = os.path.join(self.kit, "settings.txt")
        if not os.path.exists(path):
            return ""
        with open(path, encoding="utf-8") as handle:
            return handle.read()

    def read(self, *parts):
        with open(os.path.join(self.kit, *parts), encoding="utf-8") as handle:
            return handle.read()


class Basics(AmpifyTestCase):
    def test_version_and_help(self):
        result = self.ampify("version")
        self.assertEqual(result.returncode, 0)
        self.assertIn("ampify ", result.stdout)
        help_text = self.ampify("help")
        self.assertEqual(help_text.returncode, 0)
        self.assertIn("install <id>", help_text.stdout)

    def test_docs_cli_lists_every_command_of_help(self):
        with open(os.path.join(ROOT, "docs", "cli.md"), encoding="utf-8") as handle:
            docs = handle.read()
        missing = []
        for line in self.ampify("help").stdout.split("Options")[0].splitlines():
            if not re.match(r"  \S", line) or line.strip().startswith("ampify "):
                continue
            words = []
            for word in re.split(r"\s+", re.split(r"\s{2,}", line.strip())[0]):
                if not re.fullmatch(r"[a-z][a-z-]*", word):
                    break
                words.append(word)
            if words and "ampify " + " ".join(words) not in docs:
                missing.append(" ".join(words))
        self.assertEqual(missing, [], "commands of `ampify help` missing from docs/cli.md")

    def test_unknown_command_is_a_usage_error(self):
        result = self.ampify("inventer")
        self.assertEqual(result.returncode, 2)
        self.assertIn("unknown command", result.stderr)

    def test_unknown_option_is_refused(self):
        command = [Ampify, "--frobnicate", "list"]
        result = subprocess.run(command, capture_output=True, text=True)
        self.assertEqual(result.returncode, 2)

    def test_path_names_the_kit(self):
        result = self.ampify("path")
        self.assertEqual(result.returncode, 0)
        self.assertIn("Ampify folder", result.stdout)
        self.assertIn(APP_ID, result.stdout)

    def test_config_reads_writes_and_checks_values(self):
        self.assertEqual(self.ampify("config", "theme.parts.colors", "off").returncode, 0)
        self.assertIn("theme.parts.colors = 0", self.settings_text())
        self.assertEqual(self.ampify("config", "theme.parts.colors").stdout.strip(), "0")
        self.assertEqual(self.ampify("config", "theme.parts.colors", "yes").returncode, 0)
        self.assertIn("theme.parts.colors = 1", self.settings_text())

        self.assertEqual(self.ampify("config", "theme.parts.colors", "maybe").returncode, 1)
        self.assertEqual(self.ampify("config", "theme.active", "Pas Bon").returncode, 1)
        self.assertEqual(self.ampify("config", "catalog.url", "http://example.com/c.json").returncode, 1)
        self.assertEqual(self.ampify("config", "unknown.key").returncode, 1)

    def test_config_lists_every_key(self):
        self.ampify("config", "theme.active", "night")
        listing = self.ampify("config").stdout
        self.assertIn("theme.active = night", listing)


class Messages(AmpifyTestCase):
    def test_help_and_errors_are_in_english(self):
        help_text = self.ampify("help")
        self.assertEqual(help_text.returncode, 0)
        self.assertIn("Usage: ampify [options]", help_text.stdout)
        self.assertIn("install <id> [--yes]         installs from the catalog", help_text.stdout)
        self.assertNotIn("config language", help_text.stdout)
        unknown = self.ampify("nope")
        self.assertEqual(unknown.returncode, 2)
        self.assertIn("ampify: unknown command: nope", unknown.stderr)
        self.assertIn("Help: ampify help", unknown.stderr)
        self.assertIn("STATE", self.ampify("list").stdout.splitlines()[0])


class Services(AmpifyTestCase):
    def test_services_are_off_until_turned_on(self):
        for key in ("discord.enabled", "tray.enabled", "remote.enabled", "midi.enabled"):
            self.assertNotIn(key, self.settings_text())
            result = self.ampify("config", key, "maybe")
            self.assertEqual(result.returncode, 1, key)

    def test_turning_the_remote_on_creates_a_token(self):
        result = self.ampify("config", "remote.enabled", "1")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("Remote control token created", result.stdout)
        token = self.ampify("config", "remote.token").stdout.strip()
        self.assertRegex(token, r"^[A-Za-z0-9_-]{40}$")
        # Turning it on again keeps the token.
        self.ampify("config", "remote.enabled", "0")
        self.ampify("config", "remote.enabled", "1")
        self.assertEqual(self.ampify("config", "remote.token").stdout.strip(), token)
        self.assertEqual(self.ampify("config", "remote.token", "short").returncode, 1)
        self.assertEqual(self.ampify("config", "remote.port", "80").returncode, 1)
        self.assertEqual(self.ampify("config", "remote.port", "8974").returncode, 0)

    def test_midi_map_is_checked(self):
        self.assertEqual(self.ampify("config", "midi.map", "note:60=next;cc:7=gain").returncode, 0)
        result = self.ampify("config", "midi.map", "note:60=volume")
        self.assertEqual(result.returncode, 1)
        self.assertIn("unknown action", result.stderr)

    def test_examples_of_the_services_run_in_the_engine(self):
        expected = {
            "discord-presence": "clientId",
            "scrobbler": "Last.fm is not set up",
        }
        for name, text in expected.items():
            folder = os.path.join(ROOT, "examples", name)
            result = self.ampify("test", folder)
            self.assertEqual(result.returncode, 0, name + "\n" + result.stdout + result.stderr)
            self.assertIn(text, result.stdout, name)
            self.assertIn("loaded and run without error", result.stdout, name)


class Authoring(AmpifyTestCase):
    def test_validate_accepts_the_example(self):
        example = os.path.join(ROOT, "examples", "hello-extension")
        result = self.ampify("validate", example)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("valid", result.stdout)

    def test_validate_reports_a_missing_main_file(self):
        folder = self.write_element("broken", "extension")
        os.remove(os.path.join(folder, "main.js"))
        result = self.ampify("validate", folder)
        self.assertEqual(result.returncode, 1)
        self.assertIn("main: file missing from the folder: main.js", result.stderr)

    def test_validate_catalog_files(self):
        local = self.write_catalog([{"id": "demo"}])
        self.assertEqual(self.ampify("validate", local).returncode, 1)

    def test_new_creates_a_valid_element_of_every_kind(self):
        for kind in ("extension", "theme", "snippet", "app"):
            name = "sample-" + kind
            result = self.ampify("new", kind, name, cwd=self.tmp)
            self.assertEqual(result.returncode, 0, result.stderr)
            check = self.ampify("validate", os.path.join(self.tmp, name))
            self.assertEqual(check.returncode, 0, check.stderr)

    def test_new_refuses_to_overwrite(self):
        self.assertEqual(self.ampify("new", "theme", "twice", cwd=self.tmp).returncode, 0)
        self.assertEqual(self.ampify("new", "theme", "twice", cwd=self.tmp).returncode, 1)

    def test_new_theme_reads_clean_in_the_engine(self):
        self.assertEqual(self.ampify("new", "theme", "starter", cwd=self.tmp).returncode, 0)
        check = self.ampify("theme", "check", os.path.join(self.tmp, "starter"))
        self.assertEqual(check.returncode, 0, check.stdout + check.stderr)
        self.assertNotIn("Attention", check.stdout)
        # Stable names, and corners shown in XAML's order: the sidebar is rounded on the content side.
        self.assertIn(".sidebar → CornerRadius = 0,12,12,0", check.stdout)

    def test_new_starters_run_in_the_engine(self):
        for kind in ("extension", "app"):
            name = "run-" + kind
            self.assertEqual(self.ampify("new", kind, name, cwd=self.tmp).returncode, 0)
            result = self.ampify("test", os.path.join(self.tmp, name))
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(self.ampify("new", "snippet", "run-snippet", cwd=self.tmp).returncode, 0)
        check = self.ampify("theme", "check", os.path.join(self.tmp, "run-snippet"))
        self.assertEqual(check.returncode, 0, check.stdout + check.stderr)
        self.assertIn("CornerRadius = 16,16,16,16", check.stdout)

    def test_files_saved_with_a_byte_order_mark_are_read(self):
        # Notepad and PowerShell 5 save UTF-8 with a mark at the start: the manifest and the CSS still read.
        self.assertEqual(self.ampify("new", "snippet", "marked", cwd=self.tmp).returncode, 0)
        folder = os.path.join(self.tmp, "marked")
        for name in ("manifest.json", "user.css"):
            path = os.path.join(folder, name)
            with open(path, encoding="utf-8") as source:
                text = source.read()
            with open(path, "w", encoding="utf-8-sig") as target:
                target.write(text)
        validate = self.ampify("validate", folder)
        self.assertEqual(validate.returncode, 0, validate.stdout + validate.stderr)
        check = self.ampify("theme", "check", folder)
        self.assertEqual(check.returncode, 0, check.stdout + check.stderr)
        self.assertIn("CornerRadius = 16,16,16,16", check.stdout)


class SpicetifyImport(AmpifyTestCase):
    COLOR_INI = "[Nord]\nmain = 2E3440\nsidebar = 3B4252\ntext = ECEFF4\nbutton = 88C0D0\n\n[Snow]\nmain = ECEFF4\n"

    def write_spicetify_theme(self, name: str) -> str:
        folder = os.path.join(self.tmp, "spicetify", name)
        os.makedirs(folder)
        for relative, text in {"color.ini": self.COLOR_INI, "user.css": ".main-view { color: red; }\n",
                               "LICENSE": "MIT\n"}.items():
            with open(os.path.join(folder, relative), "w", encoding="utf-8") as handle:
                handle.write(text)
        return folder

    def test_import_keeps_color_ini_and_sets_spotify_css_aside(self):
        folder = self.write_spicetify_theme("Nord Theme")
        result = self.ampify("theme", "import", folder)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("Theme nord-theme imported", result.stdout)
        self.assertIn("ampify theme set nord-theme --scheme Nord", result.stdout)
        self.assertIn("main = 2E3440", self.read("themes", "nord-theme", "color.ini"))
        self.assertIn(".main-view", self.read("themes", "nord-theme", "spicetify-user.css"))
        self.assertNotIn(".main-view", self.read("themes", "nord-theme", "user.css"))
        manifest = json.loads(self.read("themes", "nord-theme", "manifest.json"))
        self.assertEqual(manifest["colors"], "color.ini")
        self.assertEqual(manifest["defaultScheme"], "Nord")
        chosen = self.ampify("theme", "set", "nord-theme", "--scheme", "Snow")
        self.assertEqual(chosen.returncode, 0, chosen.stderr)

    def test_import_refuses_to_replace_without_yes(self):
        folder = self.write_spicetify_theme("twice")
        self.assertEqual(self.ampify("theme", "import", folder).returncode, 0)
        self.assertEqual(self.ampify("theme", "import", folder).returncode, 1)
        self.assertEqual(self.ampify("theme", "import", folder, "--yes").returncode, 0)
        self.assertEqual(self.ampify("theme", "import", folder, "other").returncode, 0)

    def test_import_asks_which_theme_when_a_folder_holds_several(self):
        self.write_spicetify_theme("One")
        self.write_spicetify_theme("Two")
        result = self.ampify("theme", "import", os.path.join(self.tmp, "spicetify"))
        self.assertEqual(result.returncode, 1)
        self.assertIn("One", result.stderr)
        self.assertIn("Two", result.stderr)


class Marketplace(AmpifyTestCase):
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
        return self.ampify("install", name, "--yes", *self.catalog_option(catalog or self.catalog))

    def test_market_lists_the_catalog(self):
        result = self.ampify("market", *self.catalog_option(self.catalog))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("demo", result.stdout)
        self.assertIn("not installed", result.stdout)

    def market_json(self, catalog):
        result = self.ampify("market", "--json", *self.catalog_option(catalog))
        self.assertEqual(result.returncode, 0, result.stderr)
        return json.loads(result.stdout)

    def test_market_json_gives_tags_and_the_absolute_preview_of_an_installed_theme(self):
        folder = self.write_element("glow", "theme", files={"preview.png": "png"})
        manifest_path = os.path.join(folder, "manifest.json")
        manifest = read_json(manifest_path)
        manifest.update(tags=["dark", "glass"], preview="preview.png")
        with open(manifest_path, "w", encoding="utf-8") as handle:
            json.dump(manifest, handle)
        entry, _ = self.entry_for(folder)
        self.assertEqual(entry["tags"], ["dark", "glass"])
        catalog = self.write_catalog([entry, self.night_entry])
        self.assertEqual(self.install("glow", catalog).returncode, 0)
        self.assertEqual(self.install("night", catalog).returncode, 0)

        installed = {item["id"]: item for item in self.market_json(catalog)["installed"]}
        self.assertEqual(installed["glow"]["tags"], ["dark", "glass"])
        expected = os.path.join(self.kit, "themes", "glow", "preview.png")
        self.assertEqual(os.path.normcase(os.path.normpath(installed["glow"]["preview"])),
                         os.path.normcase(os.path.normpath(expected)))
        self.assertTrue(os.path.isabs(installed["glow"]["preview"]))
        self.assertEqual(installed["glow"]["schemes"], ["Dark"])
        self.assertEqual(installed["glow"]["defaultScheme"], "Dark")
        self.assertEqual(installed["night"]["tags"], [])
        self.assertNotIn("preview", installed["night"])

    def test_market_json_passes_on_the_translations_of_installed_and_catalog_elements(self):
        translations = {"fr": {"name": "Lueur", "description": "Un thème de test."}}
        folder = self.write_element("glow", "theme")
        manifest_path = os.path.join(folder, "manifest.json")
        manifest = read_json(manifest_path)
        manifest["translations"] = translations
        with open(manifest_path, "w", encoding="utf-8") as handle:
            json.dump(manifest, handle)
        entry, _ = self.entry_for(folder)
        self.assertNotIn("translations", entry)  # build leaves them out: Ampify 1.1.0 refuses the field
        entry["translations"] = translations  # a later catalog may carry them, and they are read
        catalog = self.write_catalog([entry, self.night_entry])
        self.assertEqual(self.install("glow", catalog).returncode, 0)
        self.assertEqual(self.install("night", catalog).returncode, 0)

        state = self.market_json(catalog)
        installed = {item["id"]: item for item in state["installed"]}
        listed = {item["id"]: item for item in state["catalog"]}
        self.assertEqual(installed["glow"]["translations"], translations)
        self.assertEqual(listed["glow"]["translations"], translations)
        self.assertNotIn("translations", installed["night"])
        self.assertNotIn("translations", listed["night"])

    def test_validate_refuses_a_translation_in_an_unknown_language(self):
        folder = self.write_element("glow", "theme")
        manifest_path = os.path.join(folder, "manifest.json")
        manifest = read_json(manifest_path)
        manifest["translations"] = {"de": {"name": "Glanz", "description": "Ein Test."}}
        with open(manifest_path, "w", encoding="utf-8") as handle:
            json.dump(manifest, handle)
        result = self.ampify("validate", folder)
        self.assertEqual(result.returncode, 1)
        self.assertIn("translations.de", result.stderr + result.stdout)

    def test_market_json_keeps_a_catalog_preview_only_from_a_known_host(self):
        def listed(name, **changes):
            return {**self.night_entry, "id": name, "url": "https://cdn.example.com/" + name + ".zip", **changes}

        catalog = self.write_catalog([
            listed("same-host", preview="https://CDN.example.com/same.png", tags=["dark"]),
            listed("other-host", preview="https://images.example.net/other.png"),
            listed("no-preview"),
        ])
        items = {item["id"]: item for item in self.market_json(catalog)["catalog"]}
        self.assertEqual(items["same-host"]["preview"], "https://CDN.example.com/same.png")
        self.assertEqual(items["same-host"]["tags"], ["dark"])
        self.assertNotIn("preview", items["other-host"])
        self.assertEqual(items["other-host"]["tags"], [])
        self.assertNotIn("preview", items["no-preview"])

    def test_build_prints_the_tags_in_the_catalog_entry(self):
        folder = self.write_element("tagged", "theme")
        manifest_path = os.path.join(folder, "manifest.json")
        manifest = read_json(manifest_path)
        manifest["tags"] = ["dark", "layout"]
        with open(manifest_path, "w", encoding="utf-8") as handle:
            json.dump(manifest, handle)
        result = self.ampify("build", folder, os.path.join(self.tmp, "tagged.zip"))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('"tags": ["dark", "layout"],', result.stdout)

    def test_install_asks_before_writing_without_yes(self):
        result = self.ampify("install", "demo", *self.catalog_option(self.catalog), input_text="no\n")
        self.assertEqual(result.returncode, 1)
        self.assertIn("Requested permissions: storage", result.stdout)
        self.assertIn("Cancelled", result.stdout)
        self.assertFalse(os.path.exists(os.path.join(self.kit, "extensions", "demo")))

    def test_extension_lifecycle_through_apply(self):
        install = self.install("demo")
        self.assertEqual(install.returncode, 0, install.stderr)
        self.assertIn("It is off", install.stdout)
        self.assertIn("ampify extension enable demo", install.stdout)

        self.assertIn("demo", self.ampify("list").stdout)
        enable = self.ampify("extension", "enable", "demo")
        self.assertEqual(enable.returncode, 0, enable.stderr)
        self.assertIn("Requested permissions: storage", enable.stdout)
        self.assertIn("extensions.demo.enabled = 1", self.settings_text())

        apply = self.ampify("apply")
        self.assertIn("extension demo 1.0.0", self.read("active.txt"))
        self.assertTrue(os.path.exists(os.path.join(self.kit, "reload.txt")))
        self.assertIn("loads     extension demo", apply.stdout)

        self.assertEqual(self.ampify("extension", "disable", "demo").returncode, 0)
        self.ampify("apply")
        self.assertNotIn("extension demo", self.read("active.txt"))

    def reload_lines(self):
        path = os.path.join(self.kit, "reload.txt")
        if not os.path.exists(path):
            return []
        return self.read("reload.txt").splitlines()

    def test_enable_and_disable_ask_a_running_kernel_to_start_and_stop(self):
        self.assertEqual(self.install("demo").returncode, 0)
        self.assertEqual(self.reload_lines()[-1], "rescan")  # the kernel loads the new folder, still disabled

        enable = self.ampify("extension", "enable", "demo")
        self.assertEqual(enable.returncode, 0, enable.stderr)
        self.assertIn("right away", enable.stdout)
        self.assertEqual(self.reload_lines()[-1], "start demo")

        self.assertEqual(self.ampify("extension", "disable", "demo").returncode, 0)
        self.assertEqual(self.reload_lines()[-1], "stop demo")

        self.assertEqual(self.ampify("config", "extensions", "+demo").returncode, 0)
        self.assertEqual(self.reload_lines()[-1], "start demo")
        self.assertEqual(self.ampify("config", "extensions", "-demo").returncode, 0)
        self.assertEqual(self.reload_lines()[-1], "stop demo")

        # Requests are added, never replaced, so the kernel cannot miss one between two passes.
        lines = self.reload_lines()
        self.assertEqual(lines[-5:], ["rescan", "start demo", "stop demo", "start demo", "stop demo"])

        # ampify apply adds its counter after them.
        self.ampify("apply")
        self.assertEqual(self.reload_lines()[:5], lines[:5])
        self.assertRegex(self.reload_lines()[-1], r"^[0-9]+$")

    def test_app_requests_name_the_app(self):
        page = self.write_element("demo", "app")
        entry, _ = self.entry_for(page)
        catalog = self.write_catalog([entry])
        self.assertEqual(self.install("demo", catalog).returncode, 0)
        enable = self.ampify("app", "enable", "demo")
        self.assertEqual(enable.returncode, 0, enable.stderr)
        self.assertIn("apps.demo.enabled = 1", self.settings_text())
        self.assertEqual(self.reload_lines()[-1], "start app:demo")
        self.assertEqual(self.ampify("app", "disable", "demo").returncode, 0)
        self.assertEqual(self.reload_lines()[-1], "stop app:demo")

    def test_snippet_enable_writes_no_request(self):
        self.assertEqual(self.install("calm").returncode, 0)
        before = self.reload_lines()
        self.assertEqual(self.ampify("snippet", "enable", "calm").returncode, 0)
        self.assertEqual(self.reload_lines(), before)

    def test_remove_and_update_ask_the_kernel_to_follow(self):
        self.assertEqual(self.install("demo").returncode, 0)
        newer_folder = self.write_element("demo", "extension", version="1.1.0", permissions=["storage"])
        newer_entry, _ = self.entry_for(newer_folder)
        catalog = self.write_catalog([newer_entry])
        self.assertEqual(self.ampify("update", "demo", "--yes", *self.catalog_option(catalog)).returncode, 0)
        self.assertEqual(self.reload_lines()[-1], "restart demo")

        self.assertEqual(self.ampify("remove", "demo").returncode, 0)
        self.assertEqual(self.reload_lines()[-2:], ["stop demo", "rescan"])

    def test_checksum_mismatch_is_refused(self):
        wrong = dict(self.demo_entry, id="broken", sha256="0" * 64)
        catalog = self.write_catalog([wrong])
        result = self.install("broken", catalog)
        self.assertEqual(result.returncode, 1)
        self.assertIn("SHA-256 differs", result.stderr)
        self.assertFalse(os.path.exists(os.path.join(self.kit, "extensions", "broken")))

    def test_path_traversal_in_an_archive_is_refused(self):
        evil_archive = os.path.join(self.packages, "evil.zip")
        with zipfile.ZipFile(evil_archive, "w") as bundle:
            with open(os.path.join(self.demo_folder, "manifest.json"), encoding="utf-8") as handle:
                bundle.writestr("manifest.json", handle.read().replace('"demo"', '"evil"').replace('"Demo"', '"Evil"'))
            bundle.writestr("../escape.js", "Ampify.log('pwned');")
        manifest = read_json(os.path.join(self.demo_folder, "manifest.json"))
        entry = pack.catalog_entry(dict(manifest, id="evil"), file_url(evil_archive), sha256_of(evil_archive))
        catalog = self.write_catalog([entry])

        result = self.install("evil", catalog)
        self.assertEqual(result.returncode, 1)
        self.assertIn("archive refused", result.stderr)
        self.assertFalse(os.path.exists(os.path.join(self.kit, "escape.js")))
        self.assertFalse(os.path.exists(os.path.join(self.kit, "extensions", "evil")))

    def test_theme_selection_scheme_and_clear(self):
        self.assertEqual(self.install("night").returncode, 0)
        choose = self.ampify("theme", "set", "night", "--scheme", "Dark")
        self.assertEqual(choose.returncode, 0, choose.stderr)
        self.assertIn("theme.active = night", self.settings_text())
        self.ampify("apply")
        self.assertIn("theme night 1.0.0 scheme=Dark", self.read("active.txt"))

        # A scheme belongs to the theme it was chosen for: setting the same theme again keeps it, another theme starts
        # on its own default, and "auto" follows Windows whatever the theme.
        again = self.ampify("theme", "set", "night")
        self.assertEqual(again.returncode, 0, again.stderr)
        self.assertIn("theme.scheme = Dark", self.settings_text())
        dusk_entry, _ = self.entry_for(self.write_element("dusk", "theme"))
        self.assertEqual(self.install("dusk", self.write_catalog([self.night_entry, dusk_entry])).returncode, 0)
        other = self.ampify("theme", "set", "dusk")
        self.assertEqual(other.returncode, 0, other.stderr)
        self.assertNotIn("theme.scheme", self.settings_text())
        self.assertEqual(self.ampify("config", "theme.scheme", "auto").returncode, 0)
        self.assertEqual(self.ampify("theme", "set", "night").returncode, 0)
        self.assertIn("theme.scheme = auto", self.settings_text())

        self.ampify("theme", "set", "night", "--scheme", "Dark")
        self.assertEqual(self.ampify("theme", "clear").returncode, 0)
        self.assertNotIn("theme.scheme", self.settings_text())
        self.ampify("apply")
        self.assertNotIn("theme night", self.read("active.txt"))

    def test_theme_set_keeps_the_schedule_and_the_other_settings(self):
        self.assertEqual(self.install("night").returncode, 0)
        self.assertEqual(self.ampify("config", "theme.schedule", "07:00=Light,19:00=Dark").returncode, 0)
        self.assertEqual(self.ampify("config", "theme.live_reload", "0").returncode, 0)
        self.assertEqual(self.ampify("theme", "set", "night").returncode, 0)
        text = self.settings_text()
        self.assertIn("theme.schedule = 07:00=Light,19:00=Dark", text)
        self.assertIn("theme.live_reload = 0", text)

    def test_theme_names_checks_the_stable_names_against_saved_trees(self):
        tree = os.path.join(self.tmp, "tree.json")
        with open(tree, "w", encoding="utf-8") as handle:
            handle.write('{"version":1,"nodes":[{"index":0,"type":"Grid","name":"Nothing","parent":-1,"depth":0}]}')
        result = self.ampify("theme", "names", tree)
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertIn("Stable names checked against 1 tree(s):", result.stdout)
        self.assertIn("Found in no tree", result.stdout)

        broken = os.path.join(self.tmp, "broken.json")
        with open(broken, "w", encoding="utf-8") as handle:
            handle.write("[]")
        self.assertEqual(self.ampify("theme", "names", broken).returncode, 2)
        self.assertEqual(self.ampify("theme", "names").returncode, 2)

    def test_snippet_enable_and_plus_minus_syntax(self):
        self.assertEqual(self.install("calm").returncode, 0)
        self.assertEqual(self.ampify("snippet", "enable", "calm").returncode, 0)
        self.assertIn("snippets.calm.enabled = 1", self.settings_text())

        plus = self.ampify("config", "extensions", "+demo")
        self.assertEqual(plus.returncode, 1, "demo is not installed yet, so it must not be enabled")
        self.assertIn("missing or invalid", plus.stderr)

        self.assertEqual(self.install("demo").returncode, 0)
        self.assertEqual(self.ampify("config", "extensions", "+demo").returncode, 0)
        self.assertIn("extensions.demo.enabled = 1", self.settings_text())
        self.assertEqual(self.ampify("config", "extensions", "-demo").returncode, 0)
        self.assertIn("extensions.demo.enabled = 0", self.settings_text())

    def test_unknown_id_is_not_in_the_catalog(self):
        result = self.install("fantome")
        self.assertEqual(result.returncode, 1)
        self.assertIn("not in the catalog", result.stderr)

    def test_remote_catalog_must_be_https(self):
        result = self.ampify("market", "--catalog", "http://127.0.0.1:9/catalog.json")
        self.assertEqual(result.returncode, 1)

    def test_remove_keeps_a_backup_that_restores_the_element(self):
        self.assertEqual(self.install("demo").returncode, 0)
        self.ampify("extension", "enable", "demo")
        removed = self.ampify("remove", "demo")
        self.assertEqual(removed.returncode, 0, removed.stderr)
        snapshot = re.search(r"Backup made first: (\S+)", removed.stdout).group(1)
        self.assertFalse(os.path.exists(os.path.join(self.kit, "extensions", "demo")))
        self.assertNotIn("extensions.demo.enabled", self.settings_text())

        self.assertIn(snapshot, self.ampify("backup", "list").stdout)
        restored = self.ampify("restore", "--backup", snapshot)
        self.assertEqual(restored.returncode, 0, restored.stderr)
        self.assertTrue(os.path.exists(os.path.join(self.kit, "extensions", "demo", "manifest.json")))
        self.assertIn("extensions.demo.enabled = 1", self.settings_text())

    def test_update_element_takes_the_newer_catalog_version(self):
        self.assertEqual(self.install("demo").returncode, 0)
        same = self.ampify("update", "demo", *self.catalog_option(self.catalog))
        self.assertIn("already up to date", same.stdout)

        newer_folder = self.write_element("demo", "extension", version="1.1.0", permissions=["storage"])
        newer_entry, _ = self.entry_for(newer_folder)
        catalog = self.write_catalog([newer_entry, self.night_entry, self.calm_entry])
        result = self.ampify("update", "demo", "--yes", *self.catalog_option(catalog))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("updated", result.stdout)
        self.assertIn('"version": "1.1.0"', self.read("extensions", "demo", "manifest.json"))

    def test_broken_element_is_reported_and_not_loaded(self):
        self.assertEqual(self.install("demo").returncode, 0)
        self.ampify("extension", "enable", "demo")
        os.remove(os.path.join(self.kit, "extensions", "demo", "main.js"))
        self.assertIn("invalid", self.ampify("list").stdout)
        apply = self.ampify("apply")
        self.assertEqual(apply.returncode, 1)
        self.assertIn("extension demo", apply.stderr)
        self.assertNotIn("extension demo", self.read("active.txt"))


class Activation(AmpifyTestCase):
    def test_launch_dry_run_names_the_launcher(self):
        result = self.ampify("launch", "--dry-run")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("ampify_launcher.exe", result.stdout)

    def test_restore_dry_run_changes_nothing(self):
        result = self.ampify("restore", "--dry-run")
        self.assertEqual(result.returncode, 0, result.stderr)
        expected = "Would stop the background launcher" if os.name == "nt" else "nothing to turn off"
        self.assertIn(expected, result.stdout)

    def test_apply_from_a_build_folder_leaves_the_automatic_start_alone(self):
        result = self.ampify("apply")
        self.assertEqual(result.returncode, 0, result.stderr)
        if os.name == "nt":
            self.assertIn("not the installed one", result.stdout)

    def test_backup_create_and_list(self):
        self.ampify("config", "theme.active", "night")
        created = self.ampify("backup")
        self.assertEqual(created.returncode, 0, created.stderr)
        name = re.search(r"Backup created: (\S+)", created.stdout).group(1)
        self.assertIn(name, self.ampify("backup", "list").stdout)
        self.assertEqual(self.ampify("restore", "--backup", "absent-19700101-000000").returncode, 1)

    def test_watch_reacts_to_an_edit(self):
        self.ampify("config", "theme.active", "")
        process = subprocess.Popen(
            [Ampify, "--kit-dir", self.kit, "watch",
                                                      "--interval", "100", "--max-cycles", "60"],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, encoding="utf-8",
        )
        time.sleep(1.5)
        self.ampify("config", "theme.parts.colors", "0")
        output, errors = process.communicate(timeout=60)
        self.assertEqual(process.returncode, 0, errors)
        self.assertIn("initial apply", output)
        self.assertIn("changes detected", output)
        self.assertIn("theme.parts.colors = 0", self.settings_text())
        self.assertTrue(os.path.exists(os.path.join(self.kit, "active.txt")))

    def test_apply_refuses_an_enabled_element_that_is_not_installed(self):
        self.ampify("config", "extensions.fantome.enabled", "1")
        result = self.ampify("apply")
        self.assertEqual(result.returncode, 1)
        self.assertIn("enabled, but missing from Ampify", result.stderr)


class KitUpdate(AmpifyTestCase):
    def make_release(self, version, sums_hash=None, release_tag=None):
        bundle_dir = os.path.join(self.tmp, "release", version)
        os.makedirs(bundle_dir, exist_ok=True)
        archive = os.path.join(bundle_dir, f"ampify-{version}-windows-x64.zip")
        with zipfile.ZipFile(archive, "w") as bundle:
            for name in ("ampify.exe", "ampify_launcher.exe", "ampify_kernel.dll"):
                bundle.writestr(name, f"{name} {version}")
            for element in ("apps/marketplace", "extensions/now-playing"):  # they ship with the kit
                bundle.writestr(f"examples/{element}/manifest.json", "{}")
        digest = sha256_of(archive)
        sums = os.path.join(bundle_dir, "SHA256SUMS.txt")
        with open(sums, "w", encoding="utf-8") as handle:
            handle.write(f"{sums_hash or digest}  ampify-{version}-windows-x64.zip\n")
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

    def test_check_reports_a_newer_release(self):
        release = self.make_release("9.9.9")
        result = self.ampify("update", "--check", "--release-url", file_url(release))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("Version 9.9.9 is available", result.stdout)

    def test_same_or_older_release_is_up_to_date(self):
        release = self.make_release("0.1.0")
        result = self.ampify("update", "--release-url", file_url(release))
        self.assertIn("up to date", result.stdout)

    def test_apply_replaces_the_files_and_keeps_the_old_ones(self):
        release = self.make_release("9.9.9")
        install_dir = os.path.join(self.tmp, "install")
        os.makedirs(install_dir)
        with open(os.path.join(install_dir, "ampify.exe"), "w", encoding="utf-8") as handle:
            handle.write("old")

        result = self.ampify("update", "--apply", "--release-url", file_url(release),
                          "--install-dir", install_dir)
        self.assertEqual(result.returncode, 0, result.stderr)
        with open(os.path.join(install_dir, "ampify.exe"), encoding="utf-8") as handle:
            self.assertEqual(handle.read(), "ampify.exe 9.9.9")
        with open(os.path.join(install_dir, "ampify.exe.old"), encoding="utf-8") as handle:
            self.assertEqual(handle.read(), "old")
        self.assertTrue(os.path.exists(os.path.join(install_dir, "ampify_kernel.dll")))
        # The update puts the Marketplace and Now Playing in the user's folder too. That an older copy is replaced
        # is tested in tests/kit_tests.cpp (InstallBundledElements).
        self.assertTrue(os.path.exists(os.path.join(self.kit, "apps", "marketplace", "manifest.json")))
        self.assertTrue(os.path.exists(os.path.join(self.kit, "extensions", "now-playing", "manifest.json")))

    def test_wrong_checksum_refuses_the_update(self):
        release = self.make_release("9.9.9", sums_hash="f" * 64)
        result = self.ampify("update", "--check", "--release-url", file_url(release))
        self.assertEqual(result.returncode, 0)  # --check only reads the version
        result = self.ampify("update", "--apply", "--release-url", file_url(release),
                          "--install-dir", os.path.join(self.tmp, "install"))
        self.assertEqual(result.returncode, 1)
        self.assertIn("wrong SHA-256", result.stderr)


def api_members():
    """Member names of section 5 of api.md (the last segment of each `Ampify.a.b` path), checked against ampify.d.ts."""
    with open(os.path.join(ROOT, "docs", "extensions", "api.md"), encoding="utf-8") as handle:
        text = handle.read()
    section = text[text.index("## 5. The global `Ampify` object"):text.index("## 6. ")]
    paths = re.findall(r"Ampify\.([A-Za-z_]\w*(?:\.[A-Za-z_]\w*)*)", section)
    names = {path.split(".")[-1] for path in paths}
    return sorted(names)


class Build(AmpifyTestCase):
    def test_build_writes_a_zip_that_zipfile_reads(self):
        folder = self.write_element("demo-build", "extension")
        out = os.path.join(self.tmp, "demo-build.zip")
        result = self.ampify("build", folder, out)
        self.assertEqual(result.returncode, 0, result.stderr)
        with zipfile.ZipFile(out) as bundle:
            self.assertIsNone(bundle.testzip())
            self.assertEqual(sorted(bundle.namelist()), ["main.js", "manifest.json"])
        self.assertIn(sha256_of(out), result.stdout)

    def test_build_is_deterministic(self):
        folder = self.write_element("demo-same", "extension")
        first = os.path.join(self.tmp, "one.zip")
        second = os.path.join(self.tmp, "two.zip")
        self.assertEqual(self.ampify("build", folder, first).returncode, 0)
        self.assertEqual(self.ampify("build", folder, second).returncode, 0)
        self.assertEqual(sha256_of(first), sha256_of(second))

    def test_build_refuses_an_invalid_element_and_writes_nothing(self):
        folder = self.write_element("demo-bad", "extension")
        os.remove(os.path.join(folder, "main.js"))
        out = os.path.join(self.tmp, "bad.zip")
        result = self.ampify("build", folder, out)
        self.assertEqual(result.returncode, 1)
        self.assertIn("invalid", result.stderr)
        self.assertFalse(os.path.exists(out))

    def test_build_refuses_to_overwrite(self):
        folder = self.write_element("demo-over", "extension")
        out = os.path.join(self.tmp, "over.zip")
        with open(out, "w", encoding="utf-8") as handle:
            handle.write("busy")
        result = self.ampify("build", folder, out)
        self.assertEqual(result.returncode, 1)
        self.assertIn("already exists", result.stderr)

    def test_built_archive_installs_from_a_local_catalog(self):
        folder = self.write_element("demo-built", "extension")
        out = os.path.join(self.tmp, "demo-built.zip")
        built = self.ampify("build", folder, out)
        self.assertEqual(built.returncode, 0, built.stderr)
        entry = json.loads(built.stdout[built.stdout.index("{"):])
        entry["url"] = file_url(out)
        catalog = self.write_catalog([entry])
        installed = self.ampify("install", "demo-built", *self.catalog_option(catalog), "--yes")
        self.assertEqual(installed.returncode, 0, installed.stderr)
        self.assertTrue(os.path.exists(os.path.join(self.kit, "extensions", "demo-built", "manifest.json")))


class Inspection(AmpifyTestCase):
    def test_inspect_lists_files_with_checksums(self):
        folder = self.write_element("demo-look", "extension")
        result = self.ampify("inspect", folder)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertRegex(result.stdout, r"[0-9a-f]{64}  main\.js")

    def test_inspect_an_installed_element_shows_its_state(self):
        folder = self.write_element("demo-state", "extension")
        self.assertEqual(self.ampify("dev", folder, "--max-cycles", "1").returncode, 0)
        result = self.ampify("inspect", "extension", "demo-state")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertRegex(result.stdout, r"enabled\s+yes")

    def test_inspect_rejects_an_unknown_kind(self):
        result = self.ampify("inspect", "widget", "demo")
        self.assertEqual(result.returncode, 1)
        self.assertIn("unknown type", result.stderr)

    def test_types_declares_every_member_of_section_5(self):
        path = os.path.join(self.tmp, "ampify.d.ts")
        result = self.ampify("types", path)
        self.assertEqual(result.returncode, 0, result.stderr)
        with open(path, encoding="utf-8") as handle:
            declared = handle.read()
        missing = [name for name in api_members() if not re.search(r"\b%s\b" % re.escape(name), declared)]
        self.assertEqual(missing, [])

    def test_types_refuses_to_overwrite_without_yes(self):
        path = os.path.join(self.tmp, "ampify.d.ts")
        with open(path, "w", encoding="utf-8") as handle:
            handle.write("my file")
        result = self.ampify("types", path)
        self.assertEqual(result.returncode, 1)
        self.assertIn("already exists", result.stderr)
        with open(path, encoding="utf-8") as handle:
            self.assertEqual(handle.read(), "my file")
        self.assertEqual(self.ampify("types", path, "--yes").returncode, 0)


class Diagnostics(AmpifyTestCase):
    def test_empty_kit_is_a_warning_not_an_error(self):
        result = self.ampify("doctor")
        self.assertIn("Ampify folder missing", result.stdout)
        self.assertIn("Summary", result.stdout)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("Summary: 0 error(s)", result.stdout)

    def test_enabled_element_that_is_missing_is_reported(self):
        self.assertEqual(self.ampify("config", "extensions.ghost.enabled", "1").returncode, 0)
        result = self.ampify("doctor")
        self.assertIn("enabled but not loaded", result.stdout)
        self.assertIn("ghost", result.stdout)


class Report(AmpifyTestCase):
    def write_kit(self):
        logs = os.path.join(self.kit, "logs")
        os.makedirs(logs)
        user = os.environ.get("USERNAME") or os.environ.get("USER") or "nobody"
        home = os.path.expanduser("~")
        with open(os.path.join(logs, "AppleMusic.log"), "w", encoding="utf-8") as handle:
            handle.write(
                f"[08:00:00.000] Kernel loaded from {home}\\Ampify for {user}\n"
                '[08:00:01.000] Player: cover of "Secret Song Title": address '
                '"amp-artwork-id://17820666364333712412.dbid/783EDFEC-D43D-47E2-B448-8ECCFFE09C81_1.1"\n'
                '[08:00:02.000] Theme "glass": 750 value(s) applied\n'
            )
        with open(os.path.join(self.kit, "settings.txt"), "w", encoding="utf-8") as handle:
            handle.write("theme.active = glass\nextensions.scrobbler.session_key = abc123secret\n")
        return user

    def test_report_gathers_the_state_with_names_and_music_taken_out(self):
        user = self.write_kit()
        out = os.path.join(self.tmp, "report.zip")
        result = self.ampify("report", out, timeout=120)
        self.assertEqual(result.returncode, 0, result.stderr)
        with zipfile.ZipFile(out) as bundle:
            names = bundle.namelist()
            text = "\n".join(bundle.read(name).decode("utf-8", "replace") for name in names)
        for expected in ("README.txt", "system.txt", "doctor.txt", "settings.txt", "logs/AppleMusic.log"):
            self.assertIn(expected, names)
        self.assertIn('Theme "glass": 750 value(s) applied', text)
        self.assertIn("theme.active = glass", text)
        self.assertIn("session_key = <hidden>", text)
        for secret in ("Secret Song", "abc123secret", "17820666364333712412", "783EDFEC"):
            self.assertNotIn(secret, text)
        if len(user) >= 3:
            self.assertNotRegex(text, r"(?i)(?<![A-Za-z0-9_])" + re.escape(user) + r"(?![A-Za-z0-9_])")

    def test_report_refuses_to_overwrite(self):
        out = os.path.join(self.tmp, "taken.zip")
        with open(out, "w", encoding="utf-8") as handle:
            handle.write("busy")
        result = self.ampify("report", out)
        self.assertEqual(result.returncode, 1)
        self.assertIn("already exists", result.stderr)


class Development(AmpifyTestCase):
    def write_journal(self, text):
        os.makedirs(os.path.join(self.kit, "logs"), exist_ok=True)
        path = os.path.join(self.kit, "logs", "AppleMusic.log")
        with open(path, "w", encoding="utf-8") as handle:
            handle.write(text)
        return path

    def test_logs_prints_the_last_lines(self):
        self.write_journal("un\ndeux\ntrois\nquatre\ncinq\n")
        result = self.ampify("logs", "2")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.splitlines(), ["quatre", "cinq"])

    def test_logs_without_journal_says_so(self):
        result = self.ampify("logs")
        self.assertEqual(result.returncode, 0)
        self.assertIn("No log yet", result.stdout)

    def test_logs_follow_prints_lines_added_later(self):
        path = self.write_journal("before\n")
        command = [
            Ampify, "--kit-dir", self.kit, "logs", "--follow", "--interval", "100", "--max-cycles", "40",
        ]
        process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, encoding="utf-8")
        time.sleep(0.5)
        with open(path, "a", encoding="utf-8") as handle:
            handle.write("after\n")
        out, _ = process.communicate(timeout=60)
        self.assertEqual(process.returncode, 0)
        self.assertEqual(out.splitlines(), ["before", "after"])

    def test_dev_copies_the_element_into_the_kit_and_enables_it(self):
        folder = self.write_element("demo-dev", "extension")
        result = self.ampify("dev", folder, "--max-cycles", "1")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(os.path.exists(os.path.join(self.kit, "extensions", "demo-dev", "main.js")))
        self.assertRegex(self.settings_text(), r"extensions\.demo-dev\.enabled\s*=\s*1")

    def test_dev_keeps_the_working_copy_when_an_edit_breaks_it(self):
        folder = self.write_element("demo-broken", "extension")
        command = [
            Ampify, "--kit-dir", self.kit, "dev", folder, "--interval", "100", "--max-cycles", "40",
        ]
        process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, encoding="utf-8")
        time.sleep(0.8)
        with open(os.path.join(folder, "manifest.json"), "w", encoding="utf-8") as handle:
            handle.write("{ casse")
        out, err = process.communicate(timeout=60)
        self.assertEqual(process.returncode, 0)
        self.assertIn("reloaded", out)
        self.assertIn("invalid", err)
        self.assertEqual(read_json(os.path.join(self.kit, "extensions", "demo-broken", "manifest.json"))["id"], "demo-broken")

    def test_dev_refuses_an_invalid_element(self):
        folder = self.write_element("demo-no-main", "extension")
        os.remove(os.path.join(folder, "main.js"))
        result = self.ampify("dev", folder, "--max-cycles", "1")
        self.assertEqual(result.returncode, 1)
        self.assertFalse(os.path.exists(os.path.join(self.kit, "extensions", "demo-no-main")))

    def test_dev_asks_the_kernel_to_restart_the_element(self):
        folder = self.write_element("demo-restart", "extension")
        result = self.ampify("dev", folder, "--max-cycles", "1")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("restart demo-restart", self.read("reload.txt").splitlines())

    def test_apply_asks_for_no_restart(self):
        result = self.ampify("apply")
        self.assertIn(result.returncode, (0, 1), result.stderr)
        self.assertFalse(any(line.startswith("restart ") for line in self.read("reload.txt").splitlines()))


class Verification(AmpifyTestCase):
    def test_test_runs_the_example_in_the_engine(self):
        folder = os.path.join(ROOT, "examples", "hello-extension")
        result = self.ampify("test", folder)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("loaded and run without error", result.stdout)
        self.assertIn("Hello, launch #1", result.stdout)

    def test_test_accepts_a_folder_with_a_trailing_separator(self):
        folder = os.path.join(ROOT, "examples", "hello-extension")
        result = self.ampify("test", folder + os.sep)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("Hello, launch #1", result.stdout)

    def test_test_refuses_a_call_without_its_permission(self):
        folder = self.write_element("demo-denied", "extension", files={"main.js": 'Ampify.storage.get("x", 0);\n'})
        result = self.ampify("test", folder)
        self.assertEqual(result.returncode, 1)
        self.assertIn("missing permission: storage", result.stdout)

    def test_test_runs_the_react_panel_example(self):
        folder = os.path.join(ROOT, "examples", "react-panel")
        result = self.ampify("test", folder)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("loaded and run without error", result.stdout)
        self.assertIn("React panel ready", result.stdout)

    def test_test_writes_nothing_to_the_kit(self):
        folder = os.path.join(ROOT, "examples", "sleep-timer")
        result = self.ampify("test", folder)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertFalse(os.path.exists(self.kit))


class Transfer(AmpifyTestCase):
    def export_one(self, name):
        folder = self.write_element(name, "extension")
        self.assertEqual(self.ampify("dev", folder, "--max-cycles", "1").returncode, 0)
        out = os.path.join(self.tmp, name + ".zip")
        result = self.ampify("export", out)
        self.assertEqual(result.returncode, 0, result.stderr)
        return out

    def test_profile_save_use_list_and_delete(self):
        self.assertEqual(self.ampify("config", "catalog.url", "https://example.org/a.json").returncode, 0)
        self.assertEqual(self.ampify("profile", "save", "work").returncode, 0)
        self.assertEqual(self.ampify("config", "catalog.url", "https://example.org/b.json").returncode, 0)
        result = self.ampify("profile", "use", "work")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("Previous state backed up", result.stdout)
        self.assertEqual(self.ampify("config", "catalog.url").stdout.strip(), "https://example.org/a.json")
        self.assertIn("work", self.ampify("profile", "list").stdout.split())
        self.assertEqual(self.ampify("profile", "delete", "work").returncode, 0)
        self.assertIn("No profile", self.ampify("profile", "list").stdout)

    def test_profile_name_and_missing_profile_are_refused(self):
        self.assertEqual(self.ampify("profile", "save", "Pas Bon").returncode, 1)
        result = self.ampify("profile", "use", "absent")
        self.assertEqual(result.returncode, 1)
        self.assertIn("not found", result.stderr)

    def test_export_writes_no_app_or_state_files(self):
        out = self.export_one("demo-export")
        with zipfile.ZipFile(out) as bundle:
            names = bundle.namelist()
        self.assertIn("Ampify-EXPORT", names)
        self.assertIn("extensions/demo-export/manifest.json", names)
        self.assertFalse(any(name.startswith(("backups", "logs", "profiles", "cache")) for name in names))

    def test_export_refuses_to_overwrite(self):
        out = os.path.join(self.tmp, "taken.zip")
        with open(out, "w", encoding="utf-8") as handle:
            handle.write("busy")
        result = self.ampify("export", out)
        self.assertEqual(result.returncode, 1)
        self.assertIn("already exists", result.stderr)

    def test_export_then_import_restores_elements_and_settings(self):
        out = self.export_one("demo-trans")
        shutil.rmtree(self.kit)
        result = self.ampify("import", out, "--yes")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(os.path.exists(os.path.join(self.kit, "extensions", "demo-trans", "manifest.json")))
        self.assertRegex(self.settings_text(), r"extensions\.demo-trans\.enabled\s*=\s*1")

    def test_import_dry_run_writes_nothing(self):
        out = self.export_one("demo-dry")
        shutil.rmtree(self.kit)
        result = self.ampify("import", out, "--dry-run")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("nothing was written", result.stdout)
        self.assertFalse(os.path.exists(os.path.join(self.kit, "extensions")))

    def test_import_refuses_an_archive_that_is_not_an_export(self):
        folder = self.write_element("demo-plain", "extension")
        archive, _ = self.package(folder)
        result = self.ampify("import", archive, "--yes")
        self.assertEqual(result.returncode, 1)
        self.assertIn("not an ampify export", result.stderr)


class Chaining(AmpifyTestCase):
    def test_verbs_without_argument_run_in_order(self):
        result = self.ampify("backup", "apply")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("Backup created", result.stdout)
        self.assertLess(result.stdout.index("Backup created"), result.stdout.index("Apple Music picks the changes up"))
        self.assertTrue(os.path.exists(os.path.join(self.kit, "active.txt")))

    def test_chain_stops_at_the_first_failure(self):
        self.ampify("config", "extensions.fantome.enabled", "1")
        result = self.ampify("apply", "backup")
        self.assertEqual(result.returncode, 1)
        self.assertIn('"apply" failed', result.stderr)
        self.assertNotIn("Backup created", result.stdout)
        self.assertIn("No backups", self.ampify("backup", "list").stdout)

    def test_a_word_that_is_not_a_verb_keeps_one_command(self):
        result = self.ampify("backup", "list")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("No backups", result.stdout)
        self.assertEqual(self.ampify("devtools", "off").returncode, 0)

    def test_version_option_and_launch_chain(self):
        result = self.ampify("--version")
        self.assertEqual(result.returncode, 0)
        self.assertIn("ampify ", result.stdout)
        result = self.ampify("apply", "devtools", "launch", "--dry-run")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("Inspector turned on", result.stdout)
        self.assertIn("ampify_launcher.exe", result.stdout)


class Paths(AmpifyTestCase):
    def test_one_path_per_name(self):
        result = self.ampify("path", "kit")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.strip(), self.kit)
        self.assertTrue(self.ampify("path", "settings").stdout.strip().endswith("settings.txt"))
        for name in ("extensions", "themes", "apps", "snippets"):
            self.assertTrue(self.ampify("path", name).stdout.strip().endswith(name))
        self.assertTrue(self.ampify("path", "logs").stdout.strip().endswith("logs"))
        result = self.ampify("path", "invented")
        self.assertEqual(result.returncode, 2)
        self.assertIn("unknown name", result.stderr)

    def test_app_and_prefs_are_found_on_their_own(self):
        for name in ("app", "prefs"):
            result = self.ampify("path", name)
            self.assertIn(result.returncode, (0, 1))
            # A Windows PC may really have Apple Music installed; elsewhere it is never found.
            if os.name != "nt":
                self.assertEqual(result.returncode, 1)
                self.assertIn("not found", result.stderr)
            self.assertNotIn("config", result.stderr, "no setting overrides these paths")

    def test_userdata_is_not_a_path_name(self):
        result = self.ampify("path", "userdata")
        self.assertEqual(result.returncode, 2)
        self.assertIn("unknown name", result.stderr)

    def test_config_dir_names_the_kit_folder(self):
        result = self.ampify("config", "--dir", "--dry-run")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(self.kit, result.stdout)
        self.assertIn("Command:", result.stdout)
        self.assertTrue(os.path.isdir(self.kit))


class Devtools(AmpifyTestCase):
    def test_devtools_switches_the_inspector(self):
        result = self.ampify("devtools")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("inspector.txt", result.stdout)
        self.assertIn("theme.inspect = 1", self.settings_text())
        self.assertEqual(self.ampify("devtools", "off").returncode, 0)
        self.assertIn("theme.inspect = 0", self.settings_text())
        self.assertEqual(self.ampify("devtools", "maybe").returncode, 2)

    def test_inspect_tree_prints_the_last_dump(self):
        result = self.ampify("inspect", "--tree")
        self.assertEqual(result.returncode, 1)
        self.assertIn("ampify devtools", result.stderr)
        os.makedirs(os.path.join(self.kit, "logs"))
        with open(os.path.join(self.kit, "logs", "inspector.txt"), "w", encoding="utf-8") as handle:
            handle.write("Window\n  Grid#Racine\n")
        result = self.ampify("inspect", "--tree")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("Grid#Racine", result.stdout)


class NewSettings(AmpifyTestCase):
    def test_booleans_are_checked_and_stored_as_1_or_0(self):
        for key in ("check_kit_update", "expose_api", "theme.inspect", "theme.parts.assets"):
            self.assertEqual(self.ampify("config", key, "on").returncode, 0, key)
            self.assertIn(f"{key} = 1", self.settings_text())
            self.assertEqual(self.ampify("config", key, "maybe").returncode, 1, key)

    def test_launch_flags_are_split_and_shown_by_launch(self):
        result = self.ampify("config", "app_launch_flags", "--remote-debugging-port=9222")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.ampify("config", "app_launch_flags", '--a "two words').returncode, 1)
        self.assertEqual(self.ampify("config", "app_launch_flags", '--x=1 "--y=two words"').returncode, 0)
        result = self.ampify("launch", "--dry-run")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('Arguments for Apple Music (app_launch_flags): --x=1 "--y=two words"', result.stdout)


class TestedVersions(AmpifyTestCase):
    REQUIRED = ["AVCFPlayerCopyCurrentItem", "AVCFPlayerItemCopyCurrentStableVariantID", "AVCFPlayerItemCopyTracks",
                "AVCFPlayerItemTrackCopyAssetTrack", "AVCFPlayerSetVolume"]

    def write_versions(self, versions, sha=None):
        path = os.path.join(self.tmp, "versions.json")
        with open(path, "w", encoding="utf-8") as handle:
            json.dump({"versionsFormat": 1, "playerDll": "AVFoundationCF.dll", "versions": versions}, handle)
        catalog = os.path.join(self.tmp, "catalog.json")
        with open(catalog, "w", encoding="utf-8") as handle:
            json.dump({"catalogVersion": 1, "extensions": [],
                       "versions": {"url": file_url(path), "sha256": sha or sha256_of(path)}}, handle)
        return catalog

    def test_update_writes_the_fully_checked_versions(self):
        catalog = self.write_versions([
            {"version": "1.1600.1.0", "tested": "2026-10-30", "exports": self.REQUIRED},
            {"version": "1.1700.2.0", "tested": "2026-10-30", "exports": self.REQUIRED[:2]},
        ])
        result = self.ampify("versions", "update", *self.catalog_option(catalog))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("ignored    1.1700.2.0", result.stdout)
        text = self.read("versions.txt")
        self.assertTrue(text.startswith("ampify-versions 1\n"))
        self.assertIn("1.1600.1.0\n", text)
        self.assertNotIn("1.1700.2.0", text)
        listing = self.ampify("versions").stdout
        self.assertIn("1.1540.23042.0", listing)
        self.assertIn("1.1600.1.0", listing)
        self.assertIn("versions.txt", listing)

    def test_wrong_checksum_writes_nothing(self):
        catalog = self.write_versions([{"version": "1.1600.1.0", "tested": "2026-10-30", "exports": self.REQUIRED}],
                                      sha="0" * 64)
        result = self.ampify("versions", "update", *self.catalog_option(catalog))
        self.assertEqual(result.returncode, 1)
        self.assertIn("SHA-256 differs", result.stderr)
        self.assertFalse(os.path.exists(os.path.join(self.kit, "versions.txt")))

    def test_catalog_without_versions_is_reported(self):
        catalog = self.write_catalog([])
        result = self.ampify("versions", "update", *self.catalog_option(catalog))
        self.assertEqual(result.returncode, 1)
        self.assertIn("does not publish a list of versions", result.stderr)

    def test_malformed_list_is_ignored(self):
        os.makedirs(self.kit)
        with open(os.path.join(self.kit, "versions.txt"), "w", encoding="utf-8") as handle:
            handle.write("ampify-versions 1\n1.1600.1.0\nn'importe quoi\n")
        listing = self.ampify("versions", "list")
        self.assertEqual(listing.returncode, 0, listing.stderr)
        self.assertNotIn("1.1600.1.0", listing.stdout)
        self.assertIn("ignored", listing.stdout)


class KitRelease(AmpifyTestCase):
    make_release = KitUpdate.make_release

    def test_install_kit_takes_an_older_version(self):
        release = self.make_release("0.0.9")
        install_dir = os.path.join(self.tmp, "install")
        os.makedirs(install_dir)
        result = self.ampify("install-kit", "0.0.9", "--apply", "--release-url", file_url(release),
                          "--install-dir", install_dir)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("is older than the installed one", result.stdout)
        with open(os.path.join(install_dir, "ampify.exe"), encoding="utf-8") as handle:
            self.assertEqual(handle.read(), "ampify.exe 0.0.9")

    def test_install_kit_check_and_wrong_version(self):
        release = self.make_release("9.9.9")
        result = self.ampify("install-kit", "v9.9.9", "--check", "--release-url", file_url(release))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("ampify install-kit 9.9.9", result.stdout)
        result = self.ampify("install-kit", "1.2.3", "--release-url", file_url(release))
        self.assertEqual(result.returncode, 1)
        self.assertIn("not version 1.2.3", result.stderr)
        self.assertEqual(self.ampify("install-kit", "latest").returncode, 1)
        self.assertEqual(self.ampify("install-kit").returncode, 2)

    def test_install_kit_without_apply_says_how_to_finish(self):
        release = self.make_release("0.0.8")
        result = self.ampify("install-kit", "0.0.8", "--release-url", file_url(release))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("ampify install-kit 0.0.8 --apply", result.stdout)


class KitUpdateNotice(AmpifyTestCase):
    make_release = KitUpdate.make_release

    def cache_path(self):
        return os.path.join(self.kit, "cache", "kit-update.txt")

    def test_no_check_unless_enabled(self):
        release = self.make_release("9.9.9")
        result = self.ampify("list", "--release-url", file_url(release))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn("is available", result.stderr)
        self.assertFalse(os.path.exists(self.cache_path()))

    def test_notice_is_printed_once_checked_and_then_cached(self):
        release = self.make_release("9.9.9")
        self.assertEqual(self.ampify("config", "check_kit_update", "1").returncode, 0)
        result = self.ampify("list", "--release-url", file_url(release))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("version 9.9.9 of Ampify is available", result.stderr)
        self.assertTrue(os.path.exists(self.cache_path()))

        os.remove(release)  # a second check would fail: the notice must come from the cache
        result = self.ampify("apply", "--release-url", file_url(release))
        self.assertIn("9.9.9", result.stderr)

        result = self.ampify("path", "kit", "--release-url", file_url(release))
        self.assertNotIn("disponible", result.stderr)
        self.assertEqual(result.stdout.strip(), self.kit)

    def test_old_cache_is_checked_again(self):
        release = self.make_release("0.0.1")
        self.ampify("config", "check_kit_update", "1")
        os.makedirs(os.path.dirname(self.cache_path()), exist_ok=True)
        with open(self.cache_path(), "w", encoding="utf-8") as handle:
            handle.write("checked=0\nlatest=9.9.9\n")
        result = self.ampify("doctor", "--release-url", file_url(release))
        self.assertNotIn("is available", result.stderr)
        with open(self.cache_path(), encoding="utf-8") as handle:
            self.assertIn("latest=0.0.1", handle.read())


if __name__ == "__main__":
    unittest.main()
