"""What scripts/record-corpus.ps1 does without Apple Music: its arguments, its list of pages, and the scrub step on a
dump already recorded. Recording itself needs Apple Music with Ampify, and is not run here.

    AMPIFY_BIN=build-msvc/release/ampify.exe python -m unittest test_record_corpus   (in tools/)
"""

import json
import os
import shutil
import subprocess
import tempfile
import unittest

TOOLS = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(TOOLS)
SCRIPT = os.path.join(ROOT, "scripts", "record-corpus.ps1")
FIXTURE = os.path.join(ROOT, "tests", "data", "replay", "transport-bar.json")
AMPIFY = os.environ.get("AMPIFY_BIN") or os.path.join(ROOT, "build-msvc", "release", "ampify.exe")
PWSH = shutil.which("pwsh")


def run(*args):
    return subprocess.run([PWSH, "-NoProfile", "-NonInteractive", "-File", SCRIPT, *args], capture_output=True,
                          text=True, encoding="utf-8", errors="replace", timeout=120)


@unittest.skipUnless(PWSH, "PowerShell 7 (pwsh) is needed")
class RecordCorpusArguments(unittest.TestCase):
    def test_lists_every_page_and_state_without_touching_anything(self):
        result = run("-ListPages", "-Kit", os.path.join(tempfile.gettempdir(), "no-such-kit"))
        self.assertEqual(result.returncode, 0, result.stderr)
        for page in ("home", "new", "radio", "albums", "album", "artist", "playlists", "songs", "search", "settings",
                     "lyrics", "queue", "marketplace", "dialog"):
            self.assertRegex(result.stdout, r"\n  %s +" % page)
        self.assertIn("idle", result.stdout)
        self.assertIn("playing", result.stdout)

    def test_keeps_the_pages_asked_for_in_their_order(self):
        result = run("-ListPages", "-Pages", "queue,home", "-States", "idle")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertLess(result.stdout.index("  queue"), result.stdout.index("  home"))
        self.assertNotRegex(result.stdout, r"\n  playing +")

    def test_reads_its_arguments_where_no_windows_profile_variable_is_set(self):
        # Linux CI: USERPROFILE is unset, and the default kit folder must not stop the script before it starts.
        env = {key: value for key, value in os.environ.items() if key.upper() != "USERPROFILE"}
        result = subprocess.run([PWSH, "-NoProfile", "-NonInteractive", "-File", SCRIPT, "-ListPages"],
                                capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=120,
                                env=env)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("idle", result.stdout)

    def test_refuses_an_unknown_page_or_state(self):
        page = run("-ListPages", "-Pages", "home,lobby")
        self.assertNotEqual(page.returncode, 0)
        self.assertIn("unknown page 'lobby'", page.stderr + page.stdout)
        state = run("-ListPages", "-States", "paused")
        self.assertNotEqual(state.returncode, 0)
        self.assertIn("unknown state 'paused'", state.stderr + state.stdout)


@unittest.skipUnless(PWSH and os.path.exists(AMPIFY), "pwsh and a built ampify.exe (AMPIFY_BIN) are needed")
class RecordCorpusScrub(unittest.TestCase):
    def setUp(self):
        self.corpus = tempfile.mkdtemp(prefix="ampify-corpus-")

    def tearDown(self):
        shutil.rmtree(self.corpus, ignore_errors=True)

    def test_scrubs_an_existing_dump_into_the_corpus_under_its_state_and_page(self):
        result = run("-ScrubOnly", FIXTURE, "-Pages", "queue", "-States", "playing", "-Corpus", self.corpus,
                     "-Ampify", AMPIFY, "-Kit", os.path.join(self.corpus, "no-kit"))
        self.assertEqual(result.returncode, 0, result.stderr)
        target = os.path.join(self.corpus, "playing", "queue.json")
        with open(target, encoding="utf-8") as handle:
            dump = json.load(handle)
        self.assertTrue(dump["scrubbed"])
        self.assertEqual(len(dump["nodes"]), 118)
        self.assertFalse(os.path.exists(os.path.join(self.corpus, "no-kit")))  # settings.txt never read or written

    def test_refuses_a_missing_dump(self):
        result = run("-ScrubOnly", os.path.join(self.corpus, "none.json"), "-Corpus", self.corpus, "-Ampify", AMPIFY)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("no dump at", result.stderr + result.stdout)


if __name__ == "__main__":
    unittest.main()
