"""scripts/install.ps1 runs alone (irm ... | iex), with no checkout to read facts/release.json from: its literals must
be the facts. Run with: python -m unittest test_facts (in tools/)."""

import json
import os
import unittest

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")


def read_text(*parts):
    with open(os.path.join(ROOT, *parts), encoding="utf-8") as handle:
        return handle.read()


class InstallScriptFollowsTheReleaseFacts(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.facts = json.loads(read_text("facts", "release.json"))
        cls.script = read_text("scripts", "install.ps1")

    def test_it_asks_the_project_repository_for_its_releases(self):
        self.assertIn(f"$repository = '{self.facts['project']['repository']}'", self.script)
        api = self.facts["project"]["releaseApi"].replace(self.facts["project"]["repository"], "$repository")
        self.assertIn(f'-Uri "{api}/$tag"', self.script)
        self.assertIn(f"raw.githubusercontent.com/{self.facts['project']['repository']}/main/scripts/install.ps1",
                      self.script)

    def test_it_looks_for_the_package_and_its_checksums_by_their_release_names(self):
        package = self.facts["package"]
        literal = lambda text: text.replace(".", "\\.")  # noqa: E731 - the only regex character in the names
        pattern = "^" + literal(package["prefix"]) + "[0-9.]+" + literal(package["suffix"]) + "$"
        self.assertIn(f"'{pattern}'", self.script)
        self.assertIn(f"$_.name -eq '{package['checksums']}'", self.script)

    def test_it_runs_the_setup_of_the_package(self):
        self.assertIn(f"Join-Path $files '{self.facts['files']['setup']}'", self.script)

    def test_it_names_the_install_and_data_folders_of_the_kit(self):
        folder = self.facts["kitFolder"]
        self.assertIn(f"'Programs\\{folder}'", self.script)
        self.assertIn(f"$env:USERPROFILE\\{folder}\\logs\\install.log", self.script)

    def test_its_example_version_is_the_release(self):
        self.assertIn(f"$env:AMPIFY_VERSION = '{self.facts['version']}'", self.script)


if __name__ == "__main__":
    unittest.main()
