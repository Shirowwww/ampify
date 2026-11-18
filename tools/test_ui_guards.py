"""Apple Music ends its interface thread by draining its DispatcherQueue after XAML has shut down, and any XAML call
there crashed it: every interface timer's tick and every queued task of src/win goes through OnUiTick or TryEnqueueUi
(src/win/xaml_ending.hpp), which do nothing by then. Run with: python -m unittest test_ui_guards (in tools/)."""

import os
import re
import unittest

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
WIN = os.path.join(ROOT, "src", "win")

# The guards themselves, and XamlPost, which checks XamlEnding() in its own task.
ALLOWED = {"xaml_ending.hpp", "xaml_host.cpp"}
RAW_ENQUEUE = re.compile(r"\.TryEnqueue\(")
RAW_TICK = re.compile(r"(?:\.|->)Tick\(\s*\[")


def sources():
    for folder, _, names in os.walk(WIN):
        for name in names:
            if name.endswith((".cpp", ".hpp")) and name not in ALLOWED:
                path = os.path.join(folder, name)
                with open(path, encoding="utf-8") as handle:
                    yield os.path.relpath(path, ROOT), handle.read()


def offences(pattern):
    found = []
    for path, text in sources():
        for number, line in enumerate(text.splitlines(), 1):
            if pattern.search(line):
                found.append(f"{path}:{number}: {line.strip()}")
    return found


class InterfaceWorkStopsWhenXamlEnds(unittest.TestCase):
    def test_queued_tasks_go_through_try_enqueue_ui(self):
        self.assertEqual(offences(RAW_ENQUEUE), [])

    def test_timer_ticks_go_through_on_ui_tick(self):
        self.assertEqual(offences(RAW_TICK), [])

    def test_the_guards_are_used(self):
        uses = sum(text.count("TryEnqueueUi(") + text.count("OnUiTick(") for _, text in sources())
        self.assertGreater(uses, 40)


if __name__ == "__main__":
    unittest.main()
